#include "Core/prism_debug_boot.h"

#if BRICK6_PRISM_DEBUG_BOOT

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "Audio/audio.h"
#include "Audio/audio_track_diag.h"
#include "Audio/fx_pool.h"
#include "Audio/metronome_runtime.h"
#include "Audio/mixer.h"
#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Core/brick6_braids_runtime.h"
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
#define PRISM_DEBUG_REPORT_MAX 8192U
#define PRISM_DEBUG_NOTE_BASE 48U

#if defined(PRISM_DEBUG_TX_NONCACHEABLE)
#define PRISM_DEBUG_TEST_VARIANT "TX_NONCACHEABLE"
#define PRISM_DEBUG_TX_MPU_REGION "region=1 base=0x30000000 size=32KB subregion_disable=0xF8 covered=12KB TEX=LEVEL1 access=FULL disable_exec=1 shareable=1 cacheable=0 bufferable=0"
#else
#define PRISM_DEBUG_TEST_VARIANT "TX_CACHEABLE"
#define PRISM_DEBUG_TX_MPU_REGION "TX in D2 LUT cacheable default region; no temporary TX MPU override"
#endif

typedef enum
{
    PRISM_DEBUG_STATE_ARMED = 0,
    PRISM_DEBUG_STATE_RUNNING,
    PRISM_DEBUG_STATE_COMPLETE,
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
    volatile prism_debug_state_t state;
    volatile uint8_t verdict;
    uint8_t notes[PRISM_DEBUG_TRACK_COUNT];
    uint8_t instance_id[PRISM_DEBUG_TRACK_COUNT];
    uint8_t voice_slot[PRISM_DEBUG_TRACK_COUNT];
    uint8_t mix_track_id[PRISM_DEBUG_TRACK_COUNT];
    uintptr_t voice_state_address[PRISM_DEBUG_TRACK_COUNT];
    uint32_t test_index;
    uint32_t boot_index;
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t test_frames;
    uint32_t audio_blocks;
    uint32_t max_frames_per_block;
    prism_debug_save_error_t save_error;
    uint8_t codec_post_test_pending;
    audio_runtime_diag_t audio_diag;
    board_audio_boot_diag_t boot_diag;
    board_audio_runtime_diag_t board_diag;
    board_audio_codec_snapshot_t codec_diag;
    char path[PRISM_DEBUG_PATH_MAX];
} prism_debug_state_data_t;

STORAGE_STATE_SDRAM static prism_debug_state_data_t g_debug;

static const uint8_t g_debug_notes[PRISM_DEBUG_TRACK_COUNT] = {
    PRISM_DEBUG_NOTE_BASE, 52U, 55U, 59U, 62U, 67U, 71U, 74U
};

static const char *const g_codec_reg_names[BOARD_AUDIO_CODEC_REG_COUNT_SENTINEL] = {
    "interface",
    "clock_0", "clock_1", "clock_2", "clock_3", "clock_4", "clock_5", "clock_6", "clock_7",
    "dac_state", "mute", "route_l", "route_r", "output_power",
    "digital_volume_l", "digital_volume_r", "analog_volume_l", "analog_volume_r",
    "status", "status_mask", "functional_mode", "chip_id"
};

static const char *debug_state_label(void)
{
    switch (g_debug.state)
    {
        case PRISM_DEBUG_STATE_ARMED: return "WAIT START";
        case PRISM_DEBUG_STATE_RUNNING: return "RUNNING";
        case PRISM_DEBUG_STATE_COMPLETE: return "TEST DONE";
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

static void debug_configure_tracks(void)
{
    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t channel[UI_TRACK_COUNT];
    uint8_t source[UI_TRACK_COUNT];

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

    mixer_set_master(1.0f);
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

static void debug_capture_frozen_diagnostics(void)
{
    audio_runtime_diag_snapshot(&g_debug.audio_diag);
    board_audio_get_boot_diag(&g_debug.boot_diag);
    board_audio_get_runtime_diag(&g_debug.board_diag);
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

static void debug_complete_test(uint32_t frames)
{
    if (g_debug.state != PRISM_DEBUG_STATE_RUNNING) return;
    g_debug.test_frames += frames;
    if (g_debug.test_frames < PRISM_DEBUG_TEST_FRAMES) return;
    g_debug.test_frames = PRISM_DEBUG_TEST_FRAMES;
    g_debug.end_tick = HAL_GetTick();
    /* Freeze the exact test window before any note-off or save-side work. */
    debug_capture_frozen_diagnostics();
    g_debug.codec_post_test_pending = 1U;
    debug_stop_scenario();
    g_debug.state = PRISM_DEBUG_STATE_COMPLETE;
}

void prism_debug_boot_init(void)
{
    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.state = PRISM_DEBUG_STATE_ARMED;
    g_debug.boot_index = 1U;
    g_debug.save_error = PRISM_DEBUG_SAVE_NONE;
    debug_configure_tracks();
}

void prism_debug_boot_start_test(void)
{
    if (g_debug.state != PRISM_DEBUG_STATE_ARMED) return;
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
    /* The first block counted by the RUNNING state is the test boundary. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    audio_runtime_diag_reset_for_test();
    g_debug.test_frames = 0U;
    g_debug.audio_blocks = 0U;
    g_debug.max_frames_per_block = 0U;
    g_debug.start_tick = HAL_GetTick();
    g_debug.state = PRISM_DEBUG_STATE_RUNNING;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t prism_debug_boot_is_active(void)
{
    return 1U;
}

void prism_debug_boot_audio_block_begin(uint32_t frames)
{
    if (g_debug.state != PRISM_DEBUG_STATE_RUNNING) return;
    g_debug.audio_blocks++;
    if (frames > g_debug.max_frames_per_block)
        g_debug.max_frames_per_block = frames;
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

uint8_t prism_debug_boot_handle_event(const ui_event_t *event)
{
    if ((event == NULL) || (event->type != UI_EVENT_BUTTON_PRESS)) return 1U;
    if (g_debug.state != PRISM_DEBUG_STATE_COMPLETE) return 1U;
    if (g_debug.codec_post_test_pending != 0U)
    {
        board_audio_get_codec_post_test_snapshot(&g_debug.codec_diag);
        g_debug.codec_post_test_pending = 0U;
    }
    if (event->id == (uint8_t)BTN_PAGE_1)
    {
        g_debug.verdict = 1U;
        g_debug.state = PRISM_DEBUG_STATE_SAVING;
    }
    else if (event->id == (uint8_t)BTN_PAGE_2)
    {
        g_debug.verdict = 0U;
        g_debug.state = PRISM_DEBUG_STATE_SAVING;
    }
    return 1U;
}

static void debug_capture_post_test_codec(void)
{
    if ((g_debug.state == PRISM_DEBUG_STATE_COMPLETE)
            && (g_debug.codec_post_test_pending != 0U))
    {
        board_audio_get_codec_post_test_snapshot(&g_debug.codec_diag);
        g_debug.codec_post_test_pending = 0U;
    }
}

static uint32_t debug_find_test_index(const char *verdict)
{
    FILINFO info;
    char path[PRISM_DEBUG_PATH_MAX];
    for (uint32_t index = 1U; index < 10000U; ++index)
    {
        (void)snprintf(path, sizeof(path), "0:/PRISM_HW_DEBUG/TEST_%04lu_%s.txt",
                       (unsigned long)index, verdict);
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

static const char *debug_last_callback(void)
{
    if (g_debug.audio_diag.last_callback == 1U) return "HALF";
    if (g_debug.audio_diag.last_callback == 2U) return "FULL";
    return "NONE";
}

static uint8_t debug_write_report(void)
{
    const char *verdict = (g_debug.verdict != 0U) ? "GOOD" : "BAD";
    const uint32_t elapsed_ms = g_debug.end_tick - g_debug.start_tick;
    char report[PRISM_DEBUG_REPORT_MAX];
    int length = snprintf(report, sizeof(report),
        "verdict = %s\ntest_index = %lu\nboot_index = %lu\n"
        "test_variant = %s\n"
        "test_state = COMPLETE\nduration_frames = %lu\n"
        "duration_audio_ms = %u\nduration_wall_ms = %lu\n"
        "sample_rate = %u\nframes_per_block = %u\nmax_frames_per_block = %lu\n"
        "audio_blocks = %lu\ntracks = 8\nosc1 = ON\nosc2 = OFF\n"
        "filter = LP_BI\nfilter_cutoff_ui = 0\nfilter_cutoff_hz = 20\n",
        verdict, (unsigned long)g_debug.test_index, (unsigned long)g_debug.boot_index,
        PRISM_DEBUG_TEST_VARIANT,
        (unsigned long)g_debug.test_frames, PRISM_DEBUG_TEST_SECONDS * 1000U,
        (unsigned long)elapsed_ms,
        PRISM_DEBUG_SAMPLE_RATE, AUDIO_BLOCK_SIZE,
        (unsigned long)g_debug.max_frames_per_block,
        (unsigned long)g_debug.audio_blocks);

    length = debug_append(report, sizeof(report), length,
        "tx_buffer_address = 0x%08lX\ntx_buffer_bytes = %lu\n"
        "tx_half_0_address = 0x%08lX\ntx_half_1_address = 0x%08lX\n"
        "tx_half_bytes = %lu\ndma_word_count = %lu\n"
        "tx_buffer_cacheable = %u\ntx_buffer_region = %s\n"
        "tx_buffer_alignment = 32\nformat = PCM24 right-aligned in int32\n"
        "tdm_slots = %u\nframes_per_dma_half = %u\n",
        (unsigned long)g_debug.audio_diag.tx_buffer_address,
        (unsigned long)g_debug.audio_diag.tx_buffer_bytes,
        (unsigned long)g_debug.audio_diag.tx_half_address[0],
        (unsigned long)g_debug.audio_diag.tx_half_address[1],
        (unsigned long)g_debug.audio_diag.tx_half_bytes,
        (unsigned long)g_debug.audio_diag.dma_word_count,
        (unsigned)g_debug.audio_diag.tx_cacheable,
        (g_debug.audio_diag.tx_cacheable != 0U) ? ".ram_d2_lut" : ".ram_d2_dma",
        BOARD_AUDIO_TDM_SLOTS, BOARD_AUDIO_FRAMES_PER_HALF);

    length = debug_append(report, sizeof(report), length,
        "hal_sai_transmit_dma_word_count = %lu\n"
        "half_callbacks = %lu\nfull_callbacks = %lu\nlast_callback = %s\n"
        "test_callback_total = %lu\nexpected_audio_blocks = %u\n"
        "expected_callback_total = %u\n"
        "callback_alternation_errors = %lu\ndma_error_callbacks = %lu\n"
        "sai_error_callbacks = %lu\nunderrun_callbacks = %lu\n"
        "last_dma_error_code = 0x%08lX\n"
        "last_sai_error_code = 0x%08lX\nunderrun_flags = 0x%08lX\n"
        "fill_half_0 = %lu\nfill_half_1 = %lu\nwrong_half_writes = %lu\n"
        "half_not_ready = %lu\nlate_fills = %lu\nmax_fill_cycles = %lu\n",
        (unsigned long)g_debug.audio_diag.dma_word_count,
        (unsigned long)g_debug.audio_diag.half_callbacks,
        (unsigned long)g_debug.audio_diag.full_callbacks,
        debug_last_callback(),
        (unsigned long)(g_debug.audio_diag.half_callbacks + g_debug.audio_diag.full_callbacks),
        PRISM_DEBUG_TEST_FRAMES / AUDIO_BLOCK_SIZE,
        PRISM_DEBUG_TEST_FRAMES / BOARD_AUDIO_FRAMES_PER_HALF,
        (unsigned long)g_debug.audio_diag.callback_alternation_errors,
        (unsigned long)g_debug.audio_diag.dma_error_callbacks,
        (unsigned long)g_debug.audio_diag.sai_error_callbacks,
        (unsigned long)g_debug.audio_diag.underrun_callbacks,
        (unsigned long)g_debug.audio_diag.last_dma_error_code,
        (unsigned long)g_debug.audio_diag.last_sai_error_code,
        (unsigned long)(g_debug.audio_diag.last_sai_error_code & HAL_SAI_ERROR_UDR),
        (unsigned long)g_debug.audio_diag.fill_count[0],
        (unsigned long)g_debug.audio_diag.fill_count[1],
        (unsigned long)g_debug.audio_diag.wrong_half_writes,
        (unsigned long)g_debug.audio_diag.half_not_ready,
        (unsigned long)g_debug.audio_diag.late_fills,
        (unsigned long)g_debug.audio_diag.max_fill_cycles);

    length = debug_append(report, sizeof(report), length,
        "codec_ready = %u\ncodec_reset_ok = %u\ncodec_clocks_ok = %u\n"
        "codec_interface_ok = %u\ndac_powered = %u\ndac_routed = %u\n"
        "dac_unmuted = %u\noutput_routed = %u\noutput_powered = %u\n"
        "output_unmuted = %u\ncodec_volume_ok = %u\ncodec_stage = %u\n"
        "codec_page = %u\ncodec_reg = %u\ncodec_expected = %u\n"
        "codec_mask = %u\ncodec_actual = %u\n"
        "codec_boot_expected_stage = %u\ncodec_boot_expected_reg = %u\n"
        "codec_boot_expected_value = %u\ncodec_register_snapshot = POST_TEST_READ\n"
        "sai_tx_state = %lu\nsai_rx_state = %lu\nsai_tx_error = 0x%08lX\n"
        "sai_rx_error = 0x%08lX\ndma_tx_state = %lu\ndma_rx_state = %lu\n"
        "dma_tx_error = 0x%08lX\ndma_rx_error = 0x%08lX\n"
        "sai_frame_length = %lu\nsai_active_frame_length = %lu\n"
        "sai_data_size = %lu\nsai_slot_size = %lu\nsai_slot_number = %lu\n"
        "sai_slot_active = 0x%08lX\n",
        (unsigned)g_debug.boot_diag.codec_ready,
        (unsigned)g_debug.boot_diag.reset_ok,
        (unsigned)g_debug.boot_diag.clocks_ok,
        (unsigned)g_debug.boot_diag.interface_ok,
        (unsigned)g_debug.boot_diag.dac_powered,
        (unsigned)g_debug.boot_diag.dac_routed,
        (unsigned)g_debug.boot_diag.dac_unmuted,
        (unsigned)g_debug.boot_diag.output_routed,
        (unsigned)g_debug.boot_diag.output_powered,
        (unsigned)g_debug.boot_diag.output_unmuted,
        (unsigned)g_debug.boot_diag.volume_ok,
        (unsigned)g_debug.boot_diag.codec_stage,
        (unsigned)g_debug.boot_diag.codec_page,
        (unsigned)g_debug.boot_diag.codec_reg,
        (unsigned)g_debug.boot_diag.codec_expected,
        (unsigned)g_debug.boot_diag.codec_mask,
        (unsigned)g_debug.boot_diag.codec_actual,
        (unsigned)g_debug.boot_diag.codec_stage,
        (unsigned)g_debug.boot_diag.codec_reg,
        (unsigned)g_debug.boot_diag.codec_expected,
        (unsigned long)g_debug.board_diag.tx_sai_state,
        (unsigned long)g_debug.board_diag.rx_sai_state,
        (unsigned long)g_debug.board_diag.tx_sai_error_code,
        (unsigned long)g_debug.board_diag.rx_sai_error_code,
        (unsigned long)g_debug.board_diag.tx_dma_state,
        (unsigned long)g_debug.board_diag.rx_dma_state,
        (unsigned long)g_debug.board_diag.tx_dma_error_code,
        (unsigned long)g_debug.board_diag.rx_dma_error_code,
        (unsigned long)g_debug.board_diag.frame_length,
        (unsigned long)g_debug.board_diag.active_frame_length,
        (unsigned long)g_debug.board_diag.data_size,
        (unsigned long)g_debug.board_diag.slot_size,
        (unsigned long)g_debug.board_diag.slot_number,
        (unsigned long)g_debug.board_diag.slot_active);

    length = debug_append(report, sizeof(report), length,
        "rx_buffer_cacheable = 1\nrx_cache_invalidate_active = 1\n"
        "tx_cache_clean_active = %u\ncache_maintenance_active = %u\nmpu_region = %s\n"
        "notes = %u,%u,%u,%u,%u,%u,%u,%u\n",
        (unsigned)g_debug.audio_diag.tx_cacheable,
        (unsigned)g_debug.audio_diag.cache_maintenance_active,
        PRISM_DEBUG_TX_MPU_REGION,
        g_debug.notes[0], g_debug.notes[1], g_debug.notes[2], g_debug.notes[3],
        g_debug.notes[4], g_debug.notes[5], g_debug.notes[6], g_debug.notes[7]);

    length = debug_append(report, sizeof(report), length,
        "codec_post_test_read_ok = %u\ncodec_post_test_i2c_error = %u\n",
        (unsigned)g_debug.codec_diag.read_ok,
        (unsigned)g_debug.codec_diag.i2c_error);
    for (uint8_t id = 0U; id < (uint8_t)BOARD_AUDIO_CODEC_REG_COUNT_SENTINEL; ++id)
    {
        const uint32_t bit = 1UL << id;
        if ((g_debug.codec_diag.valid_mask & bit) != 0U)
        {
            length = debug_append(report, sizeof(report), length,
                "codec_boot_expected_%s = 0x%02X\n"
                "codec_post_test_actual_%s = 0x%02X\n",
                g_codec_reg_names[id], (unsigned)g_debug.codec_diag.expected[id],
                g_codec_reg_names[id], (unsigned)g_debug.codec_diag.actual[id]);
        }
        else
        {
            length = debug_append(report, sizeof(report), length,
                "codec_boot_expected_%s = 0x%02X\n"
                "codec_post_test_actual_%s = NA\n",
                g_codec_reg_names[id], (unsigned)g_debug.codec_diag.expected[id],
                g_codec_reg_names[id]);
        }
    }

    for (uint8_t track = 0U; track < PRISM_DEBUG_TRACK_COUNT; ++track)
    {
        length = debug_append(report, sizeof(report), length,
            "track_%u = note:%u instance:%u voice_slot:%u mix_track:%u voice_state:0x%08lX\n",
            (unsigned)(track + 1U), (unsigned)g_debug.notes[track],
            (unsigned)g_debug.instance_id[track], (unsigned)g_debug.voice_slot[track],
            (unsigned)g_debug.mix_track_id[track],
            (unsigned long)g_debug.voice_state_address[track]);
    }

    FIL file;
    UINT written = 0U;
    if ((length <= 0) || ((size_t)length >= sizeof(report))) return 0U;
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
    if (g_debug.state != PRISM_DEBUG_STATE_SAVING) return;
    if (sd_access_gate_try_acquire(PRISM_DEBUG_REPORT_CLIENT) == 0U) return;

    const uint8_t mounted = sd_access_fs_mount_if_needed();
    if (mounted == 0U)
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

    const char *verdict = (g_debug.verdict != 0U) ? "GOOD" : "BAD";
    g_debug.test_index = debug_find_test_index(verdict);
    (void)snprintf(g_debug.path, sizeof(g_debug.path),
                   "0:/PRISM_HW_DEBUG/TEST_%04lu_%s.txt",
                   (unsigned long)g_debug.test_index, verdict);
    g_debug.state = (debug_write_report() != 0U)
        ? PRISM_DEBUG_STATE_DONE : PRISM_DEBUG_STATE_ERROR;
    sd_access_gate_release(PRISM_DEBUG_REPORT_CLIENT);
}

void prism_debug_boot_render(void)
{
    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, "PRISM HW TEST");
    drv_display_draw_text(0U, 12U, debug_state_label());
    if (g_debug.state == PRISM_DEBUG_STATE_RUNNING)
    {
        char text[24];
        (void)snprintf(text, sizeof(text), "%lu/%u FRAMES",
                       (unsigned long)g_debug.test_frames, PRISM_DEBUG_TEST_FRAMES);
        drv_display_draw_text(0U, 22U, text);
    }
    else if ((g_debug.state == PRISM_DEBUG_STATE_ERROR)
             || (g_debug.state == PRISM_DEBUG_STATE_DONE))
    {
        drv_display_draw_text(0U, 22U, debug_save_error_label());
    }
    drv_display_draw_text(0U, 34U, "GOOD: PAGE1");
    drv_display_draw_text(0U, 44U, "BAD: PAGE2");
    if (g_debug.state == PRISM_DEBUG_STATE_COMPLETE)
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
