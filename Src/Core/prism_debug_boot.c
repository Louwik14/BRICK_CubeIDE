#include "Core/prism_debug_boot.h"

#if BRICK6_PRISM_DEBUG_BOOT

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Audio/audio.h"
#include "Audio/audio_track_diag.h"
#include "Audio/fx_pool.h"
#include "Audio/metronome_runtime.h"
#include "Audio/mixer.h"
#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_audio_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Param/param_store.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "buttons.h"
#include "drv_display.h"
#include "ff.h"
#include "font.h"
#include "stm32h7xx_hal.h"

#define PRISM_DEBUG_REPORT_CLIENT SD_ACCESS_CLIENT_RECORDER
#define PRISM_DEBUG_PATH_MAX 160U
#define PRISM_DEBUG_REPORT_MAX 16384U
#define PRISM_DEBUG_NOTE_BASE 48U

typedef enum
{
    PRISM_DEBUG_STATE_ARMED = 0,
    PRISM_DEBUG_STATE_RUNNING_INITIAL,
    PRISM_DEBUG_STATE_INITIAL_COMPLETE,
    PRISM_DEBUG_STATE_SAI_RESTARTING,
    PRISM_DEBUG_STATE_RUNNING_RETEST,
    PRISM_DEBUG_STATE_RETEST_COMPLETE,
    PRISM_DEBUG_STATE_SAVING,
    PRISM_DEBUG_STATE_DONE,
    PRISM_DEBUG_STATE_ERROR
} prism_debug_state_t;

typedef enum
{
    PRISM_DEBUG_SAVE_NONE = 0,
    PRISM_DEBUG_SAVE_MOUNT,
    PRISM_DEBUG_SAVE_DIRECTORY,
    PRISM_DEBUG_SAVE_OPEN,
    PRISM_DEBUG_SAVE_WRITE
} prism_debug_save_error_t;

typedef struct
{
    uint8_t verdict;
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t test_frames;
    uint32_t audio_blocks;
    uint32_t max_frames_per_block;
    audio_runtime_diag_t audio_diag;
    board_audio_boot_diag_t boot_diag;
    board_audio_runtime_diag_t board_diag;
    board_audio_codec_snapshot_t codec_diag;
} prism_debug_window_t;

typedef struct
{
    volatile prism_debug_state_t state;
    uint8_t notes[PRISM_DEBUG_TRACK_COUNT];
    uint8_t instance_id[PRISM_DEBUG_TRACK_COUNT];
    uint8_t voice_slot[PRISM_DEBUG_TRACK_COUNT];
    uint8_t mix_track_id[PRISM_DEBUG_TRACK_COUNT];
    uintptr_t voice_state_address[PRISM_DEBUG_TRACK_COUNT];
    uint32_t test_index;
    uint32_t boot_index;
    uint8_t initial_verdict;
    uint8_t retest_verdict;
    uint8_t retest_mode;
    uint8_t codec_snapshot_pending;
    uint8_t restart_completed;
    prism_debug_save_error_t save_error;
    board_audio_restart_diag_t restart_diag;
    board_audio_codec_snapshot_t codec_before_restart;
    board_audio_codec_snapshot_t codec_after_restart;
    prism_debug_window_t initial;
    prism_debug_window_t retest;
    char path[PRISM_DEBUG_PATH_MAX];
} prism_debug_state_data_t;

STORAGE_STATE_SDRAM static prism_debug_state_data_t g_debug;

static const uint8_t g_debug_notes[PRISM_DEBUG_TRACK_COUNT] = {
    PRISM_DEBUG_NOTE_BASE, 52U, 55U, 59U, 62U, 67U, 71U, 74U
};

static const char *const g_codec_reg_names[BOARD_AUDIO_CODEC_REG_COUNT_SENTINEL] = {
    "interface", "clock_0", "clock_1", "clock_2", "clock_3", "clock_4", "clock_5",
    "clock_6", "clock_7", "dac_state", "mute", "route_l", "route_r", "output_power",
    "digital_volume_l", "digital_volume_r", "analog_volume_l", "analog_volume_r", "status",
    "status_mask", "functional_mode", "chip_id"
};

static const char *debug_state_label(void)
{
    switch (g_debug.state)
    {
        case PRISM_DEBUG_STATE_ARMED: return "WAIT START";
        case PRISM_DEBUG_STATE_RUNNING_INITIAL: return "INITIAL 6S";
        case PRISM_DEBUG_STATE_INITIAL_COMPLETE: return "INITIAL DONE";
        case PRISM_DEBUG_STATE_SAI_RESTARTING: return "SAI RESTART...";
        case PRISM_DEBUG_STATE_RUNNING_RETEST: return "RETEST 6S";
        case PRISM_DEBUG_STATE_RETEST_COMPLETE: return "AFTER RESTART";
        case PRISM_DEBUG_STATE_SAVING: return "SAVING";
        case PRISM_DEBUG_STATE_DONE: return "SAVE OK";
        default: return "SAVE ERROR";
    }
}

static const char *debug_save_error_label(void)
{
    switch (g_debug.save_error)
    {
        case PRISM_DEBUG_SAVE_MOUNT: return "MOUNT";
        case PRISM_DEBUG_SAVE_DIRECTORY: return "DIRECTORY";
        case PRISM_DEBUG_SAVE_OPEN: return "OPEN";
        case PRISM_DEBUG_SAVE_WRITE: return "WRITE";
        default: return "NONE";
    }
}

static prism_debug_window_t *debug_active_window(void)
{
    return (g_debug.state == PRISM_DEBUG_STATE_RUNNING_RETEST)
        ? &g_debug.retest : &g_debug.initial;
}

static void debug_configure_tracks(void)
{
    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t channel[UI_TRACK_COUNT];
    uint8_t source[UI_TRACK_COUNT];

    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const old_ctx = track_runtime_get_ctx(track);
        if ((old_ctx != NULL) && (old_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
        {
            brick6_braids_runtime_all_notes_off(old_ctx->instance_id);
        }
        synth_polyphony_reset_track(track);
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        family[track] = (track < PRISM_DEBUG_TRACK_COUNT)
            ? UI_TRACK_FAMILY_SYNTH : UI_TRACK_FAMILY_OFF;
        type[track] = (track < PRISM_DEBUG_TRACK_COUNT)
            ? UI_TRACK_TYPE_PRISM : UI_TRACK_TYPE_NONE;
        channel[track] = (uint8_t)(track + 1U);
        source[track] = (uint8_t)UI_TRACK_MIDI_SRC_INT;
    }
    (void)track_state_apply_bulk(family, type, channel, source);
    track_runtime_refresh_all();

    mixer_set_master(0.0f);
    metronome_runtime_set_level_u7(0U);
    fx_pool_deactivate_slot(0U);
    fx_pool_deactivate_slot(2U);
    mixer_set_reverb_wet(0.0f);
    mixer_set_delay_volume(0.0f);
    mixer_set_send_fx_slot(0U, -1);
    mixer_set_send_fx_slot(1U, -1);
    audio_track_diag_close();

    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        g_debug.notes[track] = g_debug_notes[track];
        g_debug.instance_id[track] = 0xFFU;
        g_debug.voice_slot[track] = 0xFFU;
        g_debug.mix_track_id[track] = 0xFFU;
        g_debug.voice_state_address[track] = 0U;
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)) continue;

        g_debug.instance_id[track] = ctx->instance_id;
        g_debug.mix_track_id[track] = ctx->mix_track_id;
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
        (void)synth_polyphony_set_voice_count(track, 1U);
        g_debug.voice_slot[track] = synth_polyphony_get_slot(track, 0U);
    }
}

static void debug_stop_scenario(void)
{
    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)) continue;
        brick6_braids_runtime_note_off(ctx->instance_id, g_debug.notes[track]);
        synth_polyphony_all_notes_off(track);
        mixer_track_poly_all_notes_off(ctx->mix_track_id);
    }
    mixer_set_master(0.0f);
}

static void debug_capture_frozen_diagnostics(prism_debug_window_t *window)
{
    audio_runtime_diag_snapshot(&window->audio_diag);
    board_audio_get_boot_diag(&window->boot_diag);
    board_audio_get_runtime_diag(&window->board_diag);
}

static void debug_complete_test(uint32_t frames)
{
    if ((g_debug.state != PRISM_DEBUG_STATE_RUNNING_INITIAL)
            && (g_debug.state != PRISM_DEBUG_STATE_RUNNING_RETEST)) return;
    prism_debug_window_t *const window = debug_active_window();
    window->test_frames += frames;
    if (window->test_frames < PRISM_DEBUG_TEST_FRAMES) return;
    window->test_frames = PRISM_DEBUG_TEST_FRAMES;
    window->end_tick = HAL_GetTick();
    debug_capture_frozen_diagnostics(window);
    g_debug.codec_snapshot_pending = 1U;
    debug_stop_scenario();
    g_debug.state = (g_debug.state == PRISM_DEBUG_STATE_RUNNING_RETEST)
        ? PRISM_DEBUG_STATE_RETEST_COMPLETE : PRISM_DEBUG_STATE_INITIAL_COMPLETE;
}

void prism_debug_boot_init(void)
{
    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.state = PRISM_DEBUG_STATE_ARMED;
    g_debug.boot_index = 1U;
    debug_configure_tracks();
}

void prism_debug_boot_start_test(void)
{
    if (g_debug.state != PRISM_DEBUG_STATE_ARMED) return;
    prism_debug_window_t *const active = (g_debug.retest_mode != 0U)
        ? &g_debug.retest : &g_debug.initial;
    active->verdict = 0U;
    mixer_set_master(1.0f);
    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)) continue;
        brick6_braids_runtime_note_on(ctx->instance_id, (float)g_debug.notes[track], 1.0f);
        (void)synth_polyphony_note_on(track, g_debug.notes[track]);
        mixer_track_filter_note_on(ctx->mix_track_id, g_debug.notes[track], 127U);
        mixer_track_poly_note_on(track, ctx->mix_track_id, 0U, g_debug.notes[track], 127U);
        const brick6_braids_runtime_voice_t *const voice =
            brick6_braids_runtime_get_voice(ctx->instance_id);
        g_debug.voice_state_address[track] = (uintptr_t)voice;
        g_debug.voice_slot[track] = synth_polyphony_get_slot(track, 0U);
    }
    active->test_frames = 0U;
    active->audio_blocks = 0U;
    active->max_frames_per_block = 0U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    audio_runtime_diag_reset_for_test();
    active->start_tick = HAL_GetTick();
    g_debug.state = (g_debug.retest_mode != 0U)
        ? PRISM_DEBUG_STATE_RUNNING_RETEST : PRISM_DEBUG_STATE_RUNNING_INITIAL;
    if (primask == 0U) __enable_irq();
}

uint8_t prism_debug_boot_is_active(void)
{
    return 1U;
}

void prism_debug_boot_audio_block_begin(uint32_t frames)
{
    if ((g_debug.state != PRISM_DEBUG_STATE_RUNNING_INITIAL)
            && (g_debug.state != PRISM_DEBUG_STATE_RUNNING_RETEST)) return;
    prism_debug_window_t *const window = debug_active_window();
    window->audio_blocks++;
    if (frames > window->max_frames_per_block) window->max_frames_per_block = frames;
}

void prism_debug_boot_audio_half_complete(uint8_t half_index, uint32_t frames)
{
    (void)half_index;
    debug_complete_test(frames);
}

uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta)
{
    (void)encoder;
    (void)delta;
    return 1U;
}

static void debug_capture_post_test_codec(void)
{
    if ((g_debug.codec_snapshot_pending != 0U)
            && ((g_debug.state == PRISM_DEBUG_STATE_INITIAL_COMPLETE)
                || (g_debug.state == PRISM_DEBUG_STATE_RETEST_COMPLETE)))
    {
        board_audio_codec_snapshot_t *const snapshot =
            (g_debug.state == PRISM_DEBUG_STATE_RETEST_COMPLETE)
            ? &g_debug.retest.codec_diag : &g_debug.initial.codec_diag;
        board_audio_get_codec_post_test_snapshot(snapshot);
        g_debug.codec_snapshot_pending = 0U;
    }
}

uint8_t prism_debug_boot_handle_event(const ui_event_t *event)
{
    if ((event == NULL) || (event->type != UI_EVENT_BUTTON_PRESS)) return 1U;
    debug_capture_post_test_codec();
    if (g_debug.state == PRISM_DEBUG_STATE_INITIAL_COMPLETE)
    {
        if (event->id == (uint8_t)BTN_PAGE_1)
        {
            g_debug.initial_verdict = 1U;
            g_debug.state = PRISM_DEBUG_STATE_SAVING;
        }
        else if (event->id == (uint8_t)BTN_PAGE_2)
        {
            g_debug.initial_verdict = 0U;
            g_debug.state = PRISM_DEBUG_STATE_SAI_RESTARTING;
        }
    }
    else if (g_debug.state == PRISM_DEBUG_STATE_RETEST_COMPLETE)
    {
        if (event->id == (uint8_t)BTN_PAGE_1)
        {
            g_debug.retest_verdict = 1U;
            g_debug.state = PRISM_DEBUG_STATE_SAVING;
        }
        else if (event->id == (uint8_t)BTN_PAGE_2)
        {
            g_debug.retest_verdict = 0U;
            g_debug.state = PRISM_DEBUG_STATE_SAVING;
        }
    }
    return 1U;
}

static uint32_t debug_find_test_index(const char *suffix)
{
    FILINFO info;
    char path[PRISM_DEBUG_PATH_MAX];
    for (uint32_t index = 1U; index < 10000U; ++index)
    {
        (void)snprintf(path, sizeof(path), "0:/PRISM_HW_DEBUG/TEST_%04lu_%s.txt",
                       (unsigned long)index, suffix);
        if (f_stat(path, &info) != FR_OK) return index;
    }
    return 9999U;
}

static int debug_append(char *text, size_t capacity, int length, const char *format, ...)
{
    va_list args;
    if ((length < 0) || ((size_t)length >= capacity)) return length;
    va_start(args, format);
    length += vsnprintf(&text[length], capacity - (size_t)length, format, args);
    va_end(args);
    return length;
}

static const char *debug_last_callback(const audio_runtime_diag_t *diag)
{
    if (diag->last_callback == 1U) return "HALF";
    if (diag->last_callback == 2U) return "FULL";
    return "NONE";
}

static int debug_append_codec_snapshot(char *report, int length, const char *label,
                                       const board_audio_codec_snapshot_t *snapshot)
{
    length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
        "%s_read_ok = %u\n%s_i2c_error = %u\n", label,
        (unsigned)snapshot->read_ok, label, (unsigned)snapshot->i2c_error);
    for (uint8_t id = 0U; id < (uint8_t)BOARD_AUDIO_CODEC_REG_COUNT_SENTINEL; ++id)
    {
        if ((snapshot->valid_mask & (1UL << id)) != 0U)
        {
            length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
                "%s_expected_%s = 0x%02X\n%s_actual_%s = 0x%02X\n", label,
                g_codec_reg_names[id], (unsigned)snapshot->expected[id], label,
                g_codec_reg_names[id], (unsigned)snapshot->actual[id]);
        }
        else
        {
            length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
                "%s_expected_%s = 0x%02X\n%s_actual_%s = NA\n", label,
                g_codec_reg_names[id], (unsigned)snapshot->expected[id], label,
                g_codec_reg_names[id]);
        }
    }
    return length;
}

static int debug_append_window(char *report, int length, const char *section,
                               const prism_debug_window_t *window)
{
    const uint32_t elapsed_ms = window->end_tick - window->start_tick;
    length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
        "[%s]\nverdict = %s\nduration_seconds = 6\nduration_frames = %lu\n"
        "duration_wall_ms = %lu\nsample_rate = %u\nframes_per_block = %u\n"
        "audio_blocks = %lu\nmax_frames_per_block = %lu\ntracks = 8\n"
        "notes = %u,%u,%u,%u,%u,%u,%u,%u\n",
        section, (window->verdict != 0U) ? "GOOD" : "BAD",
        (unsigned long)window->test_frames, (unsigned long)elapsed_ms,
        PRISM_DEBUG_SAMPLE_RATE, AUDIO_BLOCK_SIZE, (unsigned long)window->audio_blocks,
        (unsigned long)window->max_frames_per_block,
        g_debug.notes[0], g_debug.notes[1], g_debug.notes[2], g_debug.notes[3],
        g_debug.notes[4], g_debug.notes[5], g_debug.notes[6], g_debug.notes[7]);
    length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
        "tx_buffer_address = 0x%08lX\ntx_buffer_bytes = %lu\n"
        "tx_buffer_cacheable = %u\ntx_buffer_region = .ram_d2_lut\n"
        "tx_cache_clean_active = %u\nformat = PCM24 right-aligned in int32\n"
        "hal_sai_transmit_dma_word_count = %lu\nhalf_callbacks = %lu\n"
        "full_callbacks = %lu\nlast_callback = %s\ntest_callback_total = %lu\n"
        "expected_audio_blocks = %u\nexpected_callback_total = %u\n"
        "callback_alternation_errors = %lu\ndma_error_callbacks = %lu\n"
        "sai_error_callbacks = %lu\nunderrun_callbacks = %lu\n"
        "last_dma_error_code = 0x%08lX\nlast_sai_error_code = 0x%08lX\n"
        "fill_half_0 = %lu\nfill_half_1 = %lu\nwrong_half_writes = %lu\n"
        "half_not_ready = %lu\nlate_fills = %lu\nmax_fill_cycles = %lu\n"
        "sai_tx_state = %lu\nsai_rx_state = %lu\nsai_tx_error = 0x%08lX\n"
        "sai_rx_error = 0x%08lX\ndma_tx_state = %lu\ndma_rx_state = %lu\n"
        "dma_tx_error = 0x%08lX\ndma_rx_error = 0x%08lX\n"
        "sai_frame_length = %lu\nsai_active_frame_length = %lu\n"
        "sai_data_size = %lu\nsai_slot_size = %lu\nsai_slot_number = %lu\n"
        "sai_slot_active = 0x%08lX\n",
        (unsigned long)window->audio_diag.tx_buffer_address,
        (unsigned long)window->audio_diag.tx_buffer_bytes,
        (unsigned)window->audio_diag.tx_cacheable,
        (unsigned)window->audio_diag.cache_maintenance_active,
        (unsigned long)window->audio_diag.dma_word_count,
        (unsigned long)window->audio_diag.half_callbacks,
        (unsigned long)window->audio_diag.full_callbacks,
        debug_last_callback(&window->audio_diag),
        (unsigned long)(window->audio_diag.half_callbacks + window->audio_diag.full_callbacks),
        PRISM_DEBUG_TEST_FRAMES / AUDIO_BLOCK_SIZE,
        PRISM_DEBUG_TEST_FRAMES / BOARD_AUDIO_FRAMES_PER_HALF,
        (unsigned long)window->audio_diag.callback_alternation_errors,
        (unsigned long)window->audio_diag.dma_error_callbacks,
        (unsigned long)window->audio_diag.sai_error_callbacks,
        (unsigned long)window->audio_diag.underrun_callbacks,
        (unsigned long)window->audio_diag.last_dma_error_code,
        (unsigned long)window->audio_diag.last_sai_error_code,
        (unsigned long)window->audio_diag.fill_count[0],
        (unsigned long)window->audio_diag.fill_count[1],
        (unsigned long)window->audio_diag.wrong_half_writes,
        (unsigned long)window->audio_diag.half_not_ready,
        (unsigned long)window->audio_diag.late_fills,
        (unsigned long)window->audio_diag.max_fill_cycles,
        (unsigned long)window->board_diag.tx_sai_state,
        (unsigned long)window->board_diag.rx_sai_state,
        (unsigned long)window->board_diag.tx_sai_error_code,
        (unsigned long)window->board_diag.rx_sai_error_code,
        (unsigned long)window->board_diag.tx_dma_state,
        (unsigned long)window->board_diag.rx_dma_state,
        (unsigned long)window->board_diag.tx_dma_error_code,
        (unsigned long)window->board_diag.rx_dma_error_code,
        (unsigned long)window->board_diag.frame_length,
        (unsigned long)window->board_diag.active_frame_length,
        (unsigned long)window->board_diag.data_size,
        (unsigned long)window->board_diag.slot_size,
        (unsigned long)window->board_diag.slot_number,
        (unsigned long)window->board_diag.slot_active);
    length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
        "osc1 = ON\nosc2 = OFF\nfilter = LP_BI\nfilter_cutoff_hz = 20\n");
    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
            "track_%u = note:%u instance:%u voice_slot:%u mix_track:%u voice_state:0x%08lX\n",
            (unsigned)(track + 1U), (unsigned)g_debug.notes[track],
            (unsigned)g_debug.instance_id[track], (unsigned)g_debug.voice_slot[track],
            (unsigned)g_debug.mix_track_id[track],
            (unsigned long)g_debug.voice_state_address[track]);
    }
    length = debug_append(report, PRISM_DEBUG_REPORT_MAX, length,
        "codec_ready = %u\ncodec_reset_ok = %u\ncodec_clocks_ok = %u\n"
        "codec_interface_ok = %u\ndac_powered = %u\ndac_routed = %u\n"
        "dac_unmuted = %u\noutput_routed = %u\noutput_powered = %u\n"
        "output_unmuted = %u\ncodec_volume_ok = %u\n",
        (unsigned)window->boot_diag.codec_ready, (unsigned)window->boot_diag.reset_ok,
        (unsigned)window->boot_diag.clocks_ok, (unsigned)window->boot_diag.interface_ok,
        (unsigned)window->boot_diag.dac_powered, (unsigned)window->boot_diag.dac_routed,
        (unsigned)window->boot_diag.dac_unmuted, (unsigned)window->boot_diag.output_routed,
        (unsigned)window->boot_diag.output_powered, (unsigned)window->boot_diag.output_unmuted,
        (unsigned)window->boot_diag.volume_ok);
    return debug_append_codec_snapshot(report, length, section, &window->codec_diag);
}

static int debug_write_report(void)
{
    const uint8_t restart_attempted = (g_debug.initial_verdict == 0U) ? 1U : 0U;
    char report[PRISM_DEBUG_REPORT_MAX];
    int length = snprintf(report, sizeof(report),
        "test_variant = SAI_DMA_RESTART_V1_TX_THEN_RX\n"
        "test_index = %lu\nboot_index = %lu\n"
        "mcu_rebooted = 0\nclock_tree_reinitialized = 0\ncodec_reinitialized = 0\n"
        "codec_reset = 0\nsai_restart_attempted = %u\n"
        "sai_restarted = %u\ndma_restarted = %u\n",
        (unsigned long)g_debug.test_index, (unsigned long)g_debug.boot_index,
        (unsigned)restart_attempted, (unsigned)g_debug.restart_completed,
        (unsigned)g_debug.restart_completed);
    g_debug.initial.verdict = g_debug.initial_verdict;
    length = debug_append_window(report, length, "INITIAL_TEST", &g_debug.initial);
    if (restart_attempted != 0U)
    {
        length = debug_append(report, sizeof(report), length,
            "\n[SAI_DMA_RESTART]\nvariant_id = SAI_DMA_RESTART_V1_TX_THEN_RX\n"
            "stop_order = RX_THEN_TX\nstart_order = TX_THEN_RX\n"
            "supported = %u\nsuccess = %u\nword_count = %lu\n"
            "stop_rx_hal = %u\nstop_tx_hal = %u\n"
            "start_tx_hal = %u\nstart_rx_hal = %u\n"
            "fifo_flushed = %u\nflags_cleared = %u\nbuffers_zeroed = %u\n"
            "tx_sr_before = 0x%08lX\nrx_sr_before = 0x%08lX\n"
            "tx_sr_after_purge = 0x%08lX\nrx_sr_after_purge = 0x%08lX\n"
            "tx_sr_after_restart = 0x%08lX\nrx_sr_after_restart = 0x%08lX\n"
            "tx_cr1_before = 0x%08lX\nrx_cr1_before = 0x%08lX\n"
            "tx_cr1_after_restart = 0x%08lX\nrx_cr1_after_restart = 0x%08lX\n"
            "tx_mcken_before = %u\ntx_mckdiv_before = %lu\n"
            "tx_mcken_after_restart = %u\ntx_mckdiv_after_restart = %lu\n"
            "before_tx_sai_state = %lu\nbefore_rx_sai_state = %lu\n"
            "before_tx_dma_state = %lu\nbefore_rx_dma_state = %lu\n"
            "purged_tx_sai_state = %lu\npurged_rx_sai_state = %lu\n"
            "purged_tx_dma_state = %lu\npurged_rx_dma_state = %lu\n"
            "restarted_tx_sai_state = %lu\nrestarted_rx_sai_state = %lu\n"
            "restarted_tx_dma_state = %lu\nrestarted_rx_dma_state = %lu\n"
            "restarted_tx_sai_error = 0x%08lX\nrestarted_rx_sai_error = 0x%08lX\n"
            "restarted_tx_dma_error = 0x%08lX\nrestarted_rx_dma_error = 0x%08lX\n"
            "snapshot_before_restart = CODEC_BEFORE_SAI_RESTART\n",
            (unsigned)g_debug.restart_diag.supported,
            (unsigned)g_debug.restart_diag.success,
            (unsigned long)g_debug.restart_diag.word_count,
            (unsigned)g_debug.restart_diag.stop_rx_status,
            (unsigned)g_debug.restart_diag.stop_tx_status,
            (unsigned)g_debug.restart_diag.start_tx_status,
            (unsigned)g_debug.restart_diag.start_rx_status,
            (unsigned)g_debug.restart_diag.fifo_flushed,
            (unsigned)g_debug.restart_diag.flags_cleared,
            (unsigned)g_debug.restart_diag.buffers_zeroed,
            (unsigned long)g_debug.restart_diag.tx_sr_before,
            (unsigned long)g_debug.restart_diag.rx_sr_before,
            (unsigned long)g_debug.restart_diag.tx_sr_after_purge,
            (unsigned long)g_debug.restart_diag.rx_sr_after_purge,
            (unsigned long)g_debug.restart_diag.tx_sr_after_restart,
            (unsigned long)g_debug.restart_diag.rx_sr_after_restart,
            (unsigned long)g_debug.restart_diag.tx_cr1_before,
            (unsigned long)g_debug.restart_diag.rx_cr1_before,
            (unsigned long)g_debug.restart_diag.tx_cr1_after_restart,
            (unsigned long)g_debug.restart_diag.rx_cr1_after_restart,
            (unsigned)((g_debug.restart_diag.tx_cr1_before & SAI_xCR1_MCKEN) != 0U),
            (unsigned long)((g_debug.restart_diag.tx_cr1_before & SAI_xCR1_MCKDIV) >> 20U),
            (unsigned)((g_debug.restart_diag.tx_cr1_after_restart & SAI_xCR1_MCKEN) != 0U),
            (unsigned long)((g_debug.restart_diag.tx_cr1_after_restart & SAI_xCR1_MCKDIV) >> 20U),
            (unsigned long)g_debug.restart_diag.before.tx_sai_state,
            (unsigned long)g_debug.restart_diag.before.rx_sai_state,
            (unsigned long)g_debug.restart_diag.before.tx_dma_state,
            (unsigned long)g_debug.restart_diag.before.rx_dma_state,
            (unsigned long)g_debug.restart_diag.after_purge.tx_sai_state,
            (unsigned long)g_debug.restart_diag.after_purge.rx_sai_state,
            (unsigned long)g_debug.restart_diag.after_purge.tx_dma_state,
            (unsigned long)g_debug.restart_diag.after_purge.rx_dma_state,
            (unsigned long)g_debug.restart_diag.after_restart.tx_sai_state,
            (unsigned long)g_debug.restart_diag.after_restart.rx_sai_state,
            (unsigned long)g_debug.restart_diag.after_restart.tx_dma_state,
            (unsigned long)g_debug.restart_diag.after_restart.rx_dma_state,
            (unsigned long)g_debug.restart_diag.after_restart.tx_sai_error_code,
            (unsigned long)g_debug.restart_diag.after_restart.rx_sai_error_code,
            (unsigned long)g_debug.restart_diag.after_restart.tx_dma_error_code,
            (unsigned long)g_debug.restart_diag.after_restart.rx_dma_error_code);
        length = debug_append_codec_snapshot(report, length, "before_sai_restart",
                                              &g_debug.codec_before_restart);
        length = debug_append(report, sizeof(report), length,
                              "snapshot_after_restart = CODEC_AFTER_SAI_RESTART\n");
        length = debug_append_codec_snapshot(report, length, "after_sai_restart",
                                              &g_debug.codec_after_restart);
        if (g_debug.restart_completed != 0U)
        {
            g_debug.retest.verdict = g_debug.retest_verdict;
            length = debug_append_window(report, length,
                                         "AFTER_SAI_DMA_RESTART_TEST", &g_debug.retest);
        }
    }
    if ((length <= 0) || ((size_t)length >= sizeof(report))) return 0U;
    FIL file;
    UINT written = 0U;
    if (f_open(&file, g_debug.path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        g_debug.save_error = PRISM_DEBUG_SAVE_OPEN;
        return 0U;
    }
    const FRESULT result = f_write(&file, report, (UINT)length, &written);
    (void)f_close(&file);
    if ((result != FR_OK) || (written != (UINT)length))
    {
        g_debug.save_error = PRISM_DEBUG_SAVE_WRITE;
        return 0U;
    }
    return 1U;
}

void prism_debug_boot_service(void)
{
    debug_capture_post_test_codec();
    if (g_debug.state == PRISM_DEBUG_STATE_SAI_RESTARTING)
    {
        mixer_set_master(0.0f);
        brick6_audio_runtime_set_diagnostic_hold(1U);
        board_audio_get_codec_post_test_snapshot(&g_debug.codec_before_restart);
        g_debug.restart_completed = audio_restart_stream(&g_debug.restart_diag);
        board_audio_get_codec_post_test_snapshot(&g_debug.codec_after_restart);
        if (g_debug.restart_completed == 0U)
        {
            brick6_audio_runtime_set_diagnostic_hold(0U);
            g_debug.state = PRISM_DEBUG_STATE_SAVING;
            return;
        }
        debug_configure_tracks();
        g_debug.retest_mode = 1U;
        g_debug.state = PRISM_DEBUG_STATE_ARMED;
        prism_debug_boot_start_test();
        brick6_audio_runtime_set_diagnostic_hold(0U);
        return;
    }
    if (g_debug.state != PRISM_DEBUG_STATE_SAVING) return;
    if (sd_access_gate_try_acquire(PRISM_DEBUG_REPORT_CLIENT) == 0U) return;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        g_debug.save_error = PRISM_DEBUG_SAVE_MOUNT;
        g_debug.state = PRISM_DEBUG_STATE_ERROR;
        sd_access_gate_release(PRISM_DEBUG_REPORT_CLIENT);
        return;
    }
    const FRESULT directory_result = f_mkdir("0:/PRISM_HW_DEBUG");
    if ((directory_result != FR_OK) && (directory_result != FR_EXIST))
    {
        g_debug.save_error = PRISM_DEBUG_SAVE_DIRECTORY;
        g_debug.state = PRISM_DEBUG_STATE_ERROR;
        sd_access_gate_release(PRISM_DEBUG_REPORT_CLIENT);
        return;
    }
    const char *const suffix = (g_debug.initial_verdict != 0U) ? "GOOD"
        : ((g_debug.restart_completed == 0U) ? "BAD_SAI_RESTART_FAILED"
        : ((g_debug.retest_verdict != 0U) ? "BAD_THEN_SAI_RESTART_GOOD"
                                          : "BAD_THEN_SAI_RESTART_BAD"));
    g_debug.test_index = debug_find_test_index(suffix);
    (void)snprintf(g_debug.path, sizeof(g_debug.path),
                   "0:/PRISM_HW_DEBUG/TEST_%04lu_%s.txt",
                   (unsigned long)g_debug.test_index, suffix);
    g_debug.state = (debug_write_report() != 0U)
        ? PRISM_DEBUG_STATE_DONE : PRISM_DEBUG_STATE_ERROR;
    sd_access_gate_release(PRISM_DEBUG_REPORT_CLIENT);
}

void prism_debug_boot_render(void)
{
    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, "PRISM HW TEST");
    drv_display_draw_text(0U, 12U, debug_state_label());
    if ((g_debug.state == PRISM_DEBUG_STATE_RUNNING_INITIAL)
            || (g_debug.state == PRISM_DEBUG_STATE_RUNNING_RETEST))
    {
        const prism_debug_window_t *const window = debug_active_window();
        char text[24];
        (void)snprintf(text, sizeof(text), "%lu/%u FRAMES",
                       (unsigned long)window->test_frames, PRISM_DEBUG_TEST_FRAMES);
        drv_display_draw_text(0U, 22U, text);
    }
    else if (g_debug.state == PRISM_DEBUG_STATE_INITIAL_COMPLETE)
    {
        drv_display_draw_text(0U, 22U, "INITIAL: GOOD/BAD");
    }
    else if (g_debug.state == PRISM_DEBUG_STATE_SAI_RESTARTING)
    {
        drv_display_draw_text(0U, 22U, "INITIAL: BAD");
        drv_display_draw_text(0U, 32U, "SAI/DMA RESTART");
        drv_display_draw_text(0U, 42U, "RETEST 6S");
    }
    else if (g_debug.state == PRISM_DEBUG_STATE_RETEST_COMPLETE)
    {
        drv_display_draw_text(0U, 22U, "AFTER SAI: GOOD/BAD");
    }
    else if ((g_debug.state == PRISM_DEBUG_STATE_ERROR)
             || (g_debug.state == PRISM_DEBUG_STATE_DONE))
    {
        drv_display_draw_text(0U, 22U, debug_save_error_label());
    }
    drv_display_draw_text(0U, 34U, "GOOD: PAGE1");
    drv_display_draw_text(0U, 44U, "BAD: PAGE2");
    if ((g_debug.state == PRISM_DEBUG_STATE_INITIAL_COMPLETE)
            || (g_debug.state == PRISM_DEBUG_STATE_RETEST_COMPLETE))
        drv_display_draw_text(0U, 54U, "CHOOSE VERDICT");
    else if (g_debug.state == PRISM_DEBUG_STATE_SAVING)
        drv_display_draw_text(0U, 54U, "SAVING...");
    else if (g_debug.state == PRISM_DEBUG_STATE_DONE)
        drv_display_draw_text(0U, 54U, "SAVE OK");
    else if (g_debug.state == PRISM_DEBUG_STATE_ERROR)
        drv_display_draw_text(0U, 54U, "SAVE ERROR");
}

#else

void prism_debug_boot_init(void) {}
void prism_debug_boot_start_test(void) {}
void prism_debug_boot_service(void) {}
uint8_t prism_debug_boot_is_active(void) { return 0U; }
void prism_debug_boot_audio_block_begin(uint32_t frames) { (void)frames; }
void prism_debug_boot_audio_half_complete(uint8_t half_index, uint32_t frames)
{
    (void)half_index;
    (void)frames;
}
uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta)
{
    (void)encoder;
    (void)delta;
    return 0U;
}
uint8_t prism_debug_boot_handle_event(const ui_event_t *event)
{
    (void)event;
    return 0U;
}
void prism_debug_boot_render(void) {}

#endif
