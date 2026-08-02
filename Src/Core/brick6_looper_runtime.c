#include "Core/brick6_looper_runtime.h"
#include "Core/brick_build_config.h"

#include "Audio/audio_float.h"
#include "Core/brick6_clip_shifter.h"
#include "Sampler/sample_page_cache.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_exec.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_access_gate.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <string.h>

#define BRICK6_LOOPER_TRACK_CAP SEQ_TRACK_COUNT
#define BRICK6_LOOPER_SHIFTER_CAP BRICK6_LOOPER_SHIFTER_SLOT_CAP
#define BRICK6_LOOPER_SHIFTER_SLOT_NONE 0xFFU
#define BRICK6_LOOPER_CACHE_ID_BASE SAMPLE_PAGE_CACHE_LOOPER_ID_BASE
#define BRICK6_LOOPER_PREFETCH_PAGES 12U
#define BRICK6_LOOPER_PREROLL_FRAMES (MULTI_RECORD_WRITER_SAMPLE_RATE_HZ / 4U)
#define BRICK6_LOOPER_PREROLL_CHANNELS MULTI_RECORD_WRITER_CHANNELS
#define BRICK6_LOOPER_PCM24_FLOAT_SCALE (1.0f / 8388607.0f)
#define BRICK6_LOOPER_RESCHEDULE_GUARD_FRAMES 64U
#define BRICK6_LOOPER_LEN_STEPS_PER_BAR 16U
#define BRICK6_LOOPER_Q16_ONE 65536U
#define BRICK6_LOOPER_STRETCH_OFF 0U
#define BRICK6_LOOPER_STRETCH_SPEED 1U
#define BRICK6_LOOPER_STRETCH_SHIFTER 2U
#define BRICK6_LOOPER_DEFAULT_GRAIN_FRAMES 1536U
#define BRICK6_LOOPER_PITCH_STABLE_EPSILON 0.001f
#define BRICK6_LOOPER_TIMING_NEUTRAL_EPSILON 0.001f
#define BRICK6_LOOPER_RESYNC_STABLE_BLOCKS 2U
#define BRICK6_LOOPER_RESYNC_XFADE_FRAMES 96U

#if ((BRICK6_LOOPER_CACHE_ID_BASE + BRICK6_LOOPER_TRACK_CAP) > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "sample_page_cache transient id range is too small for Looper tracks"
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((SAMPLE_PAGE_CACHE_LOOPER_ID_BASE + SEQ_TRACK_COUNT) <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "Looper transient ids must fit in page-cache id capacity");
#endif

typedef struct
{
    brick6_looper_runtime_state_t state;
    char path[MULTI_RECORD_WRITER_PATH_MAX];
    uint16_t cache_id;
    uint32_t frames_total;
    uint32_t recorded_steps_q16;
    uint32_t source_samples_per_step_q16;
    uint32_t source_bpm_milli;
    uint64_t record_start_sample;
    uint64_t record_stop_sample;
    uint64_t playback_start_sample;
    volatile uint32_t playhead;
    uint32_t playhead_frac_q16;
    uint32_t resync_old_playhead;
    uint32_t resync_old_frac_q16;
    sample_audio_key_t cache_key;
    sample_page_ref_t current_page_ref;
    const float *current_base;
    uint32_t current_start_frame;
    uint32_t current_frame_count;
    float stretch_pitch_semitones;
    uint16_t stretch_grain_frames;
    uint8_t stretch_mode;
    uint8_t play_auto;
    uint8_t want_play_when_ready;
    uint8_t cache_registered;
    uint8_t current_acquired;
    uint8_t scheduled_start_valid;
    uint8_t is_raw;
    uint8_t raw_slot;
    uint8_t resync_pending;
    uint8_t resync_stable_blocks;
    uint8_t resync_xfade_remaining;
    uint8_t preroll_valid;
    uint8_t preroll_used_reported;
    uint8_t preroll_consumed;
    uint8_t raw_relay_done;
    uint32_t preroll_frames;
    uint64_t scheduled_start_sample;
} brick6_looper_track_state_t;

typedef struct
{
    volatile uint32_t frames;
    volatile uint8_t active;
    uint8_t track_id;
    uint8_t raw_slot;
    uint8_t reserved;
} brick6_looper_preroll_state_t;

typedef struct
{
    uint8_t start_armed;
    uint8_t stop_armed;
    uint8_t recording;
    uint8_t track_id;
    uint8_t raw_slot;
    uint8_t len_mode;
    uint8_t play_auto;
    uint8_t reserved;
    uint32_t expected_frames;
    uint32_t expected_steps_q16;
    uint32_t source_samples_per_step_q16;
    uint32_t source_bpm_milli;
    uint64_t request_start_sample;
    uint64_t request_stop_sample;
    uint64_t actual_start_sample;
    uint64_t target_stop_sample;
} brick6_looper_record_boundary_state_t;

typedef struct
{
    brick6_clip_shifter_t shifter;
    uint8_t owner_track_id;
    uint8_t last_mode;
    uint16_t last_grain_frames;
} brick6_looper_shifter_slot_t;

static AUDIO_HOT brick6_looper_track_state_t g_looper_tracks[BRICK6_LOOPER_TRACK_CAP];
static brick6_looper_runtime_diag_snapshot_t g_looper_runtime_diag;
SDRAM_RECORDER static int32_t
    g_looper_preroll_pcm[BRICK6_LOOPER_PREROLL_FRAMES * BRICK6_LOOPER_PREROLL_CHANNELS];
static brick6_looper_shifter_slot_t g_looper_shifter_slots[BRICK6_LOOPER_SHIFTER_CAP];
static float g_looper_stretch_l[AUDIO_BLOCK_SIZE];
static float g_looper_stretch_r[AUDIO_BLOCK_SIZE];
static brick6_looper_preroll_state_t g_looper_preroll;
static brick6_looper_record_boundary_state_t g_looper_record_boundary;

static void looper_request_playhead_pages(const brick6_looper_track_state_t *state);
static uint8_t looper_preroll_can_read(const brick6_looper_track_state_t *state,
                                       uint32_t playhead);
static uint8_t looper_try_enter_normal_playback(brick6_looper_track_state_t *state);

static uint32_t looper_len_mode_to_steps(uint8_t len_mode)
{
    uint32_t bars = 0U;

    switch(len_mode)
    {
        case 1U:
            bars = 1U;
            break;
        case 2U:
            bars = 2U;
            break;
        case 3U:
            bars = 4U;
            break;
        case 4U:
            bars = 8U;
            break;
        case 5U:
            bars = 16U;
            break;
        default:
            return 0U;
    }

    return bars * BRICK6_LOOPER_LEN_STEPS_PER_BAR;
}

static uint32_t looper_span_frames_to_steps_q16(uint32_t span_frames,
                                                uint32_t samples_per_step_q16)
{
    if((span_frames == 0U) || (samples_per_step_q16 == 0U))
    {
        return 0U;
    }

    const uint64_t steps_q16 =
        (((uint64_t)span_frames << 32) + ((uint64_t)samples_per_step_q16 >> 1))
        / (uint64_t)samples_per_step_q16;
    return (steps_q16 > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)steps_q16;
}

static void looper_store_take_metadata(brick6_looper_track_state_t *state,
                                       uint32_t recorded_frames,
                                       uint32_t recorded_steps_q16,
                                       uint32_t source_samples_per_step_q16,
                                       uint32_t source_bpm_milli,
                                       uint64_t record_start_sample,
                                       uint64_t record_stop_sample)
{
    if(state == 0)
    {
        return;
    }

    state->frames_total = recorded_frames;
    state->recorded_steps_q16 = recorded_steps_q16;
    state->source_samples_per_step_q16 = source_samples_per_step_q16;
    state->source_bpm_milli = source_bpm_milli;
    state->record_start_sample = record_start_sample;
    state->record_stop_sample = record_stop_sample;
}

static uint8_t looper_track_valid(uint8_t track_id)
{
    return (track_id < BRICK6_LOOPER_TRACK_CAP) ? 1U : 0U;
}

static uint8_t looper_copy_path(char *dst, const char *src)
{
    if((dst == 0) || (src == 0) || (src[0] == '\0'))
        return 0U;

    for(uint32_t i = 0U; i < MULTI_RECORD_WRITER_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
            return 1U;
    }

    dst[0] = '\0';
    return 0U;
}

static void looper_diag_copy_path(char *dst, const char *src)
{
    if(dst == 0)
        return;

    dst[0] = '\0';
    if(src == 0)
        return;

    for(uint32_t i = 0U; i < BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
            return;
    }
    dst[BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX - 1U] = '\0';
}

static void looper_diag_update_take(uint8_t track_id,
                                    const brick6_looper_track_state_t *state)
{
    if((state == 0) || (track_id >= BRICK6_LOOPER_TRACK_CAP))
        return;

    g_looper_runtime_diag.scheduled_start_sample = state->scheduled_start_sample;
    g_looper_runtime_diag.playhead = state->playhead;
    g_looper_runtime_diag.recorded_frames = state->frames_total;
    g_looper_runtime_diag.recorded_steps_q16 = state->recorded_steps_q16;
    g_looper_runtime_diag.source_samples_per_step_q16 = state->source_samples_per_step_q16;
    g_looper_runtime_diag.source_bpm_milli = state->source_bpm_milli;
    g_looper_runtime_diag.current_page_start_frame = state->current_start_frame;
    g_looper_runtime_diag.current_page_frame_count = state->current_frame_count;
    g_looper_runtime_diag.record_start_sample = state->record_start_sample;
    g_looper_runtime_diag.record_stop_sample = state->record_stop_sample;
    g_looper_runtime_diag.cache_id = state->cache_id;
    g_looper_runtime_diag.track_id = track_id;
    g_looper_runtime_diag.raw_slot = state->raw_slot;
    g_looper_runtime_diag.play_auto = state->play_auto;
    g_looper_runtime_diag.scheduled_start_valid = state->scheduled_start_valid;
    g_looper_runtime_diag.state = (uint8_t)state->state;
    g_looper_runtime_diag.source =
        (state->is_raw != 0U) ? (uint8_t)BRICK6_LOOPER_RUNTIME_SOURCE_RAW
                              : (uint8_t)BRICK6_LOOPER_RUNTIME_SOURCE_NONE;
    g_looper_runtime_diag.cache_registered = state->cache_registered;
    looper_diag_copy_path(g_looper_runtime_diag.active_path, state->path);
}

static uint16_t looper_cache_id(uint8_t track_id)
{
    return (uint16_t)(BRICK6_LOOPER_CACHE_ID_BASE + track_id);
}

static sample_audio_key_t looper_cache_key(uint8_t track_id)
{
    return sample_audio_key_looper(track_id);
}

static float looper_pcm24_to_float(int32_t sample)
{
    return (float)sample * BRICK6_LOOPER_PCM24_FLOAT_SCALE;
}

static float looper_clampf(float value, float min_value, float max_value)
{
    if(value < min_value)
    {
        return min_value;
    }
    if(value > max_value)
    {
        return max_value;
    }
    return value;
}

static float looper_pitch_ratio(float semitones)
{
    semitones = looper_clampf(semitones, -12.0f, 12.0f);
    return looper_clampf(powf(2.0f, semitones * (1.0f / 12.0f)), 0.5f, 2.0f);
}

static float looper_timing_ratio(const brick6_looper_track_state_t *state,
                                 uint32_t current_samples_per_step_q16)
{
    if((state == 0)
            || (state->frames_total == 0U)
            || (state->recorded_steps_q16 == 0U)
            || (state->source_samples_per_step_q16 == 0U)
            || (current_samples_per_step_q16 == 0U))
    {
        return 1.0f;
    }

    return looper_clampf((float)state->source_samples_per_step_q16
                         / (float)current_samples_per_step_q16,
                         0.5f,
                         2.0f);
}

static uint8_t looper_metadata_valid(const brick6_looper_track_state_t *state,
                                     uint32_t current_samples_per_step_q16)
{
    return (uint8_t)(((state != 0)
            && (state->frames_total != 0U)
            && (state->recorded_steps_q16 != 0U)
            && (state->source_samples_per_step_q16 != 0U)
            && (current_samples_per_step_q16 != 0U)) ? 1U : 0U);
}

static int8_t looper_pitch_to_int(float pitch_semitones)
{
    pitch_semitones = looper_clampf(pitch_semitones, -12.0f, 12.0f);
    return (int8_t)((pitch_semitones >= 0.0f)
        ? (pitch_semitones + 0.5f)
        : (pitch_semitones - 0.5f));
}

static uint8_t looper_pitch_stable_ratio(float pitch_semitones, float *out_ratio)
{
    const int8_t pitch_int = looper_pitch_to_int(pitch_semitones);
    if((pitch_int != -12) && (pitch_int != 0) && (pitch_int != 12))
    {
        return 0U;
    }

    const float delta = pitch_semitones - (float)pitch_int;
    if((delta <= -BRICK6_LOOPER_PITCH_STABLE_EPSILON)
            || (delta >= BRICK6_LOOPER_PITCH_STABLE_EPSILON))
    {
        return 0U;
    }

    if(out_ratio != 0)
    {
        if(pitch_int == -12)
        {
            *out_ratio = 0.5f;
        }
        else if(pitch_int == 12)
        {
            *out_ratio = 2.0f;
        }
        else
        {
            *out_ratio = 1.0f;
        }
    }
    return 1U;
}

static uint8_t looper_pitch_is_zero(float pitch_semitones)
{
    return (looper_pitch_to_int(pitch_semitones) == 0) ? 1U : 0U;
}

static uint8_t looper_timing_is_neutral(float timing_ratio)
{
    const float delta = timing_ratio - 1.0f;
    return (uint8_t)(((delta > -BRICK6_LOOPER_TIMING_NEUTRAL_EPSILON)
            && (delta < BRICK6_LOOPER_TIMING_NEUTRAL_EPSILON)) ? 1U : 0U);
}

static uint8_t looper_effective_ratio_is_stable(const brick6_looper_track_state_t *state,
                                                 uint8_t mode,
                                                 float pitch_semitones,
                                                 uint32_t current_samples_per_step_q16,
                                                 float *out_ratio)
{
    float pitch_ratio = 1.0f;
    if(state == 0)
    {
        return 1U;
    }

    if(mode == BRICK6_LOOPER_STRETCH_SHIFTER)
    {
        return 0U;
    }

    if(looper_pitch_stable_ratio(pitch_semitones, &pitch_ratio) == 0U)
    {
        return 0U;
    }

    if(mode == BRICK6_LOOPER_STRETCH_SPEED)
    {
        pitch_ratio *= looper_timing_ratio(state, current_samples_per_step_q16);
    }

    if(out_ratio != 0)
    {
        *out_ratio = pitch_ratio;
    }
    return 1U;
}

static uint8_t looper_effective_mode(const brick6_looper_track_state_t *state,
                                     uint32_t current_samples_per_step_q16)
{
    if(state == 0)
    {
        return BRICK6_LOOPER_STRETCH_OFF;
    }

    if((state->stretch_mode == BRICK6_LOOPER_STRETCH_SPEED)
            || (state->stretch_mode == BRICK6_LOOPER_STRETCH_SHIFTER))
    {
        return (looper_metadata_valid(state, current_samples_per_step_q16) != 0U)
            ? state->stretch_mode
            : BRICK6_LOOPER_STRETCH_OFF;
    }

    return BRICK6_LOOPER_STRETCH_OFF;
}

static uint32_t looper_ratio_to_increment_q16(float ratio)
{
    ratio = looper_clampf(ratio, 0.25f, 4.0f);
    return (uint32_t)(ratio * (float)BRICK6_LOOPER_Q16_ONE + 0.5f);
}

static brick6_looper_shifter_slot_t *looper_shifter_get(uint8_t track_id)
{
    for(uint8_t i = 0U; i < BRICK6_LOOPER_SHIFTER_CAP; ++i)
    {
        if(g_looper_shifter_slots[i].owner_track_id == track_id)
        {
            return &g_looper_shifter_slots[i];
        }
    }
    return 0;
}

static brick6_looper_shifter_slot_t *looper_shifter_acquire(uint8_t track_id)
{
    brick6_looper_shifter_slot_t *slot = looper_shifter_get(track_id);
    if(slot != 0)
    {
        return slot;
    }

    for(uint8_t i = 0U; i < BRICK6_LOOPER_SHIFTER_CAP; ++i)
    {
        if(g_looper_shifter_slots[i].owner_track_id != BRICK6_LOOPER_SHIFTER_SLOT_NONE)
        {
            continue;
        }

        memset(&g_looper_shifter_slots[i], 0, sizeof(g_looper_shifter_slots[i]));
        g_looper_shifter_slots[i].owner_track_id = track_id;
        g_looper_shifter_slots[i].last_mode = 0xFFU;
        brick6_clip_shifter_init(&g_looper_shifter_slots[i].shifter);
        return &g_looper_shifter_slots[i];
    }

    return 0;
}

static void looper_shifter_release(uint8_t track_id)
{
    brick6_looper_shifter_slot_t *slot = looper_shifter_get(track_id);
    if(slot == 0)
    {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->owner_track_id = BRICK6_LOOPER_SHIFTER_SLOT_NONE;
}

static void looper_shifter_reset_if_needed(brick6_looper_shifter_slot_t *slot,
                                           uint8_t mode,
                                           uint16_t grain_frames)
{
    if(slot == 0)
    {
        return;
    }

    if((slot->last_mode != mode) || (slot->last_grain_frames != grain_frames))
    {
        brick6_clip_shifter_reset(&slot->shifter);
        slot->last_mode = mode;
        slot->last_grain_frames = grain_frames;
    }
}

static void looper_preroll_stop_capture(void)
{
    g_looper_preroll.active = 0U;
}

static void looper_preroll_reset_take_state(brick6_looper_track_state_t *state)
{
    if(state == 0)
        return;

    state->preroll_valid = 0U;
    state->preroll_used_reported = 0U;
    state->preroll_consumed = 0U;
    state->raw_relay_done = 0U;
    state->preroll_frames = 0U;
}

static void looper_adopt_preroll_take(brick6_looper_track_state_t *state,
                                      uint8_t track_id,
                                      uint8_t raw_slot,
                                      uint32_t frames_total)
{
    if(state == 0)
        return;

    looper_preroll_stop_capture();
    __DMB();
    state->preroll_frames = 0U;
    state->preroll_used_reported = 0U;
    state->preroll_valid = 0U;
    if((g_looper_preroll.track_id == track_id)
            && (g_looper_preroll.raw_slot == raw_slot)
            && (g_looper_preroll.frames != 0U)
            && (frames_total != 0U))
    {
        state->preroll_frames = g_looper_preroll.frames;
        if(state->preroll_frames > frames_total)
        {
            state->preroll_frames = frames_total;
        }
        state->preroll_valid = (state->preroll_frames != 0U) ? 1U : 0U;
    }
}

static void looper_release_reader(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->current_acquired == 0U)
            || (state->cache_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        return;
    }

    sample_page_cache_release_page_ref_key(state->cache_key, &state->current_page_ref);
    memset(&state->current_page_ref, 0, sizeof(state->current_page_ref));
    state->current_base = 0;
    state->current_start_frame = 0U;
    state->current_frame_count = 0U;
    state->current_acquired = 0U;
}

static void looper_clear_stream(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->cache_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
        return;

    looper_release_reader(state);
    sample_page_cache_clear_key(state->cache_key);
    state->cache_registered = 0U;
}

static void looper_reset_take_state(brick6_looper_track_state_t *state)
{
    if(state == 0)
        return;

    const uint8_t track_id = (uint8_t)(state - g_looper_tracks);
    looper_clear_stream(state);
    looper_shifter_release(track_id);
    state->path[0] = '\0';
    looper_store_take_metadata(state, 0U, 0U, 0U, 0U, 0U, 0U);
    state->playhead = 0U;
    state->playhead_frac_q16 = 0U;
    state->playback_start_sample = 0U;
    state->resync_pending = 0U;
    state->resync_stable_blocks = 0U;
    state->resync_xfade_remaining = 0U;
    state->resync_old_playhead = 0U;
    state->resync_old_frac_q16 = 0U;
    state->play_auto = 0U;
    state->want_play_when_ready = 0U;
    state->scheduled_start_valid = 0U;
    state->scheduled_start_sample = 0U;
    state->is_raw = 0U;
    state->raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
    looper_preroll_reset_take_state(state);
    state->state = BRICK6_LOOPER_RUNTIME_STATE_EMPTY;
}

static void looper_fail(brick6_looper_track_state_t *state)
{
    if(state == 0)
        return;

    looper_shifter_release((uint8_t)(state - g_looper_tracks));
    looper_clear_stream(state);
    state->state = BRICK6_LOOPER_RUNTIME_STATE_FAILED;
    looper_store_take_metadata(state, 0U, 0U, 0U, 0U, 0U, 0U);
    state->playhead = 0U;
    state->playhead_frac_q16 = 0U;
    state->playback_start_sample = 0U;
    state->resync_pending = 0U;
    state->resync_stable_blocks = 0U;
    state->resync_xfade_remaining = 0U;
    state->resync_old_playhead = 0U;
    state->resync_old_frac_q16 = 0U;
}

static uint8_t looper_register_raw_stream(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->cache_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            || (state->is_raw == 0U) || (state->frames_total == 0U))
    {
        return 0U;
    }

    looper_release_reader(state);
    if(sample_page_cache_register_raw_pcm24_stereo_sample_key(state->cache_key,
                                                              state->path,
                                                              state->frames_total) == 0U)
    {
        return 0U;
    }

    state->cache_registered = 1U;
    uint32_t page_count = (state->frames_total + SAMPLE_PAGE_FRAMES - 1U)
        / SAMPLE_PAGE_FRAMES;
    if(page_count > BRICK6_LOOPER_PREFETCH_PAGES)
    {
        page_count = BRICK6_LOOPER_PREFETCH_PAGES;
    }
    (void)sample_page_cache_request_start_pages_key_alloc(state->cache_key,
                                                          0U,
                                                          page_count,
                                                          SAMPLE_PAGE_ALLOC_MARGIN);
    looper_request_playhead_pages(state);
    return 1U;
}

static uint8_t looper_prepare_stream(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->cache_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
        return 0U;

    if(state->is_raw != 0U)
    {
        if(looper_register_raw_stream(state) == 0U)
        {
            return 0U;
        }

        state->playhead = 0U;
        state->state = BRICK6_LOOPER_RUNTIME_STATE_LOADING;
        if((state->want_play_when_ready != 0U)
                && (state->scheduled_start_valid != 0U)
                && (state->preroll_valid != 0U)
                && (state->preroll_frames != 0U))
        {
            const uint64_t now_sample = seq_runtime_exec_get_audio_timeline_sample();
            if(state->scheduled_start_sample < now_sample)
            {
                state->scheduled_start_sample =
                    now_sample + (uint64_t)BRICK6_LOOPER_RESCHEDULE_GUARD_FRAMES;
            }
            state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
        }
        looper_diag_update_take((uint8_t)(state - g_looper_tracks), state);
        return 1U;
    }

    return 0U;
}

static void looper_request_playhead_pages(const brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->cache_registered == 0U) || (state->frames_total == 0U))
        return;

    const uint32_t page_count =
        (state->frames_total + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES;
    if(page_count == 0U)
        return;

    const uint32_t playhead = state->playhead;
    const uint32_t first_page = playhead / SAMPLE_PAGE_FRAMES;
    for(uint32_t i = 0U; i < BRICK6_LOOPER_PREFETCH_PAGES; ++i)
    {
        (void)sample_page_cache_request_page_key_alloc(state->cache_key,
                                                       (first_page + i) % page_count,
                                                       SAMPLE_PAGE_ALLOC_MARGIN);
    }
}

static uint8_t looper_preroll_can_read(const brick6_looper_track_state_t *state,
                                       uint32_t playhead)
{
    if(state == 0)
    {
        return 0U;
    }

    return (uint8_t)(((state->preroll_valid != 0U)
            && (state->preroll_consumed == 0U)
            && (playhead < state->preroll_frames)) ? 1U : 0U);
}

static void looper_advance_playhead(brick6_looper_track_state_t *state, uint32_t frames)
{
    if((state == 0) || (state->frames_total == 0U) || (frames == 0U))
        return;

    uint32_t playhead = state->playhead + frames;
    if(playhead >= state->frames_total)
    {
        playhead %= state->frames_total;
        g_looper_runtime_diag.wrap_count++;
    }
    state->playhead = playhead;
}

static void looper_start_playback(brick6_looper_track_state_t *state,
                                  uint64_t start_sample,
                                  uint32_t initial_playhead)
{
    if((state == 0) || (state->frames_total == 0U))
        return;

    if(initial_playhead >= state->frames_total)
    {
        initial_playhead %= state->frames_total;
    }

    const uint8_t start_was_scheduled = state->scheduled_start_valid;
    const uint64_t start_scheduled_sample = state->scheduled_start_sample;
    looper_release_reader(state);
    state->scheduled_start_valid = 0U;
    state->scheduled_start_sample = 0U;
    state->state = BRICK6_LOOPER_RUNTIME_STATE_PLAYING;
    state->playhead = initial_playhead;
    state->playhead_frac_q16 = 0U;
    state->playback_start_sample = start_sample;
    state->resync_pending = 0U;
    state->resync_stable_blocks = 0U;
    state->resync_xfade_remaining = 0U;
    state->resync_old_playhead = initial_playhead;
    state->resync_old_frac_q16 = 0U;
    const uint8_t track_id = (uint8_t)(state - g_looper_tracks);
    brick6_looper_shifter_slot_t *const slot = looper_shifter_get(track_id);
    if(slot != 0)
    {
        brick6_clip_shifter_reset(&slot->shifter);
        slot->last_mode = 0xFFU;
    }
    looper_diag_update_take(track_id, state);
    g_looper_runtime_diag.scheduled_start_sample = start_scheduled_sample;
    g_looper_runtime_diag.actual_start_sample = start_sample;
    g_looper_runtime_diag.first_output_audio_timeline_sample = 0U;
    g_looper_runtime_diag.first_output_frame_offset = 0U;
    g_looper_runtime_diag.first_output_valid = 0U;
    g_looper_runtime_diag.page_miss_seen = 0U;
    g_looper_runtime_diag.start_playhead = initial_playhead;
    g_looper_runtime_diag.recorded_frames = state->frames_total;
    g_looper_runtime_diag.track_id = track_id;
    g_looper_runtime_diag.raw_slot = state->raw_slot;
    g_looper_runtime_diag.play_auto = state->play_auto;
    g_looper_runtime_diag.scheduled_start_valid = start_was_scheduled;
    looper_request_playhead_pages(state);
}

static uint8_t looper_acquire_page_for_frame(brick6_looper_track_state_t *state,
                                             uint32_t frame)
{
    if((state == 0) || (state->cache_registered == 0U)
            || (state->frames_total == 0U)
            || (state->cache_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        return 0U;
    }

    if(frame >= state->frames_total)
    {
        frame %= state->frames_total;
    }

    if((state->current_acquired != 0U)
            && (frame >= state->current_start_frame)
            && (frame < (state->current_start_frame + state->current_frame_count)))
    {
        return 1U;
    }

    looper_release_reader(state);

    sample_page_span_t span;
    if(sample_page_cache_try_acquire_page_key(state->cache_key,
                                              frame / SAMPLE_PAGE_FRAMES,
                                              &span) == 0U)
    {
        return 0U;
    }

    state->current_page_ref.page_index = span.page_index;
    state->current_page_ref.page_generation = span.page_generation;
    state->current_page_ref.slot_index = span.slot_index;
    state->current_page_ref.key = span.key;
    state->current_page_ref.format = span.format;
    state->current_page_ref.stride_floats = span.stride_floats;
    state->current_page_ref.frames_per_page = span.frames_per_page;
    state->current_page_ref.registration_epoch = span.registration_epoch;
    state->current_base = span.frames_interleaved;
    state->current_start_frame = span.start_frame;
    state->current_frame_count = span.frame_count;
    state->current_acquired = 1U;
    return 1U;
}

static uint8_t looper_acquire_current_page(brick6_looper_track_state_t *state)
{
    if(state == 0)
    {
        return 0U;
    }
    return looper_acquire_page_for_frame(state, state->playhead);
}

static uint8_t looper_try_enter_normal_playback(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->cache_registered == 0U) || (state->frames_total == 0U))
    {
        return 0U;
    }

    if(looper_acquire_current_page(state) == 0U)
    {
        return 0U;
    }

    state->raw_relay_done = 1U;
    state->preroll_consumed = 1U;
    return 1U;
}

static void looper_update_ready_state(brick6_looper_track_state_t *state)
{
    if((state == 0) || (state->state != BRICK6_LOOPER_RUNTIME_STATE_LOADING))
        return;

    if(sample_page_cache_get_page_state_key(state->cache_key, 0U) == SAMPLE_PAGE_READY)
    {
        state->playhead = 0U;
        if(state->want_play_when_ready == 0U)
        {
            state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
            looper_diag_update_take((uint8_t)(state - g_looper_tracks), state);
            return;
        }

        if(state->scheduled_start_valid != 0U)
        {
            const uint64_t now_sample = seq_runtime_exec_get_audio_timeline_sample();
            if(now_sample < state->scheduled_start_sample)
            {
                state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
                looper_diag_update_take((uint8_t)(state - g_looper_tracks), state);
                return;
            }

            state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
            state->scheduled_start_sample =
                now_sample + (uint64_t)BRICK6_LOOPER_RESCHEDULE_GUARD_FRAMES;
            looper_diag_update_take((uint8_t)(state - g_looper_tracks), state);
            return;
        }

        state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
        looper_diag_update_take((uint8_t)(state - g_looper_tracks), state);
    }
}

static void looper_record_clear_boundary_state(void)
{
    g_looper_record_boundary.start_armed = 0U;
    g_looper_record_boundary.stop_armed = 0U;
    g_looper_record_boundary.recording = 0U;
    g_looper_record_boundary.track_id = 0xFFU;
    g_looper_record_boundary.raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
    g_looper_record_boundary.len_mode = 0U;
    g_looper_record_boundary.play_auto = 0U;
    g_looper_record_boundary.expected_frames = 0U;
    g_looper_record_boundary.expected_steps_q16 = 0U;
    g_looper_record_boundary.source_samples_per_step_q16 = 0U;
    g_looper_record_boundary.source_bpm_milli = 0U;
    g_looper_record_boundary.request_start_sample = 0U;
    g_looper_record_boundary.request_stop_sample = 0U;
    g_looper_record_boundary.actual_start_sample = 0U;
    g_looper_record_boundary.target_stop_sample = 0U;
}

static void looper_record_stop_at_boundary(uint64_t sample_time)
{
    if(g_looper_record_boundary.recording == 0U)
        return;

    multi_record_writer_status_t status;
    if(multi_record_writer_get_status(0U, &status) == 0U)
        return;

    const uint8_t track = g_looper_record_boundary.track_id;
    const uint8_t raw_slot = status.raw_slot;
    const uint32_t span_frames =
        (sample_time > g_looper_record_boundary.actual_start_sample)
            ? (uint32_t)(sample_time - g_looper_record_boundary.actual_start_sample)
            : 0U;
    uint32_t recorded_steps_q16 = g_looper_record_boundary.expected_steps_q16;
    if(recorded_steps_q16 == 0U)
    {
        recorded_steps_q16 =
            looper_span_frames_to_steps_q16(span_frames,
                                            g_looper_record_boundary.source_samples_per_step_q16);
    }

    if(multi_record_writer_request_stop(0U) == 0U)
        return;

    if(track < BRICK6_LOOPER_TRACK_CAP)
    {
        looper_store_take_metadata(&g_looper_tracks[track],
                                   span_frames,
                                   recorded_steps_q16,
                                   g_looper_record_boundary.source_samples_per_step_q16,
                                   g_looper_record_boundary.source_bpm_milli,
                                   g_looper_record_boundary.actual_start_sample,
                                   sample_time);
        looper_diag_update_take(track, &g_looper_tracks[track]);
    }

    if((g_looper_record_boundary.play_auto != 0U)
            && (span_frames != 0U)
            && (raw_slot != MULTI_RECORD_WRITER_RAW_SLOT_NONE))
    {
        brick6_looper_runtime_notify_preroll_take_ready(track,
                                                        raw_slot,
                                                        span_frames,
                                                        1U,
                                                        sample_time);
    }

    looper_preroll_stop_capture();
    g_looper_record_boundary.recording = 0U;
    g_looper_record_boundary.stop_armed = 0U;
    g_looper_record_boundary.target_stop_sample = 0U;
}

static void looper_record_start_at_boundary(uint64_t sample_time)
{
    if((g_looper_record_boundary.start_armed == 0U)
            || (g_looper_record_boundary.track_id >= BRICK6_LOOPER_TRACK_CAP)
            || (g_looper_record_boundary.raw_slot == MULTI_RECORD_WRITER_RAW_SLOT_NONE))
    {
        return;
    }

    if(multi_record_writer_start(0U) == 0U)
    {
        looper_record_clear_boundary_state();
        return;
    }

    const uint8_t track = g_looper_record_boundary.track_id;
    const uint8_t raw_slot = g_looper_record_boundary.raw_slot;
    brick6_looper_runtime_preroll_begin(track, raw_slot);
    g_looper_record_boundary.start_armed = 0U;
    g_looper_record_boundary.recording = 1U;
    g_looper_record_boundary.actual_start_sample = sample_time;
    g_looper_record_boundary.source_samples_per_step_q16 = seq_runtime_get_samples_per_step_q16();
    g_looper_record_boundary.source_bpm_milli = seq_runtime_get_tempo_bpm_milli();
    g_looper_record_boundary.target_stop_sample =
        (g_looper_record_boundary.expected_frames != 0U)
            ? (sample_time + (uint64_t)g_looper_record_boundary.expected_frames)
            : 0U;
}

void brick6_looper_runtime_init(void)
{
    memset(g_looper_tracks, 0, sizeof(g_looper_tracks));
    memset(&g_looper_runtime_diag, 0, sizeof(g_looper_runtime_diag));
    looper_record_clear_boundary_state();
    for(uint8_t slot = 0U; slot < BRICK6_LOOPER_SHIFTER_CAP; ++slot)
    {
        memset(&g_looper_shifter_slots[slot], 0, sizeof(g_looper_shifter_slots[slot]));
        g_looper_shifter_slots[slot].owner_track_id = BRICK6_LOOPER_SHIFTER_SLOT_NONE;
        g_looper_shifter_slots[slot].last_mode = 0xFFU;
        brick6_clip_shifter_init(&g_looper_shifter_slots[slot].shifter);
    }
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        g_looper_tracks[track].cache_id = looper_cache_id(track);
        g_looper_tracks[track].cache_key = looper_cache_key(track);
        g_looper_tracks[track].state = BRICK6_LOOPER_RUNTIME_STATE_EMPTY;
        g_looper_tracks[track].raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
        g_looper_tracks[track].stretch_mode = BRICK6_LOOPER_STRETCH_OFF;
        g_looper_tracks[track].stretch_grain_frames = BRICK6_LOOPER_DEFAULT_GRAIN_FRAMES;
    }
}

void brick6_looper_runtime_service(uint32_t byte_budget)
{
    uint8_t has_work = 0U;
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        const brick6_looper_runtime_state_t state = g_looper_tracks[track].state;
        if((state == BRICK6_LOOPER_RUNTIME_STATE_LOAD_PENDING)
                || (state == BRICK6_LOOPER_RUNTIME_STATE_LOADING)
                || (state == BRICK6_LOOPER_RUNTIME_STATE_READY)
                || (state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING))
        {
            has_work = 1U;
            break;
        }
    }

    if((has_work == 0U) || (byte_budget == 0U))
        return;

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
        return;

    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_track_state_t *state = &g_looper_tracks[track];
        if(state->state == BRICK6_LOOPER_RUNTIME_STATE_LOAD_PENDING)
        {
            if(looper_prepare_stream(state) == 0U)
            {
                looper_fail(state);
            }
        }

        if((state->state == BRICK6_LOOPER_RUNTIME_STATE_LOADING)
                || (state->state == BRICK6_LOOPER_RUNTIME_STATE_READY)
                || (state->state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING))
        {
            looper_request_playhead_pages(state);
        }
    }

    sample_page_cache_service_domain_range(SAMPLE_AUDIO_DOMAIN_LOOPER,
                                           0U,
                                           BRICK6_LOOPER_TRACK_CAP,
                                           byte_budget);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);

    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_track_state_t *state = &g_looper_tracks[track];
        looper_update_ready_state(state);
    }
}

uint8_t brick6_looper_runtime_has_pending_sd_work(void)
{
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        const brick6_looper_track_state_t *const state = &g_looper_tracks[track];
        if(state->state == BRICK6_LOOPER_RUNTIME_STATE_LOAD_PENDING)
        {
            return 1U;
        }
    }

    return sample_page_cache_has_queued_domain_range(SAMPLE_AUDIO_DOMAIN_LOOPER,
                                                     0U,
                                                     BRICK6_LOOPER_TRACK_CAP);
}

void brick6_looper_runtime_notify_raw_take_ready(uint8_t track_id,
                                                 uint8_t raw_slot,
                                                 const char *raw_path,
                                                 uint32_t recorded_frames,
                                                 uint8_t play_auto,
                                                 uint64_t scheduled_start_sample)
{
    if((looper_track_valid(track_id) == 0U) || (raw_path == 0) || (recorded_frames == 0U)
            || (raw_slot == MULTI_RECORD_WRITER_RAW_SLOT_NONE))
    {
        return;
    }

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    const uint8_t live_preroll_take =
        (uint8_t)(((state->state == BRICK6_LOOPER_RUNTIME_STATE_READY)
                || (state->state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING))
            && (state->is_raw != 0U)
            && (state->raw_slot == raw_slot)
            && (state->preroll_valid != 0U));
    const brick6_looper_runtime_state_t live_state = state->state;
    const uint32_t live_playhead = state->playhead;
    const uint8_t live_scheduled_valid = state->scheduled_start_valid;
    const uint64_t live_scheduled_sample = state->scheduled_start_sample;

    if(live_preroll_take == 0U)
    {
        looper_clear_stream(state);
    }
    if(looper_copy_path(state->path, raw_path) == 0U)
    {
        looper_fail(state);
        return;
    }

    state->is_raw = 1U;
    state->raw_slot = raw_slot;
    state->play_auto = (play_auto != 0U) ? 1U : 0U;
    state->want_play_when_ready =
        ((play_auto != 0U) && (seq_runtime_is_running() != 0U)) ? 1U : 0U;
    state->scheduled_start_valid =
        ((state->want_play_when_ready != 0U) && (scheduled_start_sample != 0U)) ? 1U : 0U;
    state->scheduled_start_sample =
        (state->scheduled_start_valid != 0U) ? scheduled_start_sample : 0U;
    state->frames_total = recorded_frames;

    if(live_preroll_take != 0U)
    {
        state->playhead = (live_playhead < recorded_frames) ? live_playhead : 0U;
        state->scheduled_start_valid = live_scheduled_valid;
        state->scheduled_start_sample = live_scheduled_sample;
        state->state = live_state;
        if(looper_register_raw_stream(state) == 0U)
        {
            state->cache_registered = 0U;
        }
    }
    else
    {
        state->playhead = 0U;
        looper_adopt_preroll_take(state, track_id, raw_slot, recorded_frames);
        state->state = BRICK6_LOOPER_RUNTIME_STATE_LOAD_PENDING;
    }
    g_looper_runtime_diag.scheduled_start_sample = state->scheduled_start_sample;
    if(live_preroll_take == 0U)
    {
        g_looper_runtime_diag.actual_start_sample = 0U;
        g_looper_runtime_diag.first_output_audio_timeline_sample = 0U;
        g_looper_runtime_diag.first_output_frame_offset = 0U;
        g_looper_runtime_diag.start_playhead = 0U;
        g_looper_runtime_diag.first_output_valid = 0U;
        g_looper_runtime_diag.page_miss_seen = 0U;
    }
    g_looper_runtime_diag.recorded_frames = recorded_frames;
    g_looper_runtime_diag.current_page_start_frame = 0U;
    g_looper_runtime_diag.current_page_frame_count = 0U;
    g_looper_runtime_diag.track_id = track_id;
    g_looper_runtime_diag.raw_slot = raw_slot;
    g_looper_runtime_diag.play_auto = state->play_auto;
    g_looper_runtime_diag.scheduled_start_valid = state->scheduled_start_valid;
    looper_diag_update_take(track_id, state);
}

void brick6_looper_runtime_notify_preroll_take_ready(uint8_t track_id,
                                                     uint8_t raw_slot,
                                                     uint32_t expected_frames,
                                                     uint8_t play_auto,
                                                     uint64_t scheduled_start_sample)
{
    if((looper_track_valid(track_id) == 0U)
            || (expected_frames == 0U)
            || (raw_slot == MULTI_RECORD_WRITER_RAW_SLOT_NONE))
    {
        return;
    }

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    looper_clear_stream(state);
    state->path[0] = '\0';
    state->is_raw = 1U;
    state->raw_slot = raw_slot;
    state->play_auto = (play_auto != 0U) ? 1U : 0U;
    state->want_play_when_ready =
        ((play_auto != 0U) && (seq_runtime_is_running() != 0U)) ? 1U : 0U;
    state->scheduled_start_valid =
        ((state->want_play_when_ready != 0U) && (scheduled_start_sample != 0U)) ? 1U : 0U;
    state->scheduled_start_sample =
        (state->scheduled_start_valid != 0U) ? scheduled_start_sample : 0U;
    state->frames_total = expected_frames;
    state->playhead = 0U;
    looper_adopt_preroll_take(state, track_id, raw_slot, expected_frames);
    state->preroll_consumed = 0U;
    state->raw_relay_done = 0U;
    state->state = (state->preroll_valid != 0U)
        ? BRICK6_LOOPER_RUNTIME_STATE_READY
        : BRICK6_LOOPER_RUNTIME_STATE_EMPTY;

    g_looper_runtime_diag.scheduled_start_sample = state->scheduled_start_sample;
    g_looper_runtime_diag.actual_start_sample = 0U;
    g_looper_runtime_diag.first_output_audio_timeline_sample = 0U;
    g_looper_runtime_diag.first_output_frame_offset = 0U;
    g_looper_runtime_diag.start_playhead = 0U;
    g_looper_runtime_diag.recorded_frames = expected_frames;
    g_looper_runtime_diag.current_page_start_frame = 0U;
    g_looper_runtime_diag.current_page_frame_count = 0U;
    g_looper_runtime_diag.track_id = track_id;
    g_looper_runtime_diag.raw_slot = raw_slot;
    g_looper_runtime_diag.play_auto = state->play_auto;
    g_looper_runtime_diag.scheduled_start_valid = state->scheduled_start_valid;
    g_looper_runtime_diag.first_output_valid = 0U;
    g_looper_runtime_diag.page_miss_seen = 0U;
    looper_diag_update_take(track_id, state);
}

void brick6_looper_runtime_stop_playback(uint8_t track_id)
{
    if(looper_track_valid(track_id) == 0U)
        return;

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    if(state->state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING)
    {
        state->state = BRICK6_LOOPER_RUNTIME_STATE_READY;
        state->playhead = 0U;
        state->playhead_frac_q16 = 0U;
        state->playback_start_sample = 0U;
        state->resync_pending = 0U;
        state->resync_stable_blocks = 0U;
        state->resync_xfade_remaining = 0U;
    }
    looper_release_reader(state);
    state->want_play_when_ready = 0U;
    state->scheduled_start_valid = 0U;
    state->scheduled_start_sample = 0U;
    looper_diag_update_take(track_id, state);
}

void brick6_looper_runtime_prepare_replace(uint8_t track_id)
{
    if(looper_track_valid(track_id) == 0U)
        return;

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    looper_reset_take_state(state);
}

void brick6_looper_runtime_arm_record_start(uint8_t track_id,
                                            uint8_t raw_slot,
                                            uint8_t len_mode,
                                            uint32_t expected_frames,
                                            uint8_t play_auto,
                                            uint64_t request_sample)
{
    if((looper_track_valid(track_id) == 0U)
            || (raw_slot == MULTI_RECORD_WRITER_RAW_SLOT_NONE))
    {
        return;
    }

    looper_record_clear_boundary_state();
    g_looper_record_boundary.start_armed = 1U;
    g_looper_record_boundary.track_id = track_id;
    g_looper_record_boundary.raw_slot = raw_slot;
    g_looper_record_boundary.len_mode = len_mode;
    g_looper_record_boundary.play_auto = (play_auto != 0U) ? 1U : 0U;
    g_looper_record_boundary.expected_frames = expected_frames;
    {
        const uint32_t expected_steps = looper_len_mode_to_steps(len_mode);
        g_looper_record_boundary.expected_steps_q16 =
            (expected_steps != 0U) ? (expected_steps << 16) : 0U;
    }
    g_looper_record_boundary.request_start_sample = request_sample;
}

void brick6_looper_runtime_arm_record_stop(uint64_t request_sample)
{
    if(g_looper_record_boundary.recording != 0U)
    {
        g_looper_record_boundary.stop_armed = 1U;
        g_looper_record_boundary.request_stop_sample = request_sample;
        if(seq_runtime_is_running() == 0U)
        {
            looper_record_stop_at_boundary(seq_runtime_exec_get_audio_timeline_sample());
        }
    }
    else if(g_looper_record_boundary.start_armed != 0U)
    {
        looper_record_clear_boundary_state();
    }
}

uint8_t brick6_looper_runtime_record_is_active_or_armed(void)
{
    return (uint8_t)(((g_looper_record_boundary.start_armed != 0U)
            || (g_looper_record_boundary.recording != 0U)) ? 1U : 0U);
}

uint8_t brick6_looper_runtime_get_record_capture_track(uint8_t *out_track)
{
    if((out_track == 0)
            || (g_looper_record_boundary.recording == 0U)
            || (g_looper_record_boundary.track_id >= BRICK6_LOOPER_TRACK_CAP))
    {
        return 0U;
    }

    multi_record_writer_status_t status;
    if((multi_record_writer_get_status(0U, &status) == 0U)
            || (status.state != MULTI_RECORD_WRITER_STATE_RECORDING))
    {
        return 0U;
    }

    *out_track = g_looper_record_boundary.track_id;
    return 1U;
}

void brick6_looper_runtime_preroll_begin(uint8_t track_id, uint8_t raw_slot)
{
    if((looper_track_valid(track_id) == 0U) || (raw_slot == MULTI_RECORD_WRITER_RAW_SLOT_NONE))
        return;

    g_looper_preroll.active = 0U;
    g_looper_preroll.frames = 0U;
    g_looper_preroll.track_id = track_id;
    g_looper_preroll.raw_slot = raw_slot;
    g_looper_preroll.active = 1U;
    looper_preroll_reset_take_state(&g_looper_tracks[track_id]);
}

void brick6_looper_runtime_preroll_capture_from_irq(uint8_t track_id,
                                                    const int32_t *lr_interleaved,
                                                    uint32_t frames)
{
    if((lr_interleaved == 0)
            || (frames == 0U)
            || (g_looper_preroll.active == 0U)
            || (g_looper_preroll.track_id != track_id)
            || (g_looper_preroll.frames >= BRICK6_LOOPER_PREROLL_FRAMES))
    {
        return;
    }

    uint32_t frames_to_copy = frames;
    const uint32_t remaining = BRICK6_LOOPER_PREROLL_FRAMES - g_looper_preroll.frames;
    if(frames_to_copy > remaining)
    {
        frames_to_copy = remaining;
    }

    uint32_t dst = g_looper_preroll.frames * BRICK6_LOOPER_PREROLL_CHANNELS;
    for(uint32_t i = 0U; i < frames_to_copy; ++i)
    {
        const uint32_t src = i * BRICK6_LOOPER_PREROLL_CHANNELS;
        g_looper_preroll_pcm[dst++] = lr_interleaved[src];
        g_looper_preroll_pcm[dst++] = lr_interleaved[src + 1U];
    }
    __DMB();
    g_looper_preroll.frames += frames_to_copy;

    if(g_looper_preroll.frames >= BRICK6_LOOPER_PREROLL_FRAMES)
    {
        g_looper_preroll.active = 0U;
    }
}

void brick6_looper_runtime_set_play_auto(uint8_t track_id, uint8_t play_auto)
{
    if(looper_track_valid(track_id) == 0U)
        return;

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    state->play_auto = (play_auto != 0U) ? 1U : 0U;
    if(state->play_auto == 0U)
    {
        brick6_looper_runtime_stop_playback(track_id);
        return;
    }

    state->want_play_when_ready = (seq_runtime_is_running() != 0U) ? 1U : 0U;
    looper_diag_update_take(track_id, state);
}

void brick6_looper_runtime_set_stretch(uint8_t track_id,
                                       uint8_t mode,
                                       float pitch_semitones,
                                       uint16_t grain_frames)
{
    if(looper_track_valid(track_id) == 0U)
    {
        return;
    }

    if(mode > BRICK6_LOOPER_STRETCH_SHIFTER)
    {
        mode = BRICK6_LOOPER_STRETCH_OFF;
    }

    if((grain_frames != 384U) && (grain_frames != 512U) && (grain_frames != 768U)
            && (grain_frames != 1024U) && (grain_frames != 1536U) && (grain_frames != 2048U))
    {
        grain_frames = BRICK6_LOOPER_DEFAULT_GRAIN_FRAMES;
    }

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    const uint8_t old_mode = state->stretch_mode;
    const uint8_t mode_changed = (old_mode != mode) ? 1U : 0U;
    const uint8_t grain_changed = (state->stretch_grain_frames != grain_frames) ? 1U : 0U;
    const uint32_t current_samples_per_step_q16 =
        ((old_mode == BRICK6_LOOPER_STRETCH_SPEED)
            || (mode == BRICK6_LOOPER_STRETCH_SPEED))
            ? seq_runtime_get_samples_per_step_q16()
            : 0U;
    const uint8_t old_stable =
        looper_effective_ratio_is_stable(state,
                                         old_mode,
                                         state->stretch_pitch_semitones,
                                         current_samples_per_step_q16,
                                         0);
    const float new_pitch = looper_clampf(pitch_semitones, -12.0f, 12.0f);
    const uint8_t new_stable =
        looper_effective_ratio_is_stable(state,
                                         mode,
                                         new_pitch,
                                         current_samples_per_step_q16,
                                         0);
    state->stretch_mode = mode;
    state->stretch_pitch_semitones = new_pitch;
    state->stretch_grain_frames = grain_frames;

    if((state->state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING)
            && (old_stable == 0U)
            && (new_stable != 0U)
            && (old_mode != BRICK6_LOOPER_STRETCH_SHIFTER)
            && (mode != BRICK6_LOOPER_STRETCH_SHIFTER))
    {
        state->resync_pending = 1U;
        state->resync_stable_blocks = 0U;
    }
    else if(new_stable == 0U)
    {
        state->resync_stable_blocks = 0U;
    }

    if((mode_changed != 0U) || (grain_changed != 0U))
    {
        brick6_looper_shifter_slot_t *const slot = looper_shifter_get(track_id);
        if(slot != 0)
        {
            brick6_clip_shifter_reset(&slot->shifter);
            slot->last_mode = 0xFFU;
        }
    }
}

void brick6_looper_runtime_on_transport_start(void)
{
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_track_state_t *state = &g_looper_tracks[track];
        state->want_play_when_ready = (state->play_auto != 0U) ? 1U : 0U;
        looper_diag_update_take(track, state);
    }
}

void brick6_looper_runtime_on_transport_stop(void)
{
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_runtime_stop_playback(track);
    }
}

void brick6_looper_runtime_on_boundary_edge(uint8_t track_id, uint64_t sample_time)
{
    if(looper_track_valid(track_id) == 0U)
        return;

    if(g_looper_record_boundary.start_armed != 0U)
    {
        looper_record_start_at_boundary(sample_time);
    }
    if((g_looper_record_boundary.recording != 0U)
            && (((g_looper_record_boundary.target_stop_sample != 0U)
                    && (sample_time >= g_looper_record_boundary.target_stop_sample)
                    && (sample_time > g_looper_record_boundary.actual_start_sample))
                || (g_looper_record_boundary.stop_armed != 0U)))
    {
        looper_record_stop_at_boundary(sample_time);
    }

    brick6_looper_track_state_t *state = &g_looper_tracks[track_id];
    if((state->state == BRICK6_LOOPER_RUNTIME_STATE_READY)
            && (state->play_auto != 0U)
            && (state->want_play_when_ready != 0U)
            && (state->scheduled_start_valid == 0U)
            && (seq_runtime_is_running() != 0U))
    {
        looper_start_playback(state, sample_time, 0U);
    }
}

uint8_t brick6_looper_runtime_next_start_offset(uint64_t block_start_sample,
                                                uint32_t block_frames,
                                                uint16_t *out_offset)
{
    if((out_offset == 0) || (block_frames == 0U))
        return 0U;

    const uint64_t block_end_sample = block_start_sample + (uint64_t)block_frames;
    uint8_t found = 0U;
    uint16_t best_offset = 0U;
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_track_state_t *state = &g_looper_tracks[track];
        if((state->scheduled_start_valid == 0U)
                || (state->state != BRICK6_LOOPER_RUNTIME_STATE_READY)
                || (state->scheduled_start_sample < block_start_sample))
        {
            continue;
        }

        if(state->scheduled_start_sample < block_end_sample)
        {
            const uint16_t offset =
                (uint16_t)(state->scheduled_start_sample - block_start_sample);
            if((found == 0U) || (offset < best_offset))
            {
                best_offset = offset;
                found = 1U;
            }
        }
    }

    if(found != 0U)
    {
        *out_offset = best_offset;
        return 1U;
    }
    return 0U;
}

void brick6_looper_runtime_on_scheduled_start(uint64_t sample_time)
{
    for(uint8_t track = 0U; track < BRICK6_LOOPER_TRACK_CAP; ++track)
    {
        brick6_looper_track_state_t *state = &g_looper_tracks[track];
        if((state->scheduled_start_valid == 0U)
                || (state->scheduled_start_sample != sample_time))
        {
            continue;
        }

        if(state->state != BRICK6_LOOPER_RUNTIME_STATE_READY)
        {
            continue;
        }

        state->scheduled_start_valid = 0U;
        state->scheduled_start_sample = 0U;
        if((state->state == BRICK6_LOOPER_RUNTIME_STATE_READY)
                && (state->play_auto != 0U)
                && (state->want_play_when_ready != 0U)
                && (seq_runtime_is_running() != 0U))
        {
            looper_start_playback(state, sample_time, 0U);
        }
        else if(state->state != BRICK6_LOOPER_RUNTIME_STATE_PLAYING)
        {
            state->want_play_when_ready = 0U;
            state->state = BRICK6_LOOPER_RUNTIME_STATE_FAILED;
            state->playhead = 0U;
        }
    }
}

uint8_t brick6_looper_runtime_is_ready(uint8_t track_id)
{
    if(looper_track_valid(track_id) == 0U)
        return 0U;

    const brick6_looper_runtime_state_t state = g_looper_tracks[track_id].state;
    return (uint8_t)((state == BRICK6_LOOPER_RUNTIME_STATE_READY)
                    || (state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING));
}

uint8_t brick6_looper_runtime_is_playing(uint8_t track_id)
{
    if(looper_track_valid(track_id) == 0U)
        return 0U;

    return (g_looper_tracks[track_id].state == BRICK6_LOOPER_RUNTIME_STATE_PLAYING) ? 1U : 0U;
}

static uint8_t looper_read_frame_normal(brick6_looper_track_state_t *state,
                                        uint32_t frame,
                                        float *out_l,
                                        float *out_r)
{
    if((state == 0) || (out_l == 0) || (out_r == 0) || (state->frames_total == 0U))
    {
        return 0U;
    }

    if(frame >= state->frames_total)
    {
        frame %= state->frames_total;
    }

    if(looper_acquire_page_for_frame(state, frame) == 0U)
    {
        return 0U;
    }

    const uint32_t page_offset = frame - state->current_start_frame;
    const float *const src_base =
        &state->current_base[page_offset * SAMPLE_PAGE_FRAME_STRIDE_FLOATS];
    *out_l = src_base[0];
    *out_r = src_base[1];
    state->raw_relay_done = 1U;
    state->preroll_consumed = 1U;
    return 1U;
}

static uint8_t looper_read_interp_normal(brick6_looper_track_state_t *state,
                                         uint32_t frame,
                                         uint32_t frac_q16,
                                         float *out_l,
                                         float *out_r)
{
    float l0 = 0.0f;
    float r0 = 0.0f;
    float l1 = 0.0f;
    float r1 = 0.0f;

    if((state == 0) || (out_l == 0) || (out_r == 0) || (state->frames_total == 0U))
    {
        return 0U;
    }

    if(looper_read_frame_normal(state, frame, &l0, &r0) == 0U)
    {
        return 0U;
    }

    if(frac_q16 == 0U)
    {
        *out_l = l0;
        *out_r = r0;
        return 1U;
    }

    uint32_t next_frame = frame + 1U;
    if(next_frame >= state->frames_total)
    {
        next_frame = 0U;
    }
    if(looper_read_frame_normal(state, next_frame, &l1, &r1) == 0U)
    {
        return 0U;
    }

    const float frac = (float)frac_q16 * (1.0f / (float)BRICK6_LOOPER_Q16_ONE);
    *out_l = l0 + ((l1 - l0) * frac);
    *out_r = r0 + ((r1 - r0) * frac);
    return 1U;
}

static void looper_advance_playhead_q16(brick6_looper_track_state_t *state,
                                        uint32_t increment_q16)
{
    if((state == 0) || (state->frames_total == 0U) || (increment_q16 == 0U))
    {
        return;
    }

    uint64_t pos_q16 = ((uint64_t)state->playhead << 16)
        + (uint64_t)state->playhead_frac_q16
        + (uint64_t)increment_q16;
    const uint64_t loop_q16 = (uint64_t)state->frames_total << 16;
    if(pos_q16 >= loop_q16)
    {
        pos_q16 %= loop_q16;
        g_looper_runtime_diag.wrap_count++;
    }
    state->playhead = (uint32_t)(pos_q16 >> 16);
    state->playhead_frac_q16 = (uint32_t)(pos_q16 & 0xFFFFU);
}

static void looper_render_varispeed(brick6_looper_track_state_t *state,
                                    uint8_t track,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames,
                                    uint32_t increment_q16)
{
    uint32_t produced = 0U;
    g_looper_runtime_diag.playback_normal_used++;

    while(produced < frames)
    {
        if(state->playhead >= state->frames_total)
        {
            state->playhead = 0U;
            state->playhead_frac_q16 = 0U;
        }

        float sample_l = 0.0f;
        float sample_r = 0.0f;
        if(looper_read_interp_normal(state,
                                     state->playhead,
                                     state->playhead_frac_q16,
                                     &sample_l,
                                     &sample_r) == 0U)
        {
            g_looper_runtime_diag.page_miss_seen = 1U;
            g_looper_runtime_diag.fallback_miss++;
            looper_diag_update_take(track, state);
            looper_advance_playhead_q16(state, increment_q16);
            produced++;
            continue;
        }

        out_l[produced] += sample_l;
        out_r[produced] += sample_r;
        if((g_looper_runtime_diag.first_output_valid == 0U)
                && ((sample_l != 0.0f) || (sample_r != 0.0f)))
        {
            g_looper_runtime_diag.first_output_audio_timeline_sample = (uint64_t)produced;
            g_looper_runtime_diag.first_output_frame_offset = produced;
            g_looper_runtime_diag.first_output_valid = 1U;
        }

        looper_advance_playhead_q16(state, increment_q16);
        produced++;
        if((state->current_acquired != 0U)
                && ((state->playhead == 0U)
                    || (state->playhead < state->current_start_frame)
                    || (state->playhead >= (state->current_start_frame + state->current_frame_count))))
        {
            looper_release_reader(state);
        }
    }

    looper_diag_update_take(track, state);
}

static uint32_t looper_render_preroll_bridge(brick6_looper_track_state_t *state,
                                             uint8_t track,
                                             float *out_l,
                                             float *out_r,
                                             uint32_t frames)
{
    if((state == 0) || (out_l == 0) || (out_r == 0) || (frames == 0U)
            || (looper_preroll_can_read(state, state->playhead) == 0U))
    {
        return 0U;
    }

    uint32_t bridge_frames = state->preroll_frames - state->playhead;
    if(bridge_frames > frames)
    {
        bridge_frames = frames;
    }

    const uint32_t base = state->playhead * BRICK6_LOOPER_PREROLL_CHANNELS;
    for(uint32_t i = 0U; i < bridge_frames; ++i)
    {
        const uint32_t src = base + (i * BRICK6_LOOPER_PREROLL_CHANNELS);
        out_l[i] += looper_pcm24_to_float(g_looper_preroll_pcm[src]);
        out_r[i] += looper_pcm24_to_float(g_looper_preroll_pcm[src + 1U]);
    }

    state->preroll_used_reported = 1U;
    g_looper_runtime_diag.preroll_bridge_active++;
    const uint32_t playhead_before = state->playhead;
    const uint8_t wraps_on_preroll =
        ((playhead_before + bridge_frames) >= state->frames_total) ? 1U : 0U;
    looper_advance_playhead(state, bridge_frames);
    if((wraps_on_preroll != 0U)
            || ((playhead_before + bridge_frames) >= state->preroll_frames))
    {
        state->preroll_consumed = 1U;
    }
    looper_diag_update_take(track, state);
    return bridge_frames;
}

static void looper_render_normal_raw(brick6_looper_track_state_t *state,
                                     uint8_t track,
                                     float *out_l,
                                     float *out_r,
                                     uint32_t frames)
{
    uint32_t produced = 0U;

    while(produced < frames)
    {
        if(state->playhead >= state->frames_total)
        {
            state->playhead = 0U;
            g_looper_runtime_diag.wrap_count++;
        }

        uint32_t request_frames = frames - produced;
        const uint32_t loop_remaining = state->frames_total - state->playhead;
        if(request_frames > loop_remaining)
        {
            request_frames = loop_remaining;
        }

        if(looper_acquire_current_page(state) == 0U)
        {
            g_looper_runtime_diag.page_miss_seen = 1U;
            g_looper_runtime_diag.fallback_miss++;
            looper_diag_update_take(track, state);
            looper_advance_playhead(state, request_frames);
            produced += request_frames;
            continue;
        }

        const uint32_t page_offset = state->playhead - state->current_start_frame;
        g_looper_runtime_diag.current_page_start_frame = state->current_start_frame;
        g_looper_runtime_diag.current_page_frame_count = state->current_frame_count;
        uint32_t block_frames = state->current_frame_count - page_offset;
        if(block_frames > request_frames)
        {
            block_frames = request_frames;
        }

        const float *const src_base =
            &state->current_base[page_offset * SAMPLE_PAGE_FRAME_STRIDE_FLOATS];
        for(uint32_t i = 0U; i < block_frames; ++i)
        {
            const uint32_t src = i * SAMPLE_PAGE_FRAME_STRIDE_FLOATS;
            out_l[produced + i] += src_base[src];
            out_r[produced + i] += src_base[src + 1U];
            if((g_looper_runtime_diag.first_output_valid == 0U)
                    && ((src_base[src] != 0.0f) || (src_base[src + 1U] != 0.0f)))
            {
                g_looper_runtime_diag.first_output_audio_timeline_sample =
                    (uint64_t)produced + (uint64_t)i;
                g_looper_runtime_diag.first_output_frame_offset = produced + i;
                g_looper_runtime_diag.first_output_valid = 1U;
            }
        }

        state->raw_relay_done = 1U;
        state->preroll_consumed = 1U;
        g_looper_runtime_diag.playback_normal_used++;
        looper_advance_playhead(state, block_frames);
        looper_diag_update_take(track, state);
        produced += block_frames;
        if((block_frames == 0U)
                || ((page_offset + block_frames) >= state->current_frame_count)
                || (state->playhead == 0U))
        {
            looper_release_reader(state);
        }
    }
}

static uint32_t looper_expected_frame_from_timeline(const brick6_looper_track_state_t *state,
                                                    float stable_ratio)
{
    if((state == 0) || (state->frames_total == 0U))
    {
        return 0U;
    }

    const uint64_t now_sample = seq_runtime_exec_get_audio_timeline_sample();
    const uint64_t elapsed =
        (now_sample > state->playback_start_sample)
            ? (now_sample - state->playback_start_sample)
            : 0ULL;
    if(stable_ratio < 0.25f)
    {
        stable_ratio = 0.25f;
    }
    else if(stable_ratio > 4.0f)
    {
        stable_ratio = 4.0f;
    }
    const uint64_t expected = (uint64_t)(((double)elapsed * (double)stable_ratio) + 0.5);
    return (uint32_t)(expected % (uint64_t)state->frames_total);
}

static void looper_try_consume_resync(brick6_looper_track_state_t *state,
                                      uint8_t stable_now,
                                      float stable_ratio)
{
    if((state == 0) || (state->resync_pending == 0U))
    {
        return;
    }

    if(stable_now == 0U)
    {
        state->resync_pending = 0U;
        state->resync_stable_blocks = 0U;
        return;
    }

    if(state->resync_stable_blocks < BRICK6_LOOPER_RESYNC_STABLE_BLOCKS)
    {
        state->resync_stable_blocks++;
        if(state->resync_stable_blocks < BRICK6_LOOPER_RESYNC_STABLE_BLOCKS)
        {
            return;
        }
    }

    const uint32_t expected_frame = looper_expected_frame_from_timeline(state, stable_ratio);
    if((state->playhead == expected_frame) && (state->playhead_frac_q16 == 0U))
    {
        state->resync_pending = 0U;
        state->resync_stable_blocks = 0U;
        return;
    }

    state->resync_old_playhead = state->playhead;
    state->resync_old_frac_q16 = state->playhead_frac_q16;
    looper_release_reader(state);
    state->playhead = expected_frame;
    state->playhead_frac_q16 = 0U;
    state->resync_xfade_remaining =
        (state->frames_total < BRICK6_LOOPER_RESYNC_XFADE_FRAMES)
            ? (uint8_t)state->frames_total
            : (uint8_t)BRICK6_LOOPER_RESYNC_XFADE_FRAMES;
    state->resync_pending = 0U;
    state->resync_stable_blocks = 0U;
    looper_request_playhead_pages(state);
}

static uint32_t looper_render_resync_xfade(brick6_looper_track_state_t *state,
                                           uint8_t track,
                                           float *out_l,
                                           float *out_r,
                                           uint32_t frames)
{
    if((state == 0) || (out_l == 0) || (out_r == 0) || (frames == 0U)
            || (state->resync_xfade_remaining == 0U))
    {
        return 0U;
    }

    uint32_t old_playhead = state->resync_old_playhead;
    uint32_t old_frac_q16 = state->resync_old_frac_q16;
    uint32_t produced = 0U;
    while((produced < frames) && (state->resync_xfade_remaining != 0U))
    {
        float old_l = 0.0f;
        float old_r = 0.0f;
        float new_l = 0.0f;
        float new_r = 0.0f;
        const uint8_t old_ok =
            looper_read_interp_normal(state, old_playhead, old_frac_q16, &old_l, &old_r);
        const uint8_t new_ok =
            looper_read_frame_normal(state, state->playhead, &new_l, &new_r);

        if((old_ok == 0U) || (new_ok == 0U))
        {
            g_looper_runtime_diag.page_miss_seen = 1U;
            g_looper_runtime_diag.fallback_miss++;
        }

        const uint32_t phase =
            BRICK6_LOOPER_RESYNC_XFADE_FRAMES - (uint32_t)state->resync_xfade_remaining;
        const float wet = (float)(phase + 1U) * (1.0f / (float)BRICK6_LOOPER_RESYNC_XFADE_FRAMES);
        const float dry = 1.0f - wet;
        out_l[produced] += (old_l * dry) + (new_l * wet);
        out_r[produced] += (old_r * dry) + (new_r * wet);

        uint64_t old_pos_q16 = ((uint64_t)old_playhead << 16)
            + (uint64_t)old_frac_q16
            + (uint64_t)BRICK6_LOOPER_Q16_ONE;
        const uint64_t loop_q16 = (uint64_t)state->frames_total << 16;
        if(old_pos_q16 >= loop_q16)
        {
            old_pos_q16 %= loop_q16;
        }
        old_playhead = (uint32_t)(old_pos_q16 >> 16);
        old_frac_q16 = (uint32_t)(old_pos_q16 & 0xFFFFU);

        looper_advance_playhead(state, 1U);
        produced++;
        state->resync_xfade_remaining--;
        if((state->current_acquired != 0U)
                && ((state->playhead == 0U)
                    || (state->playhead < state->current_start_frame)
                    || (state->playhead >= (state->current_start_frame + state->current_frame_count))))
        {
            looper_release_reader(state);
        }
    }

    state->resync_old_playhead = old_playhead;
    state->resync_old_frac_q16 = old_frac_q16;
    looper_diag_update_take(track, state);
    return produced;
}

void brick6_looper_runtime_render_track(const track_runtime_ctx_t *ctx,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames)
{
    if((ctx == 0) || (out_l == 0) || (out_r == 0) || (frames == 0U))
        return;

    const uint8_t track = ctx->track_id;
    if(looper_track_valid(track) == 0U)
        return;

    brick6_looper_track_state_t *state = &g_looper_tracks[track];
    if((state->state != BRICK6_LOOPER_RUNTIME_STATE_PLAYING)
            || (state->frames_total == 0U)
            || ((state->cache_registered == 0U) && (state->preroll_valid == 0U)))
    {
        return;
    }

    if((state->raw_relay_done == 0U)
            && (looper_preroll_can_read(state, state->playhead) != 0U))
    {
        if(looper_try_enter_normal_playback(state) == 0U)
        {
            const uint32_t bridged =
                looper_render_preroll_bridge(state, track, out_l, out_r, frames);
            if(bridged >= frames)
            {
                return;
            }

            if(looper_try_enter_normal_playback(state) == 0U)
            {
                g_looper_runtime_diag.page_miss_seen = 1U;
                g_looper_runtime_diag.fallback_miss++;
                looper_diag_update_take(track, state);
                return;
            }

            out_l += bridged;
            out_r += bridged;
            frames -= bridged;
        }
    }

    if(state->cache_registered == 0U)
    {
        return;
    }

    const uint32_t current_samples_per_step_q16 =
        ((state->stretch_mode == BRICK6_LOOPER_STRETCH_SPEED)
            || (state->stretch_mode == BRICK6_LOOPER_STRETCH_SHIFTER))
            ? seq_runtime_get_samples_per_step_q16()
            : 0U;
    uint8_t mode = looper_effective_mode(state, current_samples_per_step_q16);
    float stable_ratio = 1.0f;
    const uint8_t pitch_zero = looper_pitch_is_zero(state->stretch_pitch_semitones);
    const uint8_t pitch_stable =
        looper_pitch_stable_ratio(state->stretch_pitch_semitones, &stable_ratio);
    const float pitch_ratio =
        (pitch_stable != 0U) ? stable_ratio : looper_pitch_ratio(state->stretch_pitch_semitones);
    float timing_ratio = 1.0f;

    if(mode != BRICK6_LOOPER_STRETCH_OFF)
    {
        timing_ratio = looper_timing_ratio(state, current_samples_per_step_q16);
    }

    if((mode == BRICK6_LOOPER_STRETCH_SHIFTER)
            && (looper_timing_is_neutral(timing_ratio) != 0U)
            && (pitch_zero != 0U))
    {
        mode = BRICK6_LOOPER_STRETCH_OFF;
    }

    float effective_stable_ratio = stable_ratio;
    if(mode == BRICK6_LOOPER_STRETCH_SPEED)
    {
        effective_stable_ratio *= timing_ratio;
    }
    const uint8_t ratio_stable =
        (uint8_t)(((mode == BRICK6_LOOPER_STRETCH_OFF) && (pitch_stable != 0U))
            || ((mode == BRICK6_LOOPER_STRETCH_SPEED) && (pitch_stable != 0U)));
    looper_try_consume_resync(state, ratio_stable, effective_stable_ratio);
    if(state->resync_xfade_remaining != 0U)
    {
        const uint32_t xfade_frames =
            looper_render_resync_xfade(state, track, out_l, out_r, frames);
        if(xfade_frames >= frames)
        {
            return;
        }
        out_l += xfade_frames;
        out_r += xfade_frames;
        frames -= xfade_frames;
    }

    if(mode == BRICK6_LOOPER_STRETCH_SHIFTER)
    {
        brick6_looper_shifter_slot_t *slot = looper_shifter_acquire(track);
        if(slot == 0)
        {
            mode = BRICK6_LOOPER_STRETCH_SPEED;
        }
        else
        {
            float correction = pitch_ratio / timing_ratio;
            if(correction < 0.25f)
            {
                correction = 0.25f;
            }
            else if(correction > 4.0f)
            {
                correction = 4.0f;
            }

            looper_shifter_reset_if_needed(slot, mode, state->stretch_grain_frames);
            brick6_clip_shifter_set_window_frames(&slot->shifter, state->stretch_grain_frames);
            brick6_clip_shifter_set_pitch_correction(&slot->shifter, correction);

            memset(g_looper_stretch_l, 0, sizeof(g_looper_stretch_l));
            memset(g_looper_stretch_r, 0, sizeof(g_looper_stretch_r));
            looper_render_varispeed(state,
                                    track,
                                    g_looper_stretch_l,
                                    g_looper_stretch_r,
                                    frames,
                                    looper_ratio_to_increment_q16(timing_ratio));
            brick6_clip_shifter_process_stereo(&slot->shifter,
                                               g_looper_stretch_l,
                                               g_looper_stretch_r,
                                               frames);
            for(uint32_t i = 0U; i < frames; ++i)
            {
                out_l[i] += g_looper_stretch_l[i];
                out_r[i] += g_looper_stretch_r[i];
            }
            return;
        }
    }

    if(mode == BRICK6_LOOPER_STRETCH_SPEED)
    {
        looper_render_varispeed(state,
                                track,
                                out_l,
                                out_r,
                                frames,
                                looper_ratio_to_increment_q16(timing_ratio * pitch_ratio));
        return;
    }

    if((mode == BRICK6_LOOPER_STRETCH_OFF) && (pitch_zero == 0U))
    {
        looper_render_varispeed(state,
                                track,
                                out_l,
                                out_r,
                                frames,
                                looper_ratio_to_increment_q16(pitch_ratio));
        return;
    }

    looper_render_normal_raw(state, track, out_l, out_r, frames);
}

void brick6_looper_runtime_diag_get_snapshot(brick6_looper_runtime_diag_snapshot_t *out_snapshot)
{
    if(out_snapshot == 0)
    {
        return;
    }

    *out_snapshot = g_looper_runtime_diag;
}
