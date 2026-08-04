#include "Core/prism_debug_boot.h"

#if BRICK6_PRISM_DEBUG_BOOT

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Audio/mixer.h"
#include "Audio/fx_pool.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Param/param_store.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "buttons.h"
#include "drv_display.h"
#include "encoders.h"
#include "ff.h"
#include "font.h"
#include "stm32h7xx_hal.h"

#define PRISM_DEBUG_CAPTURE_CLIENT SD_ACCESS_CLIENT_RECORDER
#define PRISM_DEBUG_WAV_HEADER 44U
#define PRISM_DEBUG_WAV_CHUNK 1024U
#define PRISM_DEBUG_PATH_MAX 320U
#define PRISM_DEBUG_NOTE_BASE 48U

typedef enum
{
    PRISM_DEBUG_STATE_RUNNING = 0,
    PRISM_DEBUG_STATE_FROZEN,
    PRISM_DEBUG_STATE_SAVING,
    PRISM_DEBUG_STATE_DONE,
    PRISM_DEBUG_STATE_ERROR
} prism_debug_state_t;

typedef struct
{
    volatile prism_debug_probe_t requested_probe;
    volatile prism_debug_probe_t active_probe;
    volatile uint8_t freeze_requested;
    volatile uint8_t verdict;
    volatile uint8_t frozen;
    volatile uint8_t block_open;
    volatile uint8_t block_mask;
    volatile uint8_t render_track;
    uint32_t block_frames;
    uint32_t write_frame;
    uint32_t valid_frames;
    uint32_t capture_index;
    uint32_t boot_index;
    prism_debug_state_t state;
    uint8_t save_file;
    uint8_t save_phase;
    uint32_t save_frame;
    uint32_t save_oldest;
    uint32_t save_frames;
    uint32_t save_byte_budget;
    char folder[PRISM_DEBUG_PATH_MAX];
    char wav_path[PRISM_DEBUG_PATH_MAX];
    char info_path[PRISM_DEBUG_PATH_MAX];
    FIL file;
    uint8_t file_open;
    uint8_t io[PRISM_DEBUG_WAV_CHUNK * 2U];
} prism_debug_state_data_t;

STORAGE_STATE_SDRAM static prism_debug_state_data_t g_debug;
AUDIO_HISTORY_SDRAM static int16_t g_debug_ring[PRISM_DEBUG_TRACK_COUNT][PRISM_DEBUG_RING_FRAMES];
AUDIO_HOT static float g_debug_block[PRISM_DEBUG_TRACK_COUNT][AUDIO_BLOCK_SIZE];

static const uint8_t g_debug_notes[PRISM_DEBUG_TRACK_COUNT] = {
    PRISM_DEBUG_NOTE_BASE, 52U, 55U, 59U, 62U, 67U, 71U, 74U
};

static uint8_t debug_track_valid(uint8_t track)
{
    return (track < PRISM_DEBUG_TRACK_COUNT) ? 1U : 0U;
}

static void debug_join_path(char *dst, size_t capacity, const char *folder, const char *leaf)
{
    const size_t folder_length = strlen(folder);
    const size_t leaf_length = strlen(leaf);
    size_t copy_folder = folder_length;
    size_t copy_leaf = leaf_length;

    if (capacity == 0U) return;
    if (copy_folder > capacity - 1U) copy_folder = capacity - 1U;
    if (copy_leaf > capacity - copy_folder - 1U) copy_leaf = capacity - copy_folder - 1U;
    memcpy(dst, folder, copy_folder);
    if (copy_folder < capacity - 1U)
    {
        dst[copy_folder++] = '/';
        if (copy_leaf > capacity - copy_folder - 1U) copy_leaf = capacity - copy_folder - 1U;
        memcpy(&dst[copy_folder], leaf, copy_leaf);
        copy_folder += copy_leaf;
    }
    dst[copy_folder] = '\0';
}

static float debug_clampf(float value)
{
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static int16_t debug_pcm16(float value)
{
    const float clamped = debug_clampf(value);
    const float scaled = (clamped >= 0.0f) ? (clamped * 32767.0f) : (clamped * 32768.0f);
    return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void debug_copy_float(uint8_t track, const float *src, uint32_t offset, uint32_t frames)
{
    if ((debug_track_valid(track) == 0U) || (src == NULL) || (offset >= AUDIO_BLOCK_SIZE))
        return;
    if (frames > (AUDIO_BLOCK_SIZE - offset)) frames = AUDIO_BLOCK_SIZE - offset;
    memcpy(&g_debug_block[track][offset], src, frames * sizeof(float));
    g_debug.block_mask |= (uint8_t)(1U << track);
}

static void debug_set_probe_source(uint8_t track, uint32_t offset, float sample)
{
    if ((debug_track_valid(track) == 0U) || (offset >= AUDIO_BLOCK_SIZE)) return;
    g_debug_block[track][offset] = sample;
    g_debug.block_mask |= (uint8_t)(1U << track);
}

static void debug_build_path(uint8_t file_index)
{
    const char *verdict = (g_debug.verdict != 0U) ? "GOOD" : "BAD";
    const char *probe = prism_debug_boot_probe_name(g_debug.active_probe);
    (void)snprintf(g_debug.folder, sizeof(g_debug.folder),
                   "0:/PRISM_DEBUG/BOOT_%03lu_%s_%s",
                   (unsigned long)g_debug.boot_index, verdict, probe);
    if (file_index < PRISM_DEBUG_TRACK_COUNT)
    {
        char leaf[16];
        (void)snprintf(leaf, sizeof(leaf), "TRACK_%u.wav", (unsigned)(file_index + 1U));
        debug_join_path(g_debug.wav_path, sizeof(g_debug.wav_path), g_debug.folder, leaf);
    }
    debug_join_path(g_debug.info_path, sizeof(g_debug.info_path), g_debug.folder, "INFO.txt");
}

static uint8_t debug_capture_index_used(uint32_t index)
{
    FILINFO info;
    char path[PRISM_DEBUG_PATH_MAX];
    static const char *const verdicts[] = { "BAD", "GOOD" };

    for (uint8_t verdict = 0U; verdict < 2U; ++verdict)
    {
        for (uint8_t probe = 0U; probe < PRISM_DEBUG_PROBE_COUNT; ++probe)
        {
            (void)snprintf(path, sizeof(path), "0:/PRISM_DEBUG/BOOT_%03lu_%s_%s",
                           (unsigned long)index, verdicts[verdict],
                           prism_debug_boot_probe_name((prism_debug_probe_t)probe));
            if (f_stat(path, &info) == FR_OK) return 1U;
        }
    }
    return 0U;
}

static void debug_write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void debug_write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void debug_wav_header(uint8_t *header)
{
    const uint32_t data_bytes = PRISM_DEBUG_RING_FRAMES * 2U;
    memset(header, 0, PRISM_DEBUG_WAV_HEADER);
    memcpy(header, "RIFF", 4U);
    debug_write_le32(&header[4], 36U + data_bytes);
    memcpy(&header[8], "WAVEfmt ", 8U);
    debug_write_le32(&header[16], 16U);
    debug_write_le16(&header[20], 1U);
    debug_write_le16(&header[22], 1U);
    debug_write_le32(&header[24], PRISM_DEBUG_SAMPLE_RATE);
    debug_write_le32(&header[28], PRISM_DEBUG_SAMPLE_RATE * 2U);
    debug_write_le16(&header[32], 2U);
    debug_write_le16(&header[34], 16U);
    memcpy(&header[36], "data", 4U);
    debug_write_le32(&header[40], data_bytes);
}

static void debug_reset_block(void)
{
    memset(g_debug_block, 0, sizeof(g_debug_block));
    g_debug.block_mask = 0U;
}

void prism_debug_boot_init(void)
{
    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t channel[UI_TRACK_COUNT];
    uint8_t source[UI_TRACK_COUNT];

    memset(&g_debug, 0, sizeof(g_debug));
    memset(g_debug_ring, 0, sizeof(g_debug_ring));
    g_debug.requested_probe = PRISM_DEBUG_PROBE_P6;
    g_debug.active_probe = PRISM_DEBUG_PROBE_P6;
    g_debug.state = PRISM_DEBUG_STATE_RUNNING;
    g_debug.boot_index = 1U;
    g_debug.capture_index = 1U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        family[track] = (track < PRISM_DEBUG_TRACK_COUNT) ? UI_TRACK_FAMILY_SYNTH : UI_TRACK_FAMILY_OFF;
        type[track] = (track < PRISM_DEBUG_TRACK_COUNT) ? UI_TRACK_TYPE_PRISM : UI_TRACK_TYPE_NONE;
        channel[track] = (uint8_t)(track + 1U);
        source[track] = (uint8_t)UI_TRACK_MIDI_SRC_INT;
    }
    (void)track_state_apply_bulk(family, type, channel, source);
    track_runtime_refresh_all();

    mixer_set_master(1.0f);
    fx_pool_deactivate_slot(0U);
    fx_pool_deactivate_slot(2U);
    mixer_set_reverb_wet(0.0f);
    mixer_set_delay_volume(0.0f);
    mixer_set_send_fx_slot(0U, -1);
    mixer_set_send_fx_slot(1U, -1);
    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)) continue;

        mixer_set_track_gain(ctx->mix_track_id, 1.0f);
        mixer_set_track_pan(ctx->mix_track_id, 0.0f);
        mixer_set_track_mute(ctx->mix_track_id, 0U);
        mixer_set_track_route(ctx->mix_track_id, MIXER_ROUTE_MASTER);
        mixer_set_track_insert_slot(ctx->mix_track_id, 0U, -1);
        mixer_set_track_insert_slot(ctx->mix_track_id, 1U, -1);
        mixer_set_track_send_level(ctx->mix_track_id, 0U, 0.0f);
        mixer_set_track_send_level(ctx->mix_track_id, 1U, 0.0f);
        mixer_set_track_filter_type(ctx->mix_track_id, MIXER_TRACK_FILTER_LP_BI);
        mixer_set_track_filter_cutoff(ctx->mix_track_id, 20.0f);
        mixer_set_track_filter_resonance(ctx->mix_track_id, 0.0f);
        mixer_set_track_filter_eg_amount(ctx->mix_track_id, 0.0f);
        mixer_set_track_filter_attack(ctx->mix_track_id, 0.001f);
        mixer_set_track_filter_decay(ctx->mix_track_id, 0.001f);
        mixer_set_track_filter_sustain(ctx->mix_track_id, 1.0f);
        mixer_set_track_filter_release(ctx->mix_track_id, 0.1f);
        mixer_set_track_filter_keytrack(ctx->mix_track_id, 0.0f);
        mixer_track_filter_note_on(ctx->mix_track_id, g_debug_notes[track], 127U);

        brick6_braids_runtime_set_osc_level(ctx->instance_id, 0U, 1.0f);
        brick6_braids_runtime_set_osc_level(ctx->instance_id, 1U, 0.0f);
        brick6_braids_runtime_set_osc_edit(ctx->instance_id, 0U, 0.0f);
        brick6_braids_runtime_set_osc_edit(ctx->instance_id, 1U, 0.0f);
        brick6_braids_runtime_set_osc_fm(ctx->instance_id, 0U, 0.0f);
        brick6_braids_runtime_set_osc_fm(ctx->instance_id, 1U, 0.0f);
        brick6_braids_runtime_set_osc_timbre(ctx->instance_id, 0U, 0.5f);
        brick6_braids_runtime_set_osc_color(ctx->instance_id, 0U, 0.5f);
        brick6_braids_runtime_set_osc_timbre(ctx->instance_id, 1U, 0.5f);
        brick6_braids_runtime_set_osc_color(ctx->instance_id, 1U, 0.5f);
        brick6_braids_runtime_note_on(ctx->instance_id, (float)g_debug_notes[track], 1.0f);
        (void)synth_polyphony_note_on(track, g_debug_notes[track]);
        mixer_track_poly_note_on(track, ctx->mix_track_id, 0U, g_debug_notes[track], 127U);
    }
}

uint8_t prism_debug_boot_is_active(void) { return 1U; }

void prism_debug_boot_begin_block(uint32_t frames)
{
    if (frames > AUDIO_BLOCK_SIZE) frames = AUDIO_BLOCK_SIZE;
    g_debug.block_open = 1U;
    g_debug.block_frames = frames;
    g_debug.active_probe = g_debug.requested_probe;
    if (g_debug.freeze_requested != 0U)
    {
        g_debug.frozen = 1U;
        g_debug.freeze_requested = 0U;
        g_debug.state = PRISM_DEBUG_STATE_FROZEN;
    }
    debug_reset_block();
}

void prism_debug_boot_set_render_track(uint8_t track)
{
    g_debug.render_track = track;
}

uint8_t prism_debug_boot_get_render_track(void)
{
    return g_debug.render_track;
}

void prism_debug_boot_capture_p6(uint8_t track, const float *mono, uint32_t frames)
{
    if (g_debug.active_probe == PRISM_DEBUG_PROBE_P6 && g_debug.frozen == 0U)
        debug_copy_float(track, mono, 0U, frames);
}

void prism_debug_boot_capture_p5(uint8_t track, const float *mono, uint32_t frames)
{
    if (g_debug.active_probe == PRISM_DEBUG_PROBE_P5 && g_debug.frozen == 0U)
        debug_copy_float(track, mono, 0U, frames);
}

void prism_debug_boot_capture_p4_sample(uint8_t track, uint32_t offset, float sample)
{
    if (g_debug.active_probe == PRISM_DEBUG_PROBE_P4 && g_debug.frozen == 0U)
        debug_set_probe_source(track, offset, sample);
}

void prism_debug_boot_capture_p3_sample(uint8_t track, uint32_t offset, float sample)
{
    if (g_debug.active_probe == PRISM_DEBUG_PROBE_P3 && g_debug.frozen == 0U)
        debug_set_probe_source(track, offset, sample);
}

void prism_debug_boot_capture_p2(uint8_t track, const int16_t *native, uint32_t offset, uint32_t frames)
{
    if ((g_debug.active_probe != PRISM_DEBUG_PROBE_P2) || (g_debug.frozen != 0U)
            || (native == NULL) || !debug_track_valid(track)) return;
    if (offset >= AUDIO_BLOCK_SIZE) return;
    if (frames > AUDIO_BLOCK_SIZE - offset) frames = AUDIO_BLOCK_SIZE - offset;
    for (uint32_t i = 0U; i < frames; ++i)
        g_debug_block[track][offset + i] = (float)native[i] * (1.0f / 32768.0f);
    g_debug.block_mask |= (uint8_t)(1U << track);
}

void prism_debug_boot_end_block(uint32_t frames)
{
    if (g_debug.block_open == 0U) return;
    g_debug.block_open = 0U;
    if ((g_debug.frozen != 0U) || (frames == 0U)) return;
    if (frames > AUDIO_BLOCK_SIZE) frames = AUDIO_BLOCK_SIZE;
    if (g_debug.block_mask != 0xFFU) return;

    for (uint32_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        uint32_t dst = g_debug.write_frame;
        for (uint32_t i = 0U; i < frames; ++i)
        {
            g_debug_ring[track][dst] = debug_pcm16(g_debug_block[track][i]);
            dst++;
            if (dst >= PRISM_DEBUG_RING_FRAMES) dst = 0U;
        }
    }
    g_debug.write_frame = (g_debug.write_frame + frames) % PRISM_DEBUG_RING_FRAMES;
    g_debug.valid_frames += frames;
    if (g_debug.valid_frames > PRISM_DEBUG_RING_FRAMES) g_debug.valid_frames = PRISM_DEBUG_RING_FRAMES;
}

void prism_debug_boot_request_probe(prism_debug_probe_t probe)
{
    if (probe < PRISM_DEBUG_PROBE_COUNT && g_debug.frozen == 0U)
        g_debug.requested_probe = probe;
}

prism_debug_probe_t prism_debug_boot_get_probe(void) { return g_debug.requested_probe; }

const char *prism_debug_boot_probe_name(prism_debug_probe_t probe)
{
    static const char *const names[] = { "P6", "P5", "P4", "P3", "P2" };
    return (probe < PRISM_DEBUG_PROBE_COUNT) ? names[probe] : "P6";
}

const char *prism_debug_boot_probe_label(prism_debug_probe_t probe)
{
    static const char *const labels[] = {
        "P6 PRE FILTER", "P5 PRISM OUT", "P4 OSC MIX", "P3 OSC1 FLOAT", "P2 OSC1 NATIVE"
    };
    return (probe < PRISM_DEBUG_PROBE_COUNT) ? labels[probe] : labels[0];
}

uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta)
{
    if (encoder != ENC_PAGE || delta == 0 || g_debug.frozen != 0U) return 0U;
    int32_t next = (int32_t)g_debug.requested_probe + ((delta > 0) ? 1 : -1);
    while (next < 0) next += PRISM_DEBUG_PROBE_COUNT;
    next %= PRISM_DEBUG_PROBE_COUNT;
    prism_debug_boot_request_probe((prism_debug_probe_t)next);
    return 1U;
}

uint8_t prism_debug_boot_handle_event(const ui_event_t *event)
{
    if ((event == NULL) || (event->type != UI_EVENT_BUTTON_PRESS)) return 0U;
    if (event->id == (uint8_t)BTN_PAGE_1 && g_debug.frozen == 0U)
    {
        g_debug.verdict = 1U;
        g_debug.freeze_requested = 1U;
        return 1U;
    }
    if (event->id == (uint8_t)BTN_PAGE_2 && g_debug.frozen == 0U)
    {
        g_debug.verdict = 0U;
        g_debug.freeze_requested = 1U;
        return 1U;
    }
    return 1U;
}

static uint8_t debug_open_next_file(void)
{
    uint8_t header[PRISM_DEBUG_WAV_HEADER];
    debug_build_path(g_debug.save_file);
    if (f_open(&g_debug.file, g_debug.wav_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 0U;
    debug_wav_header(header);
    UINT written = 0U;
    if ((f_write(&g_debug.file, header, sizeof(header), &written) != FR_OK) || written != sizeof(header))
    {
        (void)f_close(&g_debug.file);
        return 0U;
    }
    g_debug.file_open = 1U;
    g_debug.save_frame = 0U;
    g_debug.save_oldest = (g_debug.valid_frames == PRISM_DEBUG_RING_FRAMES)
        ? g_debug.write_frame : 0U;
    g_debug.save_frames = (g_debug.valid_frames < PRISM_DEBUG_RING_FRAMES)
        ? g_debug.valid_frames : PRISM_DEBUG_RING_FRAMES;
    return 1U;
}

static uint8_t debug_write_file_chunk(void)
{
    uint32_t count = PRISM_DEBUG_WAV_CHUNK;
    if (g_debug.save_frame >= PRISM_DEBUG_RING_FRAMES) return 1U;
    if (count > PRISM_DEBUG_RING_FRAMES - g_debug.save_frame) count = PRISM_DEBUG_RING_FRAMES - g_debug.save_frame;
    for (uint32_t i = 0U; i < count; ++i)
    {
        const uint32_t logical = g_debug.save_frame + i;
        const uint32_t padding = PRISM_DEBUG_RING_FRAMES - g_debug.save_frames;
        const uint8_t valid = (logical >= padding) ? 1U : 0U;
        const uint32_t src = (g_debug.save_oldest + logical - padding) % PRISM_DEBUG_RING_FRAMES;
        const int16_t sample = (valid != 0U) ? g_debug_ring[g_debug.save_file][src] : 0;
        debug_write_le16(&g_debug.io[i * 2U], (uint16_t)sample);
    }
    UINT written = 0U;
    if ((f_write(&g_debug.file, g_debug.io, count * 2U, &written) != FR_OK)
            || (written != count * 2U)) return 0U;
    g_debug.save_frame += count;
    return 1U;
}

static uint8_t debug_write_info(void)
{
    FIL info;
    if (f_open(&info, g_debug.info_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 0U;
    char text[1024];
    int length = snprintf(text, sizeof(text),
        "verdict = %s\nprobe = %s\nprobe_name = %s\ncapture_index = %lu\nboot_index = %lu\n"
        "sample_rate = %u\nframes_per_channel = %u\ntrack_count = 8\nengine = PRISM\n"
        "osc1 = ON\nosc2 = OFF\nfilter = ON\ncutoff = 0\n",
        (g_debug.verdict != 0U) ? "GOOD" : "BAD",
        prism_debug_boot_probe_name(g_debug.active_probe),
        prism_debug_boot_probe_label(g_debug.active_probe),
        (unsigned long)g_debug.capture_index, (unsigned long)g_debug.boot_index,
        PRISM_DEBUG_SAMPLE_RATE, PRISM_DEBUG_RING_FRAMES);
    for (uint8_t i = 0U; i < PRISM_DEBUG_TRACK_COUNT && length > 0 && length < (int)sizeof(text); ++i)
        length += snprintf(&text[length], sizeof(text) - (size_t)length, "note_track_%u = %u\n",
                           (unsigned)(i + 1U), (unsigned)g_debug_notes[i]);
    UINT written = 0U;
    const uint8_t ok = (length > 0 && f_write(&info, text, (UINT)length, &written) == FR_OK
                        && written == (UINT)length) ? 1U : 0U;
    (void)f_close(&info);
    return ok;
}

void prism_debug_boot_service(void)
{
    if (g_debug.state == PRISM_DEBUG_STATE_FROZEN)
    {
        if (sd_access_gate_try_acquire(PRISM_DEBUG_CAPTURE_CLIENT) == 0U) return;
        FILINFO root_info;
        if (sd_access_fs_mount_if_needed() == 0U)
        {
            sd_access_gate_release(PRISM_DEBUG_CAPTURE_CLIENT);
            g_debug.state = PRISM_DEBUG_STATE_ERROR;
            return;
        }
        const FRESULT root_mkdir = f_mkdir("0:/PRISM_DEBUG");
        if ((root_mkdir != FR_OK) && (f_stat("0:/PRISM_DEBUG", &root_info) != FR_OK))
        {
            sd_access_gate_release(PRISM_DEBUG_CAPTURE_CLIENT);
            g_debug.state = PRISM_DEBUG_STATE_ERROR;
            return;
        }
        while (debug_capture_index_used(g_debug.boot_index) != 0U)
        {
            ++g_debug.boot_index;
        }
        g_debug.capture_index = g_debug.boot_index;
        debug_build_path(0U);
        (void)f_mkdir(g_debug.folder);
        g_debug.save_file = 0U;
        g_debug.file_open = 0U;
        g_debug.state = PRISM_DEBUG_STATE_SAVING;
        sd_access_gate_release(PRISM_DEBUG_CAPTURE_CLIENT);
        return;
    }
    if (g_debug.state != PRISM_DEBUG_STATE_SAVING) return;
    if (sd_access_gate_try_acquire(PRISM_DEBUG_CAPTURE_CLIENT) == 0U) return;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        g_debug.state = PRISM_DEBUG_STATE_ERROR;
        sd_access_gate_release(PRISM_DEBUG_CAPTURE_CLIENT);
        return;
    }
    if (g_debug.file_open == 0U)
    {
        if (g_debug.save_file < PRISM_DEBUG_TRACK_COUNT)
        {
            if (debug_open_next_file() == 0U) g_debug.state = PRISM_DEBUG_STATE_ERROR;
        }
        else if (debug_write_info() == 0U)
        {
            g_debug.state = PRISM_DEBUG_STATE_ERROR;
        }
        else
        {
            g_debug.state = PRISM_DEBUG_STATE_DONE;
        }
    }
    else if (debug_write_file_chunk() == 0U)
    {
        (void)f_close(&g_debug.file);
        g_debug.file_open = 0U;
        g_debug.state = PRISM_DEBUG_STATE_ERROR;
    }
    else if (g_debug.save_frame >= PRISM_DEBUG_RING_FRAMES)
    {
        (void)f_close(&g_debug.file);
        g_debug.file_open = 0U;
        g_debug.save_file++;
    }
    sd_access_gate_release(PRISM_DEBUG_CAPTURE_CLIENT);
}

void prism_debug_boot_render(void)
{
    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, "PRISM DEBUG");
    drv_display_draw_text(0U, 12U, "PROBE:");
    drv_display_draw_text(0U, 20U, prism_debug_boot_probe_label(g_debug.requested_probe));
    if (g_debug.requested_probe != g_debug.active_probe) drv_display_draw_text(84U, 20U, "WAIT");
    drv_display_draw_text(0U, 32U, "GOOD: PAGE1");
    drv_display_draw_text(0U, 42U, "BAD: PAGE2");
    if (g_debug.state == PRISM_DEBUG_STATE_SAVING) drv_display_draw_text(0U, 54U, "SAVING...");
    else if (g_debug.state == PRISM_DEBUG_STATE_DONE) drv_display_draw_text(0U, 54U, "CAPTURE DONE");
    else if (g_debug.state == PRISM_DEBUG_STATE_ERROR) drv_display_draw_text(0U, 54U, "SD ERROR");
    else drv_display_draw_text(0U, 54U, "8 TRACKS RUNNING");
}

#else

void prism_debug_boot_init(void) {}
void prism_debug_boot_service(void) {}
uint8_t prism_debug_boot_is_active(void) { return 0U; }
void prism_debug_boot_begin_block(uint32_t frames) { (void)frames; }
void prism_debug_boot_set_render_track(uint8_t track) { (void)track; }
uint8_t prism_debug_boot_get_render_track(void) { return 0U; }
void prism_debug_boot_capture_p6(uint8_t track, const float *mono, uint32_t frames) { (void)track; (void)mono; (void)frames; }
void prism_debug_boot_capture_p5(uint8_t track, const float *mono, uint32_t frames) { (void)track; (void)mono; (void)frames; }
void prism_debug_boot_capture_p4_sample(uint8_t track, uint32_t offset, float sample) { (void)track; (void)offset; (void)sample; }
void prism_debug_boot_capture_p3_sample(uint8_t track, uint32_t offset, float sample) { (void)track; (void)offset; (void)sample; }
void prism_debug_boot_capture_p2(uint8_t track, const int16_t *native, uint32_t offset, uint32_t frames) { (void)track; (void)native; (void)offset; (void)frames; }
void prism_debug_boot_end_block(uint32_t frames) { (void)frames; }
void prism_debug_boot_request_probe(prism_debug_probe_t probe) { (void)probe; }
prism_debug_probe_t prism_debug_boot_get_probe(void) { return PRISM_DEBUG_PROBE_P6; }
const char *prism_debug_boot_probe_name(prism_debug_probe_t probe) { (void)probe; return "P6"; }
const char *prism_debug_boot_probe_label(prism_debug_probe_t probe) { (void)probe; return "P6 PRE FILTER"; }
uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta) { (void)encoder; (void)delta; return 0U; }
uint8_t prism_debug_boot_handle_event(const ui_event_t *event) { (void)event; return 0U; }
void prism_debug_boot_render(void) {}

#endif
