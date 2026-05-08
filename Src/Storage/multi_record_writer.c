#include "Storage/multi_record_writer.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#include <string.h>

#define MRW_RING_USABLE_FRAMES (MULTI_RECORD_WRITER_RING_FRAMES - 1U)

typedef struct
{
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    multi_record_writer_state_t state;
    multi_record_writer_error_t error;
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t frames_received;
    uint32_t frames_drained;
    uint32_t simulated_bytes_drained;
    char temp_path[MULTI_RECORD_WRITER_PATH_MAX];
    char final_path[MULTI_RECORD_WRITER_PATH_MAX];
} multi_record_writer_client_t;

SDRAM_RECORDER static int32_t
    g_record_rings[MULTI_RECORD_WRITER_MAX_CLIENTS][MULTI_RECORD_WRITER_RING_FRAMES * MULTI_RECORD_WRITER_CHANNELS];
static multi_record_writer_client_t g_record_clients[MULTI_RECORD_WRITER_MAX_CLIENTS];

_Static_assert(MULTI_RECORD_WRITER_MAX_CLIENTS > 0U, "record writer needs at least one client");
_Static_assert(MULTI_RECORD_WRITER_RING_FRAMES > 1U, "record writer ring needs spare frame");
_Static_assert(MULTI_RECORD_WRITER_SAMPLE_RATE_HZ == 48000U, "record writer target rate is fixed");
_Static_assert(MULTI_RECORD_WRITER_CHANNELS == 2U, "record writer target is stereo");
_Static_assert(MULTI_RECORD_WRITER_BITS_PER_SAMPLE == 24U, "record writer target is PCM24");

static uint8_t client_id_valid(uint8_t client_id)
{
    return (client_id < MULTI_RECORD_WRITER_MAX_CLIENTS) ? 1U : 0U;
}

static uint32_t ring_pending_frames(const multi_record_writer_client_t *client)
{
    const uint32_t wr = client->write_index;
    const uint32_t rd = client->read_index;
    return (wr >= rd) ? (wr - rd) : (MULTI_RECORD_WRITER_RING_FRAMES - (rd - wr));
}

static void ring_reset(multi_record_writer_client_t *client)
{
    client->write_index = 0U;
    client->read_index = 0U;
}

static void set_error(multi_record_writer_client_t *client, multi_record_writer_error_t error)
{
    client->error = error;
}

static uint8_t copy_path(char *dst, const char *src)
{
    if((dst == 0) || (src == 0) || (src[0] == '\0'))
        return 0U;

    uint32_t i = 0U;
    while((i + 1U) < MULTI_RECORD_WRITER_PATH_MAX)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
            return 1U;
        i++;
    }

    dst[0] = '\0';
    return 0U;
}

static int32_t pick_most_filled_client(void)
{
    int32_t best = -1;
    uint32_t best_pending = 0U;

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        multi_record_writer_client_t *client = &g_record_clients[i];
        if((client->state != MULTI_RECORD_WRITER_STATE_RECORDING) &&
           (client->state != MULTI_RECORD_WRITER_STATE_DRAINING))
        {
            continue;
        }

        const uint32_t pending = ring_pending_frames(client);
        if(pending > best_pending)
        {
            best_pending = pending;
            best = (int32_t)i;
        }
    }

    return best;
}

static void drain_client_frames(uint32_t client_id, uint32_t frames)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    client->read_index = (client->read_index + frames) % MULTI_RECORD_WRITER_RING_FRAMES;
    client->frames_drained += frames;
    client->simulated_bytes_drained += frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
}

void multi_record_writer_init(void)
{
    memset(g_record_clients, 0, sizeof(g_record_clients));
    memset(g_record_rings, 0, sizeof(g_record_rings));

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        g_record_clients[i].state = MULTI_RECORD_WRITER_STATE_IDLE;
        g_record_clients[i].error = MULTI_RECORD_WRITER_ERROR_NONE;
    }
}

uint8_t multi_record_writer_prepare_temp(uint8_t client_id, const char *path)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->state != MULTI_RECORD_WRITER_STATE_IDLE) &&
       (client->state != MULTI_RECORD_WRITER_STATE_COMMITTED) &&
       (client->state != MULTI_RECORD_WRITER_STATE_FAILED))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    if(copy_path(client->temp_path, path) == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_PATH);
        return 0U;
    }

    client->final_path[0] = '\0';
    client->error = MULTI_RECORD_WRITER_ERROR_NONE;
    client->high_watermark = 0U;
    client->overflow_count = 0U;
    client->dropped_frames = 0U;
    client->frames_received = 0U;
    client->frames_drained = 0U;
    client->simulated_bytes_drained = 0U;
    ring_reset(client);
    client->state = MULTI_RECORD_WRITER_STATE_PREPARED;
    return 1U;
}

uint8_t multi_record_writer_start(uint8_t client_id)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(client->state != MULTI_RECORD_WRITER_STATE_PREPARED)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    ring_reset(client);
    client->state = MULTI_RECORD_WRITER_STATE_RECORDING;
    return 1U;
}

uint8_t multi_record_writer_request_stop(uint8_t client_id)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(client->state != MULTI_RECORD_WRITER_STATE_RECORDING)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    client->state = MULTI_RECORD_WRITER_STATE_STOP_REQUESTED;
    return 1U;
}

uint8_t multi_record_writer_push_audio_block_from_irq(uint8_t client_id,
                                                      const int32_t *lr_interleaved,
                                                      uint32_t frames)
{
    if((client_id_valid(client_id) == 0U) || (lr_interleaved == 0) || (frames == 0U))
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(client->state != MULTI_RECORD_WRITER_STATE_RECORDING)
        return 0U;

    const uint32_t pending = ring_pending_frames(client);
    const uint32_t available_frames = MRW_RING_USABLE_FRAMES - pending;
    if(frames > available_frames)
    {
        client->overflow_count++;
        client->dropped_frames += frames;
        client->error = MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
        return 0U;
    }

    uint32_t wr = client->write_index;
    for(uint32_t i = 0U; i < frames; ++i)
    {
        const uint32_t dst = wr * MULTI_RECORD_WRITER_CHANNELS;
        const uint32_t src = i * MULTI_RECORD_WRITER_CHANNELS;
        g_record_rings[client_id][dst] = lr_interleaved[src];
        g_record_rings[client_id][dst + 1U] = lr_interleaved[src + 1U];
        wr++;
        if(wr >= MULTI_RECORD_WRITER_RING_FRAMES)
            wr = 0U;
    }

    __DMB();
    client->write_index = wr;
    client->frames_received += frames;

    const uint32_t new_pending = ring_pending_frames(client);
    if(new_pending > client->high_watermark)
        client->high_watermark = new_pending;

    return 1U;
}

void multi_record_writer_service(uint32_t byte_budget)
{
    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        multi_record_writer_client_t *client = &g_record_clients[i];
        if(client->state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
            client->state = MULTI_RECORD_WRITER_STATE_DRAINING;
        else if(client->state == MULTI_RECORD_WRITER_STATE_FINALIZING)
            client->state = MULTI_RECORD_WRITER_STATE_TEMP_READY;
    }

    while(byte_budget >= MULTI_RECORD_WRITER_BYTES_PER_FRAME)
    {
        const int32_t picked = pick_most_filled_client();
        if(picked < 0)
            break;

        multi_record_writer_client_t *client = &g_record_clients[(uint32_t)picked];
        const uint32_t pending = ring_pending_frames(client);
        if(pending == 0U)
            break;

        uint32_t frames = byte_budget / MULTI_RECORD_WRITER_BYTES_PER_FRAME;
        if(frames > pending)
            frames = pending;

        drain_client_frames((uint32_t)picked, frames);
        byte_budget -= frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    }

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        multi_record_writer_client_t *client = &g_record_clients[i];
        if((client->state == MULTI_RECORD_WRITER_STATE_DRAINING) &&
           (ring_pending_frames(client) == 0U))
        {
            client->state = MULTI_RECORD_WRITER_STATE_FINALIZING;
            client->error = (client->overflow_count == 0U) ?
                MULTI_RECORD_WRITER_ERROR_NOT_IMPLEMENTED :
                MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
        }
    }
}

uint8_t multi_record_writer_commit(uint8_t client_id, const char *final_path)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(client->state != MULTI_RECORD_WRITER_STATE_TEMP_READY)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    if(copy_path(client->final_path, final_path) == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_PATH);
        return 0U;
    }

    client->error = MULTI_RECORD_WRITER_ERROR_NOT_IMPLEMENTED;
    return 0U;
}

uint8_t multi_record_writer_delete_temp(uint8_t client_id)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->state == MULTI_RECORD_WRITER_STATE_RECORDING) ||
       (client->state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED) ||
       (client->state == MULTI_RECORD_WRITER_STATE_DRAINING) ||
       (client->state == MULTI_RECORD_WRITER_STATE_FINALIZING))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    client->state = MULTI_RECORD_WRITER_STATE_DELETE_PENDING;
    client->error = MULTI_RECORD_WRITER_ERROR_NOT_IMPLEMENTED;
    return 0U;
}

uint8_t multi_record_writer_get_status(uint8_t client_id,
                                       multi_record_writer_status_t *out_status)
{
    if((client_id_valid(client_id) == 0U) || (out_status == 0))
        return 0U;

    const multi_record_writer_client_t *client = &g_record_clients[client_id];
    out_status->state = client->state;
    out_status->error = client->error;
    out_status->frames_pending = ring_pending_frames(client);
    out_status->high_watermark = client->high_watermark;
    out_status->overflow_count = client->overflow_count;
    out_status->dropped_frames = client->dropped_frames;
    out_status->frames_received = client->frames_received;
    out_status->frames_drained = client->frames_drained;
    out_status->simulated_bytes_drained = client->simulated_bytes_drained;
    return 1U;
}

uint8_t multi_record_writer_any_active(void)
{
    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        const multi_record_writer_state_t state = g_record_clients[i].state;
        if((state == MULTI_RECORD_WRITER_STATE_RECORDING) ||
           (state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED) ||
           (state == MULTI_RECORD_WRITER_STATE_DRAINING) ||
           (state == MULTI_RECORD_WRITER_STATE_FINALIZING))
        {
            return 1U;
        }
    }

    return 0U;
}
