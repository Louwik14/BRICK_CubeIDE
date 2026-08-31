/**
 * @file brick6_sampler_runtime.c
 * @brief Track-aware Sampler backend for RAM, Stream, Multi and RAM grid slicing.
 */

#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/audio_note_engine_adapter.h"

#include "Audio/multi_voice_dsp.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Audio/mixer.h"
#include "Board/board_audio_format.h"
#include "Audio/brick6_clip_shifter.h"
#include "Audio/audio_transport_runtime.h"
#include "Track/synth_polyphony.h"
#include "Track/track_types.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "Platform/memory_layout.h"
#include "Audio/multi_sample_audio_projection_audio.h"
#include "Audio/sample_classic_audio_projection_audio.h"
#include "Sampler/sample_reader_contract.h"
#include "Sampler/sample_page_cache_audio.h"
#include "Sampler/sample_voice_reader.h"
#include "Audio/sampler_ram_audio_projection_audio.h"
#include "Audio/audio_shared_memory.h"

/* The sampler voice table is a lane resource.  GROUP children reuse this
 * table; they do not allocate a second sampler pool. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

#define BRICK6_SAMPLER_Q16_ONE (65536U)
#define BRICK6_SAMPLER_CACHE_VOICE_BASE (2U)
#define BRICK6_SAMPLER_CLIP_SLOT_NONE 0xFFU
#define BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES 1536U
#define BRICK6_SAMPLER_CACHE_VOICE_NONE UINT8_MAX
#define BRICK6_SAMPLER_MULTI_LOOKAHEAD_PAGES SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES
#define BRICK6_SAMPLER_MULTI_WINDOW_MASK_BITS (32U)
#define BRICK6_SAMPLER_MIN_READY_TARGET_FRAMES SAMPLE_PREP_MIN_READY_FRAMES
#define STEAL_DECLICK_SAMPLES (16U)
#define STEAL_DECLICK_TAIL_SLOTS (32U)
#define STEAL_DECLICK_EPSILON (0.0000001f)
#define BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ (48000U)
#define BRICK6_SAMPLER_Q16_FRAC_MASK (BRICK6_SAMPLER_Q16_ONE - 1U)
#define BRICK6_SAMPLER_MULTI_RENDER_PITCHED   (0U)
#define BRICK6_SAMPLER_MULTI_RENDER_MONO_1X   (1U)
#define BRICK6_SAMPLER_MULTI_RENDER_STEREO_1X (2U)

typedef struct
{
    uint32_t generation;
    uint32_t frames;
    uint32_t sample_rate;
    uint32_t data_offset;
    float *data;
    uint16_t global_slot;
    uint16_t ram_slot;
    uint16_t channels;
    uint16_t bytes_per_frame;
    sampler_ram_format_t format;
} sampler_ram_audio_view_t;

typedef struct
{
    uint8_t source_kind;
    uint16_t sample_id;
    uint16_t multi_instrument_id;
    uint16_t multi_sample_id;
    uint16_t ram_slot;
    uint32_t ram_generation;
    uint8_t owner_track_id;
    const float *ram_data;
    float position;
    uint64_t ram_position_q16;
    uint8_t active;
    uint8_t note;
    uint8_t velocity;
    uint8_t mode;
    uint8_t slice_count;
    uint8_t slice_index;
    float gain;
    float trigger_velocity_gain;
    float start;
    float length;
    float loop_start;
    float tune;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_frames;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    float step_signed;
    uint32_t ram_step_q16;
    uint8_t reverse;
    uint8_t loop_mode;
    uint8_t use_slice;
    uint8_t use_segment_cursor;
    uint8_t multi_renderer;
    uint8_t release_pending;
    uint8_t vca_note_released;
    uint8_t dsp_slot;
    uint8_t spread_index;
    sample_play_plan_t play_plan;
    sample_voice_reader_t reader;
    /* Multi only: non-zero trigger_order is the handle generation. */
    uint32_t trigger_order;
    uint32_t output_id;
    float last_out_l;
    float last_out_r;
    uint8_t last_out_valid;
    uint8_t start_fade_remaining;
    uint16_t ram_channels;
    sampler_ram_format_t ram_format;
    uint32_t ram_frames;
    uint32_t ram_sample_rate;
} brick6_sampler_voice_t;

typedef enum
{
    BRICK6_SAMPLER_VOICE_NONE = 0,
    BRICK6_SAMPLER_VOICE_CLASSIC,
    BRICK6_SAMPLER_VOICE_CLIP,
    BRICK6_SAMPLER_VOICE_MULTI,
    BRICK6_SAMPLER_VOICE_RAM
} brick6_sampler_voice_kind_t;

typedef enum
{
    BRICK6_SAMPLER_CLIP_STATE_IDLE = 0,
    BRICK6_SAMPLER_CLIP_STATE_PLAYING,
    BRICK6_SAMPLER_CLIP_STATE_STOPPED
} brick6_sampler_clip_state_t;

typedef struct
{
    uint16_t sample_id;
    float gain;
    float source_bpm;
    uint16_t grain_size;
    uint8_t sync_length;
    float pitch_semitones;
    uint8_t play_mode;
    uint8_t loop_enabled;
    uint8_t stretch_mode;
    uint8_t state;
    uint8_t use_shifter_engine;
    uint8_t clip_slot_index;
    uint32_t timing_ratio_q16;
    uint32_t pitch_ratio_q16;
    float shifter_correction;
} brick6_sampler_clip_runtime_t;

typedef struct
{
    int16_t note_delta;
    uint32_t timing_ratio_q16;
    uint32_t pitch_ratio_q16;
    uint32_t step_q16;
    float shifter_correction;
} brick6_sampler_stream_pitch_plan_t;

typedef struct
{
    uint16_t instrument_id;
    float gain;
    uint8_t loop_enabled;
    uint8_t voice_count;
    float spread;
} brick6_sampler_multi_track_state_t;

typedef struct
{
    uint8_t owner_track_id;
    brick6_clip_shifter_t shifter;
    float stretch_render_l[AUDIO_BLOCK_SIZE];
    float stretch_render_r[AUDIO_BLOCK_SIZE];
} brick6_sampler_clip_slot_t;

typedef struct
{
    float start_l;
    float start_r;
    uint8_t owner_track_id;
    uint8_t remaining;
    uint8_t active;
} brick6_sampler_declick_tail_t;

enum
{
    BRICK6_SAMPLER_LOOP_NONE = 0,
    BRICK6_SAMPLER_LOOP_FORWARD = 1,
    BRICK6_SAMPLER_LOOP_PINGPONG = 2
};

typedef enum
{
    BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC = 0,
    BRICK6_SAMPLE_COMMON_TRIGGER_MULTI
} brick6_sample_common_trigger_kind_t;

typedef enum
{
    BRICK6_SAMPLE_COMMON_PLAN_OK = 0,
    BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG,
    BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL,
    BRICK6_SAMPLE_COMMON_PLAN_SOURCE_INVALID,
    BRICK6_SAMPLE_COMMON_PLAN_BUILD_FAIL,
    BRICK6_SAMPLE_COMMON_PLAN_PLAN_INVALID
} brick6_sample_common_plan_result_t;

typedef struct
{
    brick6_sample_common_trigger_kind_t kind;
    const brick6_sampler_voice_t *voice;
    const sample_play_plan_t *runtime_plan;
    const multi_sample_audio_source_t *multi_source;
    uint32_t total_frames;
    uint32_t step_q16;
    float render_gain;
    uint16_t instrument_id;
    uint8_t track_id;
    uint8_t note;
    uint8_t velocity;
} brick6_sample_common_trigger_t;

static AUDIO_HOT brick6_sampler_voice_t g_sampler_voice[SEQ_TRACK_COUNT];
static AUDIO_HOT brick6_sampler_voice_t
    g_sampler_multi_voice[SAMPLER_MULTI_MAX_GLOBAL_VOICES];
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLER_MULTI_MAX_GLOBAL_VOICES == 8U,
               "Multi runtime pool must remain globally capped at eight voices");
_Static_assert(sizeof(brick6_sampler_voice_t) == 472U,
               "Sampler voice size changed; remeasure DTCM before accepting it");
_Static_assert(sizeof(g_sampler_multi_voice) == 3776U,
               "Multi sampler voice pool size changed; remeasure DTCM before accepting it");
#endif
static brick6_sampler_clip_runtime_t g_sampler_clip_runtime[SEQ_TRACK_COUNT];
static brick6_sampler_multi_track_state_t g_sampler_multi_track_state[SEQ_TRACK_COUNT];
static brick6_sampler_clip_slot_t g_sampler_clip_slots[BRICK6_MAX_CLIP_TRACKS];
static AUDIO_HOT brick6_sampler_declick_tail_t
    g_sampler_declick_tail[STEAL_DECLICK_TAIL_SLOTS];
static AUDIO_HOT uint32_t g_sampler_render_track_mask;
static uint32_t g_sampler_voice_trigger_counter;
static CTRL_STATE uint8_t
    g_sampler_multi_stream_release_pending[MULTI_SAMPLE_POOL_MAX_SAMPLES];
static CTRL_STATE uint8_t
    g_sampler_multi_page0_reject_logged[MULTI_SAMPLE_POOL_MAX_SAMPLES];

static brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag;

#define BRICK6_SAMPLER_RUNTIME_DIAG_INC(field) ((void)0)

#define BRICK6_SAMPLER_STEP_EPSILON (0.0001f)

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id);
static uint32_t brick6_sampler_runtime_multi_active_count(void);
static uint32_t brick6_sampler_runtime_multi_active_count_for_track(uint8_t track_id);
static uint8_t brick6_sampler_runtime_multi_spread_voice_count(uint8_t track_id);
static uint8_t brick6_sampler_runtime_multi_voice_occupied(
    const brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_multi_assign_spread_index(
    brick6_sampler_voice_t *voice,
    uint8_t track_id);
static void brick6_sampler_runtime_multi_reindex_spread(uint8_t track_id);
static void brick6_sampler_runtime_multi_get_spread_pan(
    const brick6_sampler_voice_t *voice,
    float *out_pan_l,
    float *out_pan_r);
static uint8_t brick6_sampler_runtime_resolve_grid_count(uint8_t raw_grid_count);
static float brick6_sampler_runtime_velocity_gain(uint8_t velocity);
static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio);
static uint32_t brick6_sampler_runtime_ram_step_q16(const brick6_sampler_voice_t *voice,
                                                    const sampler_ram_audio_view_t *ram);
static uint32_t brick6_sampler_runtime_next_trigger_order(void);
static uint32_t brick6_sampler_runtime_clip_ratio_q16(float source_bpm);
static uint32_t brick6_sampler_runtime_clamp_region_begin(uint32_t length_frames, float start);
static uint32_t brick6_sampler_runtime_clamp_region_length(uint32_t length_frames,
                                                           uint32_t region_begin,
                                                           float length);
static void brick6_sampler_runtime_build_effective_ram_region(uint32_t length_frames,
                                                              float start,
                                                              float length,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end);
static void brick6_sampler_runtime_build_effective_ram_bounds(uint32_t length_frames,
                                                              float start,
                                                              float length,
                                                              float loop_start,
                                                              uint8_t mode,
                                                              uint8_t use_loop_marker,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end,
                                                              uint32_t *out_loop_begin);
static void brick6_sampler_runtime_build_effective_ram_slice_region(uint32_t region_begin,
                                                                    uint32_t region_end,
                                                                    uint32_t slice_count,
                                                                    uint32_t slice_index,
                                                                    uint32_t *out_begin,
                                                                    uint32_t *out_end);
static uint8_t brick6_sampler_runtime_ram_mode_to_loop_mode(uint8_t mode,
                                                            uint32_t region_begin,
                                                            uint32_t region_end,
                                                            uint8_t loop_valid);
static void brick6_sampler_runtime_reconcile_ram_voice_bounds_live(uint8_t track_id);
static void brick6_sampler_runtime_reproject_ram_voice_tune_live(uint8_t track_id);
static float brick6_sampler_runtime_pitch_ratio(float semitones);
static uint32_t brick6_sampler_runtime_clip_resolve_timing_ratio_q16(uint8_t track_id,
                                                                     const sample_resolved_source_t *desc,
                                                                     const brick6_sampler_clip_runtime_t *clip,
                                                                     uint32_t *out_region_begin,
                                                                     uint32_t *out_region_end);
static brick6_sampler_stream_pitch_plan_t brick6_sampler_runtime_stream_build_pitch_plan(
    uint8_t track_id,
    uint8_t played_note,
    const sample_resolved_source_t *desc,
    const brick6_sampler_clip_runtime_t *clip,
    uint8_t use_shifter,
    uint32_t *out_region_begin,
    uint32_t *out_region_end);
static uint8_t brick6_sampler_runtime_clip_uses_shifter(const brick6_sampler_clip_runtime_t *clip);
static uint16_t brick6_sampler_runtime_clip_sanitize_grain_size(uint16_t grain_size);
static uint16_t brick6_sampler_runtime_clip_shifter_window_frames(uint16_t grain_size);
static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_get_slot(uint8_t track_id);
static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_ensure_slot(uint8_t track_id);
static void brick6_sampler_runtime_clip_release_slot(uint8_t track_id);
static void brick6_sampler_runtime_clip_configure_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                          brick6_sampler_clip_slot_t *slot);
static void brick6_sampler_runtime_clip_reset_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                      brick6_sampler_clip_slot_t *slot);
static void brick6_sampler_runtime_clip_reset(uint8_t track_id);
static uint8_t brick6_sampler_runtime_clip_start_playback(uint8_t track_id);
static void brick6_sampler_runtime_clip_stop_playback(uint8_t track_id);
static void brick6_sampler_runtime_clip_stop_playback_silent(uint8_t track_id);
static void brick6_sampler_runtime_clip_render_shifter(brick6_sampler_voice_t *voice,
                                                       brick6_sampler_clip_runtime_t *clip,
                                                       brick6_sampler_clip_slot_t *slot,
                                                       float *out_l,
                                                       float *out_r,
                                                       uint32_t frames);
static uint8_t brick6_sampler_runtime_resolve_ram_source(uint16_t global_slot,
                                                         uint16_t *out_ram_slot,
                                                         sampler_ram_audio_view_t *out_ram);
static uint8_t brick6_sampler_runtime_trigger_ram(uint8_t track_id);
static uint8_t brick6_sampler_runtime_render_ram_forward_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static uint8_t brick6_sampler_runtime_render_ram_reverse_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static uint64_t brick6_sampler_runtime_wrap_q16(uint64_t offset_q16, uint64_t span_q16);
static inline float brick6_sampler_runtime_ram_fade_gain(brick6_sampler_voice_t *voice);
static uint8_t brick6_sampler_runtime_render_ram_pingpong_unpitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint32_t position);
static uint8_t brick6_sampler_runtime_render_ram_pingpong_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_audio_view_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static void brick6_sampler_runtime_render_ram(brick6_sampler_voice_t *voice,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t frames);
static void brick6_sampler_runtime_render_ram_mono(brick6_sampler_voice_t *voice,
                                                   float *out_mono,
                                                   uint32_t frames);
static void brick6_sampler_runtime_clear_ram_voice(brick6_sampler_voice_t *voice);
static void brick6_sampler_render_sample_segment_cursor(brick6_sampler_voice_t *voice,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames);
static void brick6_sampler_render_sample_segment_cursor_mono(brick6_sampler_voice_t *voice,
                                                             float *out_mono,
                                                             uint32_t frames);
static void brick6_sampler_render_sample_mono(brick6_sampler_voice_t *voice,
                                              float *out_mono,
                                              uint32_t frames);
static void brick6_sampler_render_multi(brick6_sampler_voice_t *voice,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames);
static void brick6_sampler_runtime_multi_stop_voice(brick6_sampler_voice_t *voice, uint8_t reason);
static void brick6_sampler_runtime_multi_stop_voice_after_vca(brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_multi_stop_track(uint8_t track_id);
static void brick6_sampler_runtime_multi_stop_track_renderer(uint8_t track_id);
static void brick6_sampler_runtime_multi_defer_stream_release(uint16_t multi_sample_id);
static void brick6_sampler_runtime_multi_service_stream_releases(void);
static sample_audio_key_t brick6_sampler_runtime_multi_key(uint16_t multi_sample_id);
static void brick6_sampler_runtime_multi_diag_note_page0_reject(
    uint8_t track_id,
    uint8_t note,
    uint8_t velocity,
    uint16_t instrument_id,
    const multi_sample_audio_source_t *resolved,
    sample_page_state_t state0);
static void brick6_sampler_runtime_multi_diag_note_stop(
    const brick6_sampler_voice_t *voice,
    uint8_t reason);
static void brick6_sampler_runtime_voice_note_output(brick6_sampler_voice_t *voice,
                                                     float out_l,
                                                     float out_r);
static uint8_t brick6_sampler_runtime_begin_declick_tail(uint8_t track_id,
                                                         brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_refresh_track_activity(uint8_t track_id);
static void brick6_sampler_runtime_start_declick_fade_in(brick6_sampler_voice_t *voice);
static uint8_t brick6_sampler_runtime_apply_start_fade(brick6_sampler_voice_t *voice,
                                                       float *fade,
                                                       uint32_t frames);
static float brick6_sampler_runtime_fade_gain(uint32_t frame_index,
                                              uint32_t loop_frames,
                                              uint32_t fade_in_frames,
                                              uint32_t fade_out_frames);
static void brick6_sampler_runtime_mix_declick_tails(uint8_t track_id,
                                                     float *out_l,
                                                     float *out_r,
                                                     uint32_t frames);
static void brick6_sampler_runtime_mix_declick_tails_mono(uint8_t track_id,
                                                          float *out_mono,
                                                          uint32_t frames);
static brick6_sampler_voice_t *brick6_sampler_runtime_multi_alloc_voice(uint8_t track_id);
static void brick6_sampler_runtime_multi_track_reset(uint8_t track_id);
static brick6_sampler_multi_voice_handle_t
brick6_sampler_runtime_multi_voice_handle(const brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_multi_release_dsp_slot(brick6_sampler_voice_t *voice);
static brick6_sample_common_plan_result_t brick6_sampler_runtime_build_common_play_plan(
    const brick6_sample_common_trigger_t *trigger,
    sample_resolved_source_t *out_source,
    sample_play_plan_t *out_plan);
static void brick6_sampler_runtime_note_common_play_plan_result(
    brick6_sample_common_plan_result_t result,
    uint8_t classic);
void brick6_sampler_runtime_diag_reset(void);
void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot);

/* Private implementation fragments intentionally share this translation unit.
 * This preserves the existing static state, symbol visibility and call order. */
#include "sampler_audio_common.inc"

#include "sampler_clip_voice.inc"

#include "sampler_ram_control.inc"

#include "sampler_audio_dispatch.inc"

#include "sampler_multi_voice.inc"

#include "sampler_audio_diagnostics.inc"

#include "sampler_stream_multi_render.inc"

#include "sampler_ram_voice.inc"

#include "sampler_audio_render.inc"
