#include "Storage/multi_record_writer.h"

#include "Sampler/sample_cache.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#include <string.h>

#define MRW_RING_USABLE_FRAMES (MULTI_RECORD_WRITER_RING_FRAMES - 1U)
#define MRW_PACK_FRAMES 1024U

#define MRW_FINALIZE_PHASE_BEGIN 0U
#define MRW_FINALIZE_PHASE_SYNC 1U
#define MRW_FINALIZE_PHASE_CLOSE 2U

typedef struct
{
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    multi_record_writer_state_t state;
    multi_record_writer_error_t error;
    multi_record_writer_operation_t last_operation;
    FRESULT last_sd_error;
    uint8_t file_open;
    uint8_t finalize_phase;
    uint8_t raw_slot;
    uint8_t raw_take_valid;
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t frames_received;
    uint32_t frames_drained;
    uint32_t frames_written;
    uint32_t bytes_written;
    uint32_t frame_limit;
    uint32_t recorded_frames;
    FIL file;
    char raw_path[MULTI_RECORD_WRITER_PATH_MAX];
} multi_record_writer_client_t;

SDRAM_RECORDER static int32_t
    g_record_rings[MULTI_RECORD_WRITER_MAX_CLIENTS][MULTI_RECORD_WRITER_RING_FRAMES * MULTI_RECORD_WRITER_CHANNELS];
static multi_record_writer_client_t g_record_clients[MULTI_RECORD_WRITER_MAX_CLIENTS];
static ALIGN32 uint8_t g_pcm24_pack[MRW_PACK_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];

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

static void set_sd_error(multi_record_writer_client_t *client,
                         multi_record_writer_operation_t operation,
                         FRESULT sd_error)
{
    client->last_operation = operation;
    client->last_sd_error = sd_error;
    client->error = (sd_error == FR_OK) ? MULTI_RECORD_WRITER_ERROR_NONE :
        MULTI_RECORD_WRITER_ERROR_SD_IO;
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

static uint8_t has_service_work(void)
{
    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        const multi_record_writer_client_t *client = &g_record_clients[i];
        if((client->state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED) ||
           (client->state == MULTI_RECORD_WRITER_STATE_DRAINING) ||
           (client->state == MULTI_RECORD_WRITER_STATE_FINALIZING) ||
           ((client->state == MULTI_RECORD_WRITER_STATE_RECORDING) &&
            (ring_pending_frames(client) != 0U)))
        {
            return 1U;
        }
    }

    return 0U;
}

static void pack_pcm24_from_ring(uint32_t client_id, uint32_t frames)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    uint32_t rd = client->read_index;
    uint32_t out = 0U;

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const uint32_t src = rd * MULTI_RECORD_WRITER_CHANNELS;
        for(uint32_t ch = 0U; ch < MULTI_RECORD_WRITER_CHANNELS; ++ch)
        {
            const int32_t v = g_record_rings[client_id][src + ch];
            g_pcm24_pack[out++] = (uint8_t)(v & 0xFF);
            g_pcm24_pack[out++] = (uint8_t)((v >> 8) & 0xFF);
            g_pcm24_pack[out++] = (uint8_t)((v >> 16) & 0xFF);
        }

        rd++;
        if(rd >= MULTI_RECORD_WRITER_RING_FRAMES)
            rd = 0U;
    }
}

static void drain_client_frames(uint32_t client_id, uint32_t frames)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    client->read_index = (client->read_index + frames) % MULTI_RECORD_WRITER_RING_FRAMES;
    client->frames_drained += frames;
    client->frames_written += frames;
    client->bytes_written += frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
}

static uint8_t close_file_if_open(multi_record_writer_client_t *client)
{
    if(client->file_open == 0U)
        return 1U;

    const FRESULT fr = f_close(&client->file);
    client->file_open = 0U;
    if(fr != FR_OK)
    {
        set_sd_error(client, client->last_operation, fr);
        return 0U;
    }

    return 1U;
}

static uint8_t write_audio_chunk(uint32_t client_id, uint32_t *byte_budget)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    const uint32_t pending = ring_pending_frames(client);
    if((pending == 0U) || (byte_budget == 0) || (*byte_budget < MULTI_RECORD_WRITER_BYTES_PER_FRAME))
        return 0U;

    if(client->file_open == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
        return 0U;
    }

    uint32_t frames = *byte_budget / MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    if(frames > MRW_PACK_FRAMES)
        frames = MRW_PACK_FRAMES;
    if(frames > pending)
        frames = pending;

    const uint32_t bytes = frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    pack_pcm24_from_ring(client_id, frames);

    UINT bw = 0U;
    client->last_operation = MULTI_RECORD_WRITER_OP_WRITE_AUDIO;
    sd_access_trace_begin("multi_record_raw_write");
    const FRESULT fr = f_write(&client->file, g_pcm24_pack, bytes, &bw);
    sd_access_trace_end("multi_record_raw_write", (int)fr, 0U);
    if((fr != FR_OK) || (bw != bytes))
    {
        set_sd_error(client, MULTI_RECORD_WRITER_OP_WRITE_AUDIO,
                     (fr != FR_OK) ? fr : FR_DISK_ERR);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
        (void)close_file_if_open(client);
        return 0U;
    }

    drain_client_frames(client_id, frames);
    *byte_budget -= bytes;
    return 1U;
}

static uint8_t finalize_client_step(uint32_t client_id)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];

    if(client->file_open == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
        return 0U;
    }

    client->last_operation = MULTI_RECORD_WRITER_OP_FINALIZE;

    if(client->finalize_phase == MRW_FINALIZE_PHASE_BEGIN)
    {
        client->finalize_phase = MRW_FINALIZE_PHASE_SYNC;
    }

    if(client->finalize_phase == MRW_FINALIZE_PHASE_SYNC)
    {
        sd_access_trace_begin("multi_record_raw_sync");
        const FRESULT fr = f_sync(&client->file);
        sd_access_trace_end("multi_record_raw_sync", (int)fr, 0U);
        if(fr != FR_OK)
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_FINALIZE, fr);
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            (void)close_file_if_open(client);
            return 0U;
        }
        client->finalize_phase = MRW_FINALIZE_PHASE_CLOSE;
        return 1U;
    }

    if(client->finalize_phase == MRW_FINALIZE_PHASE_CLOSE)
    {
        sd_access_trace_begin("multi_record_raw_close");
        const FRESULT fr = f_close(&client->file);
        sd_access_trace_end("multi_record_raw_close", (int)fr, 0U);
        client->file_open = 0U;
        if(fr != FR_OK)
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_FINALIZE, fr);
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            return 0U;
        }

        client->recorded_frames = client->frames_written;
        client->raw_take_valid = (client->recorded_frames != 0U) ? 1U : 0U;
        client->last_sd_error = FR_OK;
        client->error = (client->overflow_count == 0U) ?
            MULTI_RECORD_WRITER_ERROR_NONE :
            MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
        client->finalize_phase = MRW_FINALIZE_PHASE_BEGIN;
        client->state = MULTI_RECORD_WRITER_STATE_TAKE_READY;
    }

    return 1U;
}

void multi_record_writer_init(void)
{
    memset(g_record_clients, 0, sizeof(g_record_clients));
    memset(g_record_rings, 0, sizeof(g_record_rings));
    memset(g_pcm24_pack, 0, sizeof(g_pcm24_pack));

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        g_record_clients[i].state = MULTI_RECORD_WRITER_STATE_IDLE;
        g_record_clients[i].error = MULTI_RECORD_WRITER_ERROR_NONE;
        g_record_clients[i].last_operation = MULTI_RECORD_WRITER_OP_NONE;
        g_record_clients[i].last_sd_error = FR_OK;
        g_record_clients[i].raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
    }
}

uint8_t multi_record_writer_prepare_raw(uint8_t client_id,
                                        uint8_t raw_slot,
                                        const char *raw_path,
                                        uint32_t expected_frames)
{
    if((client_id_valid(client_id) == 0U)
            || (raw_slot >= LOOPER_STORAGE_RAW_SLOT_COUNT)
            || (raw_path == 0)
            || (raw_path[0] == '\0'))
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->state != MULTI_RECORD_WRITER_STATE_IDLE) &&
       (client->state != MULTI_RECORD_WRITER_STATE_PREPARED) &&
       (client->state != MULTI_RECORD_WRITER_STATE_TAKE_READY) &&
       (client->state != MULTI_RECORD_WRITER_STATE_FAILED))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    if(close_file_if_open(client) == 0U)
    {
        return 0U;
    }

    if(copy_path(client->raw_path, raw_path) == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_PATH);
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_SD_BUSY);
        client->last_operation = MULTI_RECORD_WRITER_OP_PREPARE_RAW;
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        FILINFO info;
        FRESULT fr = f_stat(client->raw_path, &info);
        if((fr != FR_OK)
                || ((uint64_t)info.fsize != (uint64_t)LOOPER_STORAGE_RAW_RESERVOIR_BYTES))
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_RAW,
                         (fr != FR_OK) ? fr : FR_DISK_ERR);
            goto raw_prepare_done;
        }

        sd_access_trace_begin("multi_record_open_raw");
        fr = f_open(&client->file, client->raw_path, FA_WRITE | FA_OPEN_EXISTING);
        sd_access_trace_end("multi_record_open_raw", (int)fr, 0U);
        if(fr == FR_OK)
        {
            client->file_open = 1U;
            sd_access_trace_begin("multi_record_raw_seek0");
            fr = f_lseek(&client->file, 0U);
            sd_access_trace_end("multi_record_raw_seek0", (int)fr, 0U);
            if(fr == FR_OK)
            {
                ok = 1U;
            }
            else
            {
                set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_RAW, fr);
            }
        }
        else
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_RAW, fr);
        }
    }
    else
    {
        set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_RAW, FR_NOT_READY);
    }

raw_prepare_done:
    if(ok != 0U)
    {
        client->error = MULTI_RECORD_WRITER_ERROR_NONE;
        client->last_operation = MULTI_RECORD_WRITER_OP_PREPARE_RAW;
        client->last_sd_error = FR_OK;
        client->high_watermark = 0U;
        client->overflow_count = 0U;
        client->dropped_frames = 0U;
        client->frames_received = 0U;
        client->frames_drained = 0U;
        client->frames_written = 0U;
        client->bytes_written = 0U;
        const uint32_t raw_capacity = looper_storage_raw_get_capacity_frames();
        client->frame_limit = ((expected_frames != 0U) && (expected_frames < raw_capacity)) ?
            expected_frames : raw_capacity;
        client->recorded_frames = 0U;
        client->raw_slot = raw_slot;
        client->raw_take_valid = 0U;
        client->finalize_phase = MRW_FINALIZE_PHASE_BEGIN;
        ring_reset(client);
        client->state = MULTI_RECORD_WRITER_STATE_PREPARED;
    }
    else
    {
        (void)close_file_if_open(client);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

uint8_t multi_record_writer_start(uint8_t client_id)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->state != MULTI_RECORD_WRITER_STATE_PREPARED) || (client->file_open == 0U))
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
    if(client->state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED ||
       client->state == MULTI_RECORD_WRITER_STATE_DRAINING ||
       client->state == MULTI_RECORD_WRITER_STATE_FINALIZING ||
       client->state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
    {
        return 1U;
    }

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

    uint32_t frames_to_store = frames;
    if(client->frame_limit != 0U)
    {
        if(client->frames_received >= client->frame_limit)
            return 1U;

        const uint32_t remaining = client->frame_limit - client->frames_received;
        if(frames_to_store > remaining)
            frames_to_store = remaining;
    }

    const uint32_t pending = ring_pending_frames(client);
    const uint32_t available_frames = MRW_RING_USABLE_FRAMES - pending;
    if(frames_to_store > available_frames)
    {
        client->overflow_count++;
        client->dropped_frames += frames_to_store;
        client->error = MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
        return 0U;
    }

    uint32_t wr = client->write_index;
    for(uint32_t i = 0U; i < frames_to_store; ++i)
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
    client->frames_received += frames_to_store;

    const uint32_t new_pending = ring_pending_frames(client);
    if(new_pending > client->high_watermark)
        client->high_watermark = new_pending;

    return 1U;
}

void multi_record_writer_service(uint32_t byte_budget)
{
    uint8_t finalized_one = 0U;

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        if(g_record_clients[i].state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
            g_record_clients[i].state = MULTI_RECORD_WRITER_STATE_DRAINING;
    }

    if((byte_budget < MULTI_RECORD_WRITER_BYTES_PER_FRAME) && (has_service_work() == 0U))
        return;

    if(sample_cache_has_pending_sd_work() != 0U)
        return;

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
        return;

    if(byte_budget >= MULTI_RECORD_WRITER_BYTES_PER_FRAME)
    {
        const int32_t picked = pick_most_filled_client();
        if(picked >= 0)
        {
            (void)write_audio_chunk((uint32_t)picked, &byte_budget);
        }
    }

    if(sample_cache_has_pending_sd_work() == 0U)
    {
        for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
        {
            multi_record_writer_client_t *client = &g_record_clients[i];
            if((client->state == MULTI_RECORD_WRITER_STATE_DRAINING) &&
               (ring_pending_frames(client) == 0U))
            {
                client->state = MULTI_RECORD_WRITER_STATE_FINALIZING;
            }

            if((client->state == MULTI_RECORD_WRITER_STATE_FINALIZING) && (finalized_one == 0U))
            {
                (void)finalize_client_step(i);
                finalized_one = 1U;
            }
        }
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
}

uint8_t multi_record_writer_get_status(uint8_t client_id,
                                       multi_record_writer_status_t *out_status)
{
    if((client_id_valid(client_id) == 0U) || (out_status == 0))
        return 0U;

    const multi_record_writer_client_t *client = &g_record_clients[client_id];
    out_status->state = client->state;
    out_status->error = client->error;
    out_status->last_operation = client->last_operation;
    out_status->last_sd_error = (uint32_t)client->last_sd_error;
    out_status->frames_pending = ring_pending_frames(client);
    out_status->high_watermark = client->high_watermark;
    out_status->overflow_count = client->overflow_count;
    out_status->dropped_frames = client->dropped_frames;
    out_status->frames_received = client->frames_received;
    out_status->frames_drained = client->frames_drained;
    out_status->frames_written = client->frames_written;
    out_status->bytes_written = client->bytes_written;
    out_status->raw_slot = client->raw_slot;
    return 1U;
}

uint8_t multi_record_writer_get_last_raw_take(uint8_t client_id,
                                              uint8_t *out_slot,
                                              const char **out_path,
                                              uint32_t *out_recorded_frames)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    const multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->raw_take_valid == 0U) || (client->raw_path[0] == '\0'))
    {
        return 0U;
    }

    if(out_slot != 0)
    {
        *out_slot = client->raw_slot;
    }
    if(out_path != 0)
    {
        *out_path = client->raw_path;
    }
    if(out_recorded_frames != 0)
    {
        *out_recorded_frames = client->recorded_frames;
    }
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
