#include "Storage/multi_record_writer.h"

#include "Core/rec_live_debug.h"
#include "Sampler/sample_cache.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/sample_capture.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#include <string.h>

#define MRW_RING_USABLE_FRAMES (MULTI_RECORD_WRITER_RING_FRAMES - 1U)
#define MRW_PACK_FRAMES 1024U

#define MRW_FINALIZE_PHASE_BEGIN 0U
#define MRW_FINALIZE_PHASE_PATCH_WAV_HEADER 1U
#define MRW_FINALIZE_PHASE_SYNC 2U
#define MRW_FINALIZE_PHASE_CLOSE 3U
#define MRW_FINALIZE_PHASE_RENAME_WAV 4U
#define MRW_WAV_JUNK_BYTES (MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES - 52U)
#define MRW_MARKER_RING_CAP 16U
#define MRW_WAV_MAX_FRAMES \
    ((UINT32_MAX - (MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES - 8U)) / MULTI_RECORD_WRITER_BYTES_PER_FRAME)

typedef struct
{
    multi_record_writer_marker_t type;
    uint32_t generation;
    uint32_t frames_received;
} multi_record_writer_marker_event_t;

typedef struct
{
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint8_t emergency_valid;
    multi_record_writer_marker_event_t events[MRW_MARKER_RING_CAP];
    multi_record_writer_marker_event_t emergency_event;
} multi_record_writer_marker_ring_t;

typedef struct
{
    volatile uint32_t generation;
    volatile uint8_t session_active;
    volatile uint8_t accepts_blocks;
    volatile uint8_t client_id;
    volatile uint8_t backend;
    volatile uint32_t frame_limit;
    volatile uint32_t frames_received;
} multi_record_writer_audio_projection_t;

typedef struct
{
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t blocks_produced;
    uint32_t blocks_consumed;
    uint32_t markers_produced;
    uint32_t markers_consumed;
    uint32_t marker_critical_failures;
    uint32_t stale_generation;
    uint32_t sessions_abandoned;
    uint32_t max_blocks_drained_per_service;
    uint32_t legacy_writer_calls_from_irq;
} multi_record_writer_diag_t;

typedef struct
{
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    multi_record_writer_backend_t backend;
    multi_record_writer_state_t state;
    multi_record_writer_error_t error;
    multi_record_writer_operation_t last_operation;
    FRESULT last_sd_error;
    uint8_t file_open;
    uint8_t finalize_phase;
    uint8_t raw_slot;
    uint8_t raw_take_valid;
    uint8_t degraded;
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t frames_received;
    uint32_t frames_drained;
    uint32_t frames_written;
    uint32_t bytes_written;
    uint32_t frame_limit;
    uint32_t recorded_frames;
    uint32_t session_generation;
    FIL file;
    char raw_path[MULTI_RECORD_WRITER_PATH_MAX];
    char final_path[MULTI_RECORD_WRITER_PATH_MAX];
} multi_record_writer_client_t;

SDRAM_RECORDER static int32_t
    g_record_rings[MULTI_RECORD_WRITER_MAX_CLIENTS][MULTI_RECORD_WRITER_RING_FRAMES * MULTI_RECORD_WRITER_CHANNELS];
static multi_record_writer_client_t g_record_clients[MULTI_RECORD_WRITER_MAX_CLIENTS];
static multi_record_writer_marker_ring_t g_record_markers[MULTI_RECORD_WRITER_MAX_CLIENTS];
static multi_record_writer_audio_projection_t g_record_audio_projection[MULTI_RECORD_WRITER_MAX_CLIENTS];
static multi_record_writer_diag_t g_record_diag[MULTI_RECORD_WRITER_MAX_CLIENTS];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_pcm24_pack[MRW_PACK_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
_Static_assert(MULTI_RECORD_WRITER_MAX_CLIENTS > 0U, "record writer needs at least one client");
_Static_assert(MULTI_RECORD_WRITER_RING_FRAMES > 1U, "record writer ring needs spare frame");
_Static_assert(MULTI_RECORD_WRITER_SAMPLE_RATE_HZ == 48000U, "record writer target rate is fixed");
_Static_assert(MULTI_RECORD_WRITER_CHANNELS == 2U, "record writer target is stereo");
_Static_assert(MULTI_RECORD_WRITER_BITS_PER_SAMPLE == 24U, "record writer target is PCM24");
_Static_assert(MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES >= 52U,
               "record writer WAV data offset must fit header and JUNK chunk");
_Static_assert(SAMPLE_CAPTURE_RECORD_CLIENT_ID < MULTI_RECORD_WRITER_MAX_CLIENTS,
               "sample capture needs a dedicated multi_record_writer client");
_Static_assert(SAMPLE_CAPTURE_RECORD_CLIENT_ID != 0U,
               "sample capture must not share the Looper RAW writer client");

static uint8_t close_file_if_open(multi_record_writer_client_t *client);

static uint8_t client_id_valid(uint8_t client_id)
{
    return (client_id < MULTI_RECORD_WRITER_MAX_CLIENTS) ? 1U : 0U;
}

static uint8_t writer_in_irq(void)
{
    return (__get_IPSR() != 0U) ? 1U : 0U;
}

static void record_writer_barrier_release(void)
{
    __DMB();
}

static void record_writer_barrier_acquire(void)
{
    __DMB();
}

static void record_writer_publish_producer(void)
{
    record_writer_barrier_release();
}

static void record_writer_acquire_consumer(void)
{
    record_writer_barrier_acquire();
}

static void record_writer_release_consumer(void)
{
    record_writer_barrier_release();
}

static uint32_t next_session_generation(multi_record_writer_client_t *client)
{
    uint32_t generation = client->session_generation + 1U;
    if(generation == 0U)
    {
        generation = 1U;
    }
    client->session_generation = generation;
    return generation;
}

static void publish_audio_projection(uint8_t client_id,
                                     const multi_record_writer_client_t *client,
                                     uint8_t active,
                                     uint8_t accepts_blocks)
{
    multi_record_writer_audio_projection_t *projection = &g_record_audio_projection[client_id];
    projection->client_id = client_id;
    projection->backend = (uint8_t)client->backend;
    projection->frame_limit = client->frame_limit;
    projection->frames_received = 0U;
    projection->generation = client->session_generation;
    projection->accepts_blocks = (accepts_blocks != 0U) ? 1U : 0U;
    projection->session_active = (active != 0U) ? 1U : 0U;
    record_writer_publish_producer();
}

static uint8_t marker_ring_push(uint8_t client_id, multi_record_writer_marker_t marker)
{
    multi_record_writer_marker_ring_t *ring = &g_record_markers[client_id];
    multi_record_writer_audio_projection_t *projection = &g_record_audio_projection[client_id];
    multi_record_writer_diag_t *diag = &g_record_diag[client_id];
    const uint32_t wr = ring->write_index;
    uint32_t next = wr + 1U;
    if(next >= MRW_MARKER_RING_CAP)
    {
        next = 0U;
    }
    if(next == ring->read_index)
    {
        if(ring->emergency_valid != 0U)
        {
            diag->marker_critical_failures++;
            return 0U;
        }
        ring->emergency_event.type = marker;
        ring->emergency_event.generation = projection->generation;
        ring->emergency_event.frames_received = projection->frames_received;
        record_writer_publish_producer();
        ring->emergency_valid = 1U;
        diag->markers_produced++;
        return 1U;
    }

    ring->events[wr].type = marker;
    ring->events[wr].generation = projection->generation;
    ring->events[wr].frames_received = projection->frames_received;
    record_writer_publish_producer();
    ring->write_index = next;
    diag->markers_produced++;
    return 1U;
}

static uint8_t marker_ring_pop(uint8_t client_id, multi_record_writer_marker_event_t *out_event)
{
    multi_record_writer_marker_ring_t *ring = &g_record_markers[client_id];
    const uint32_t rd = ring->read_index;
    if(rd == ring->write_index)
    {
        if(ring->emergency_valid != 0U)
        {
            record_writer_acquire_consumer();
            if(out_event != 0)
            {
                *out_event = ring->emergency_event;
            }
            ring->emergency_valid = 0U;
            record_writer_release_consumer();
            g_record_diag[client_id].markers_consumed++;
            return 1U;
        }
        return 0U;
    }

    record_writer_acquire_consumer();
    if(out_event != 0)
    {
        *out_event = ring->events[rd];
    }
    uint32_t next = rd + 1U;
    if(next >= MRW_MARKER_RING_CAP)
    {
        next = 0U;
    }
    ring->read_index = next;
    record_writer_release_consumer();
    g_record_diag[client_id].markers_consumed++;
    return 1U;
}

static const char *client_debug_path(const multi_record_writer_client_t *client)
{
    if(client == 0)
    {
        return 0;
    }
    return (client->final_path[0] != '\0') ? client->final_path : client->raw_path;
}

static void writer_debug_mark(const multi_record_writer_client_t *client,
                              rec_live_debug_code_t code,
                              uint32_t frames)
{
    if(client == 0)
    {
        return;
    }
    rec_live_debug_mark((uint32_t)code,
                        frames,
                        rec_live_debug_path_hash(client_debug_path(client)),
                        (uint32_t)client->state,
                        0U,
                        (uint32_t)client->error);
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

static void marker_ring_reset(uint8_t client_id)
{
    g_record_markers[client_id].write_index = 0U;
    g_record_markers[client_id].read_index = 0U;
    g_record_markers[client_id].emergency_valid = 0U;
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

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void build_wav_header(uint8_t *header, uint32_t data_bytes)
{
    const uint16_t block_align = MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    const uint32_t byte_rate = MULTI_RECORD_WRITER_SAMPLE_RATE_HZ * (uint32_t)block_align;

    memset(header, 0, MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES);
    memcpy(&header[0], "RIFF", 4U);
    write_le32(&header[4], (MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES - 8U) + data_bytes);
    memcpy(&header[8], "WAVE", 4U);
    memcpy(&header[12], "fmt ", 4U);
    write_le32(&header[16], 16U);
    write_le16(&header[20], 1U);
    write_le16(&header[22], MULTI_RECORD_WRITER_CHANNELS);
    write_le32(&header[24], MULTI_RECORD_WRITER_SAMPLE_RATE_HZ);
    write_le32(&header[28], byte_rate);
    write_le16(&header[32], block_align);
    write_le16(&header[34], MULTI_RECORD_WRITER_BITS_PER_SAMPLE);
    memcpy(&header[36], "JUNK", 4U);
    write_le32(&header[40], MRW_WAV_JUNK_BYTES);
    memcpy(&header[MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES - 8U], "data", 4U);
    write_le32(&header[MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES - 4U], data_bytes);
}

static uint8_t write_wav_header_at_current_pos(multi_record_writer_client_t *client,
                                               uint32_t data_bytes,
                                               multi_record_writer_operation_t operation)
{
    uint8_t header[MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES];
    UINT bw = 0U;

    build_wav_header(header, data_bytes);
    client->last_operation = operation;
    sd_access_trace_begin("multi_record_wav_header");
    const FRESULT fr = f_write(&client->file, header, sizeof(header), &bw);
    sd_access_trace_end("multi_record_wav_header", (int)fr, 0U);
    if((fr != FR_OK) || (bw != sizeof(header)))
    {
        set_sd_error(client, operation, (fr != FR_OK) ? fr : FR_DISK_ERR);
        return 0U;
    }

    return 1U;
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

static void reset_client_take_fields(multi_record_writer_client_t *client)
{
    client->error = MULTI_RECORD_WRITER_ERROR_NONE;
    client->last_sd_error = FR_OK;
    client->high_watermark = 0U;
    client->overflow_count = 0U;
    client->dropped_frames = 0U;
    client->degraded = 0U;
    client->frames_received = 0U;
    client->frames_drained = 0U;
    client->frames_written = 0U;
    client->bytes_written = 0U;
    client->recorded_frames = 0U;
    client->raw_take_valid = 0U;
    client->finalize_phase = MRW_FINALIZE_PHASE_BEGIN;
    ring_reset(client);
}

static void reset_client_diag(uint8_t client_id)
{
    memset(&g_record_diag[client_id], 0, sizeof(g_record_diag[client_id]));
}

static uint8_t start_client_recording(uint8_t client_id, uint8_t reset_audio_ring)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->state != MULTI_RECORD_WRITER_STATE_PREPARED) || (client->file_open == 0U))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        return 0U;
    }

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        if(i == client_id)
        {
            continue;
        }

        const multi_record_writer_state_t other_state = g_record_clients[i].state;
        if((other_state == MULTI_RECORD_WRITER_STATE_RECORDING)
                || (other_state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
                || (other_state == MULTI_RECORD_WRITER_STATE_DRAINING)
                || (other_state == MULTI_RECORD_WRITER_STATE_FINALIZING))
        {
            set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
            return 0U;
        }
    }

    if(reset_audio_ring != 0U)
    {
        ring_reset(client);
    }
    client->state = MULTI_RECORD_WRITER_STATE_RECORDING;
    publish_audio_projection(client_id, client, 1U, 1U);
    return 1U;
}

static uint8_t request_stop_client(uint8_t client_id)
{
    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(client->state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED ||
       client->state == MULTI_RECORD_WRITER_STATE_DRAINING ||
       client->state == MULTI_RECORD_WRITER_STATE_FINALIZING ||
       client->state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
    {
        publish_audio_projection(client_id, client, 0U, 0U);
        return 1U;
    }

    if(client->state != MULTI_RECORD_WRITER_STATE_RECORDING)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        publish_audio_projection(client_id, client, 0U, 0U);
        return 0U;
    }

    client->state = MULTI_RECORD_WRITER_STATE_STOP_REQUESTED;
    publish_audio_projection(client_id, client, 0U, 0U);
    writer_debug_mark(client,
                      REC_LIVE_DEBUG_REC_LIVE_STOP_REQUESTED,
                      client->frames_received);
    return 1U;
}

static void process_marker_event(uint8_t client_id, const multi_record_writer_marker_event_t *event)
{
    if(event == 0)
    {
        return;
    }

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    if(event->generation != client->session_generation)
    {
        g_record_diag[client_id].stale_generation++;
        return;
    }

    switch(event->type)
    {
        case MULTI_RECORD_WRITER_MARKER_START:
            client->last_operation = MULTI_RECORD_WRITER_OP_MARKER_START;
            (void)start_client_recording(client_id, 0U);
            break;
        case MULTI_RECORD_WRITER_MARKER_STOP:
            client->last_operation = MULTI_RECORD_WRITER_OP_MARKER_STOP;
            client->frames_received = event->frames_received;
            client->overflow_count = g_record_diag[client_id].overflow_count;
            client->dropped_frames = g_record_diag[client_id].dropped_frames;
            client->high_watermark = g_record_diag[client_id].high_watermark;
            if(client->overflow_count != 0U)
            {
                client->degraded = 1U;
                client->error = MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
            }
            (void)request_stop_client(client_id);
            break;
        case MULTI_RECORD_WRITER_MARKER_ABORT:
            client->last_operation = MULTI_RECORD_WRITER_OP_MARKER_ABORT;
            g_record_diag[client_id].sessions_abandoned++;
            client->frames_received = event->frames_received;
            client->overflow_count = g_record_diag[client_id].overflow_count;
            client->dropped_frames = g_record_diag[client_id].dropped_frames;
            client->high_watermark = g_record_diag[client_id].high_watermark;
            client->degraded = 1U;
            client->error = MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            (void)close_file_if_open(client);
            publish_audio_projection(client_id, client, 0U, 0U);
            break;
        case MULTI_RECORD_WRITER_MARKER_END:
            client->last_operation = MULTI_RECORD_WRITER_OP_MARKER_END;
            break;
        default:
            break;
    }
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

    if((client->file_open == 0U)
            && (client->finalize_phase != MRW_FINALIZE_PHASE_RENAME_WAV))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
        return 0U;
    }

    client->last_operation = MULTI_RECORD_WRITER_OP_FINALIZE;

    if(client->finalize_phase == MRW_FINALIZE_PHASE_BEGIN)
    {
        client->finalize_phase =
            (client->backend == MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV) ?
                MRW_FINALIZE_PHASE_PATCH_WAV_HEADER : MRW_FINALIZE_PHASE_SYNC;
    }

    if(client->finalize_phase == MRW_FINALIZE_PHASE_PATCH_WAV_HEADER)
    {
        sd_access_trace_begin("multi_record_wav_seek0");
        FRESULT fr = f_lseek(&client->file, 0U);
        sd_access_trace_end("multi_record_wav_seek0", (int)fr, 0U);
        if(fr != FR_OK)
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_PATCH_WAV_HEADER, fr);
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            (void)close_file_if_open(client);
            return 0U;
        }

        if(client->frames_written > MRW_WAV_MAX_FRAMES)
        {
            set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_STATE);
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            (void)close_file_if_open(client);
            return 0U;
        }

        if(write_wav_header_at_current_pos(client,
                                           client->frames_written * MULTI_RECORD_WRITER_BYTES_PER_FRAME,
                                           MULTI_RECORD_WRITER_OP_PATCH_WAV_HEADER) == 0U)
        {
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            (void)close_file_if_open(client);
            return 0U;
        }

        client->finalize_phase = MRW_FINALIZE_PHASE_SYNC;
        return 1U;
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
        client->raw_take_valid =
            ((client->backend == MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW)
             && (client->recorded_frames != 0U)) ? 1U : 0U;
        client->last_sd_error = FR_OK;
        client->error = (client->overflow_count == 0U) ?
            MULTI_RECORD_WRITER_ERROR_NONE :
            MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW;
        client->degraded = (client->overflow_count == 0U) ? 0U : 1U;
        client->finalize_phase =
            (client->backend == MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV) ?
                MRW_FINALIZE_PHASE_RENAME_WAV : MRW_FINALIZE_PHASE_BEGIN;
        if(client->backend != MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV)
        {
            client->state = MULTI_RECORD_WRITER_STATE_TAKE_READY;
            writer_debug_mark(client,
                              REC_LIVE_DEBUG_WRITER_FINAL_READY,
                              client->recorded_frames);
        }
        return 1U;
    }

    if(client->finalize_phase == MRW_FINALIZE_PHASE_RENAME_WAV)
    {
        if(strncmp(client->raw_path, client->final_path, MULTI_RECORD_WRITER_PATH_MAX) == 0)
        {
            client->finalize_phase = MRW_FINALIZE_PHASE_BEGIN;
            client->state = MULTI_RECORD_WRITER_STATE_TAKE_READY;
            writer_debug_mark(client,
                              REC_LIVE_DEBUG_WRITER_FINAL_READY,
                              client->recorded_frames);
            return 1U;
        }

        client->last_operation = MULTI_RECORD_WRITER_OP_RENAME_WAV;
        sd_access_trace_begin("multi_record_wav_rename");
        const FRESULT fr = f_rename(client->raw_path, client->final_path);
        sd_access_trace_end("multi_record_wav_rename", (int)fr, 0U);
        if(fr != FR_OK)
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_RENAME_WAV, fr);
            client->state = MULTI_RECORD_WRITER_STATE_FAILED;
            return 0U;
        }

        client->finalize_phase = MRW_FINALIZE_PHASE_BEGIN;
        client->state = MULTI_RECORD_WRITER_STATE_TAKE_READY;
        writer_debug_mark(client,
                          REC_LIVE_DEBUG_WRITER_FINAL_READY,
                          client->recorded_frames);
    }

    return 1U;
}

void multi_record_writer_init(void)
{
    memset(g_record_clients, 0, sizeof(g_record_clients));
    memset(g_record_rings, 0, sizeof(g_record_rings));
    memset(g_record_markers, 0, sizeof(g_record_markers));
    memset(g_record_audio_projection, 0, sizeof(g_record_audio_projection));
    memset(g_record_diag, 0, sizeof(g_record_diag));
    memset(g_pcm24_pack, 0, sizeof(g_pcm24_pack));

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        g_record_clients[i].state = MULTI_RECORD_WRITER_STATE_IDLE;
        g_record_clients[i].backend = MULTI_RECORD_WRITER_BACKEND_NONE;
        g_record_clients[i].error = MULTI_RECORD_WRITER_ERROR_NONE;
        g_record_clients[i].last_operation = MULTI_RECORD_WRITER_OP_NONE;
        g_record_clients[i].last_sd_error = FR_OK;
        g_record_clients[i].raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
        g_record_audio_projection[i].client_id = (uint8_t)i;
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
        client->backend = MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW;
        client->last_operation = MULTI_RECORD_WRITER_OP_PREPARE_RAW;
        client->last_sd_error = FR_OK;
        const uint32_t raw_capacity = looper_storage_raw_get_capacity_frames();
        client->frame_limit = ((expected_frames != 0U) && (expected_frames < raw_capacity)) ?
            expected_frames : raw_capacity;
        client->raw_slot = raw_slot;
        client->final_path[0] = '\0';
        (void)next_session_generation(client);
        reset_client_diag(client_id);
        marker_ring_reset(client_id);
        reset_client_take_fields(client);
        client->state = MULTI_RECORD_WRITER_STATE_PREPARED;
        publish_audio_projection(client_id, client, 0U, 1U);
    }
    else
    {
        (void)close_file_if_open(client);
        client->state = MULTI_RECORD_WRITER_STATE_FAILED;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

uint8_t multi_record_writer_prepare_sample_wav(uint8_t client_id,
                                               const char *temp_path,
                                               const char *final_path,
                                               uint32_t frame_limit)
{
    if((client_id_valid(client_id) == 0U)
            || (temp_path == 0)
            || (temp_path[0] == '\0')
            || (final_path == 0)
            || (final_path[0] == '\0'))
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

    if((copy_path(client->raw_path, temp_path) == 0U)
            || (copy_path(client->final_path, final_path) == 0U))
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_INVALID_PATH);
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        set_error(client, MULTI_RECORD_WRITER_ERROR_SD_BUSY);
        client->last_operation = MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV;
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_unlink(client->raw_path);
        sd_access_trace_begin("multi_record_open_wav_tmp");
        FRESULT fr = f_open(&client->file, client->raw_path, FA_CREATE_NEW | FA_WRITE | FA_READ);
        sd_access_trace_end("multi_record_open_wav_tmp", (int)fr, 0U);
        if(fr == FR_OK)
        {
            client->file_open = 1U;
            ok = write_wav_header_at_current_pos(client,
                                                 0U,
                                                 MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV);
        }
        else
        {
            set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV, fr);
        }
    }
    else
    {
        set_sd_error(client, MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV, FR_NOT_READY);
    }

    if(ok != 0U)
    {
        client->backend = MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV;
        client->last_operation = MULTI_RECORD_WRITER_OP_PREPARE_SAMPLE_WAV;
        client->last_sd_error = FR_OK;
        client->frame_limit =
            ((frame_limit != 0U) && (frame_limit < MRW_WAV_MAX_FRAMES)) ?
                frame_limit : MRW_WAV_MAX_FRAMES;
        client->raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
        (void)next_session_generation(client);
        reset_client_diag(client_id);
        marker_ring_reset(client_id);
        reset_client_take_fields(client);
        client->state = MULTI_RECORD_WRITER_STATE_PREPARED;
        publish_audio_projection(client_id, client, 0U, 1U);
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

    if(writer_in_irq() != 0U)
    {
        g_record_diag[client_id].legacy_writer_calls_from_irq++;
        multi_record_writer_audio_projection_t *projection = &g_record_audio_projection[client_id];
        if((projection->accepts_blocks == 0U) || (projection->generation == 0U))
        {
            g_record_diag[client_id].marker_critical_failures++;
            return 0U;
        }

        if(marker_ring_push(client_id, MULTI_RECORD_WRITER_MARKER_START) == 0U)
        {
            return 0U;
        }
        projection->session_active = 1U;
        projection->accepts_blocks = 1U;
        projection->frames_received = 0U;
        record_writer_publish_producer();
        return 1U;
    }

    return start_client_recording(client_id, 1U);
}

uint8_t multi_record_writer_request_stop(uint8_t client_id)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    if(writer_in_irq() != 0U)
    {
        g_record_diag[client_id].legacy_writer_calls_from_irq++;
        if(marker_ring_push(client_id, MULTI_RECORD_WRITER_MARKER_STOP) == 0U)
        {
            return 0U;
        }
        g_record_audio_projection[client_id].accepts_blocks = 0U;
        g_record_audio_projection[client_id].session_active = 0U;
        record_writer_publish_producer();
        return 1U;
    }

    return request_stop_client(client_id);
}

uint8_t multi_record_writer_push_audio_block_from_irq(uint8_t client_id,
                                                      const int32_t *lr_interleaved,
                                                      uint32_t frames)
{
    if((client_id_valid(client_id) == 0U) || (lr_interleaved == 0) || (frames == 0U))
        return 0U;

    multi_record_writer_audio_projection_t *projection = &g_record_audio_projection[client_id];
    multi_record_writer_diag_t *diag = &g_record_diag[client_id];
    if((projection->session_active == 0U) || (projection->accepts_blocks == 0U)
            || (projection->generation == 0U))
        return 0U;

    uint32_t frames_to_store = frames;
    const uint32_t generation = projection->generation;
    const uint32_t frame_limit = projection->frame_limit;
    const uint32_t frames_received = projection->frames_received;
    if(frame_limit != 0U)
    {
        if(frames_received >= frame_limit)
            return 1U;

        const uint32_t remaining = frame_limit - frames_received;
        if(frames_to_store > remaining)
            frames_to_store = remaining;
    }

    multi_record_writer_client_t *client = &g_record_clients[client_id];
    const uint32_t pending = ring_pending_frames(client);
    const uint32_t available_frames = MRW_RING_USABLE_FRAMES - pending;
    if(frames_to_store > available_frames)
    {
        diag->marker_critical_failures += (projection->backend == MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW) ? 1U : 0U;
        diag->overflow_count++;
        diag->dropped_frames += frames_to_store;
        projection->accepts_blocks = 0U;
        if(projection->backend == MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW)
        {
            projection->session_active = 0U;
            (void)marker_ring_push(client_id, MULTI_RECORD_WRITER_MARKER_ABORT);
        }
        else
        {
            (void)marker_ring_push(client_id, MULTI_RECORD_WRITER_MARKER_STOP);
        }
        record_writer_publish_producer();
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

    record_writer_publish_producer();
    client->write_index = wr;
    projection->frames_received = frames_received + frames_to_store;
    if(generation != client->session_generation)
    {
        diag->stale_generation++;
    }
    diag->blocks_produced++;

    const uint32_t new_pending = ring_pending_frames(client);
    if(new_pending > diag->high_watermark)
        diag->high_watermark = new_pending;

    return 1U;
}

void multi_record_writer_service(uint32_t byte_budget)
{
    uint8_t finalized_one = 0U;
    uint32_t blocks_drained_this_service[MULTI_RECORD_WRITER_MAX_CLIENTS] = {0U};

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        multi_record_writer_marker_event_t event;
        while(marker_ring_pop((uint8_t)i, &event) != 0U)
        {
            process_marker_event((uint8_t)i, &event);
        }
    }

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        if(g_record_clients[i].state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
        {
            g_record_clients[i].state = MULTI_RECORD_WRITER_STATE_DRAINING;
            writer_debug_mark(&g_record_clients[i],
                              REC_LIVE_DEBUG_WRITER_DRAINING,
                              g_record_clients[i].frames_written);
        }
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
            const uint32_t before = g_record_clients[(uint32_t)picked].frames_drained;
            (void)write_audio_chunk((uint32_t)picked, &byte_budget);
            if(g_record_clients[(uint32_t)picked].frames_drained != before)
            {
                g_record_diag[(uint32_t)picked].blocks_consumed++;
                blocks_drained_this_service[(uint32_t)picked]++;
            }
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
                writer_debug_mark(client,
                                  REC_LIVE_DEBUG_WRITER_FINALIZING,
                                  client->frames_written);
            }

            if((client->state == MULTI_RECORD_WRITER_STATE_FINALIZING) && (finalized_one == 0U))
            {
                (void)finalize_client_step(i);
                finalized_one = 1U;
            }
        }
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        if(blocks_drained_this_service[i] > g_record_diag[i].max_blocks_drained_per_service)
        {
            g_record_diag[i].max_blocks_drained_per_service = blocks_drained_this_service[i];
        }
    }
}

uint8_t multi_record_writer_get_status(uint8_t client_id,
                                       multi_record_writer_status_t *out_status)
{
    if((client_id_valid(client_id) == 0U) || (out_status == 0))
        return 0U;

    const multi_record_writer_client_t *client = &g_record_clients[client_id];
    const multi_record_writer_audio_projection_t *projection = &g_record_audio_projection[client_id];
    const multi_record_writer_diag_t *diag = &g_record_diag[client_id];
    out_status->state = client->state;
    out_status->backend = client->backend;
    out_status->error = (diag->overflow_count != 0U)
        ? MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW
        : client->error;
    out_status->last_operation = client->last_operation;
    out_status->last_sd_error = (uint32_t)client->last_sd_error;
    out_status->degraded = ((client->degraded != 0U) || (diag->overflow_count != 0U)) ? 1U : 0U;
    out_status->frames_pending = ring_pending_frames(client);
    out_status->high_watermark = (diag->high_watermark > client->high_watermark)
        ? diag->high_watermark
        : client->high_watermark;
    out_status->overflow_count = (diag->overflow_count > client->overflow_count)
        ? diag->overflow_count
        : client->overflow_count;
    out_status->dropped_frames = (diag->dropped_frames > client->dropped_frames)
        ? diag->dropped_frames
        : client->dropped_frames;
    out_status->frames_received = (projection->frames_received > client->frames_received)
        ? projection->frames_received
        : client->frames_received;
    out_status->frames_drained = client->frames_drained;
    out_status->frames_written = client->frames_written;
    out_status->bytes_written = client->bytes_written;
    out_status->session_generation = client->session_generation;
    out_status->blocks_produced = diag->blocks_produced;
    out_status->blocks_consumed = diag->blocks_consumed;
    out_status->markers_produced = diag->markers_produced;
    out_status->markers_consumed = diag->markers_consumed;
    out_status->marker_critical_failures = diag->marker_critical_failures;
    out_status->stale_generation = diag->stale_generation;
    out_status->sessions_abandoned = diag->sessions_abandoned;
    out_status->max_blocks_drained_per_service = diag->max_blocks_drained_per_service;
    out_status->legacy_writer_calls_from_irq = diag->legacy_writer_calls_from_irq;
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

uint8_t multi_record_writer_get_last_sample_wav_take(uint8_t client_id,
                                                     const char **out_path,
                                                     uint32_t *out_recorded_frames)
{
    if(client_id_valid(client_id) == 0U)
        return 0U;

    const multi_record_writer_client_t *client = &g_record_clients[client_id];
    if((client->backend != MULTI_RECORD_WRITER_BACKEND_SAMPLE_WAV)
            || (client->state != MULTI_RECORD_WRITER_STATE_TAKE_READY)
            || (client->recorded_frames == 0U)
            || (client->final_path[0] == '\0'))
    {
        return 0U;
    }

    if(out_path != 0)
    {
        *out_path = client->final_path;
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

uint8_t multi_record_writer_any_active_backend(multi_record_writer_backend_t backend)
{
    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_MAX_CLIENTS; ++i)
    {
        const multi_record_writer_client_t *const client = &g_record_clients[i];
        const multi_record_writer_state_t state = client->state;
        if((client->backend == backend)
                && ((state == MULTI_RECORD_WRITER_STATE_RECORDING)
                    || (state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
                    || (state == MULTI_RECORD_WRITER_STATE_DRAINING)
                    || (state == MULTI_RECORD_WRITER_STATE_FINALIZING)))
        {
            return 1U;
        }
    }

    return 0U;
}
