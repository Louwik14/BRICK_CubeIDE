/**
 * @file mixer.c
 * @brief Moteur de mixage final track-based (gain/pan/mute/routing/inserts/sends).
 *
 * Rôle du module:
 * - Maintenir l'état runtime des tracks du mixer.
 * - Effectuer le mix final MAIN avec inserts et send FX.
 *
 * Architecture:
 * - Appelé par: my_dsp() (brick6_app_init.c via dsp_engine).
 * - Appelle: fx_chain_process_track_inserts_pre_fader(),
 *   fx_chain_process_audio_fx_post_fader(),
 *   fx_chain_process_global_slot(), audio_float_set_master_gain().
 *
 * Contraintes temps réel:
 * - IRQ: oui (mixer_process est dans le chemin DSP audio).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Slots insert/send à -1 => FX inactif (coût CPU nul sur le slot).
 */

#include "mixer.h"
#include "Audio/audio_float.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/audio_waveform_capture_audio.h"
#include "Audio/audio_io.h"
#include "Audio/audio_rec_level_producer.h"
#include "IPC/control_audio_command.h"

#include "env_adsr.h"
#include "vca_env.h"
#include "fx_biquad_filter.h"
#include "fx_deluge_filter.h"
#include "fx_delay_dual.h"
#include "fx_delay_stereo.h"
#include "Audio/fx_modfx_global.h"
#include "fx_reverb.h"
#include "Param/spectral_window.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/fm_engine.h"
#include "Track/synth_polyphony.h"
#include "Track/track_types.h"
#include "Audio/audio_rec_bus_runtime.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/multi_voice_dsp.h"

#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/control_routing_audio.h"

#include <math.h>
#include <string.h>

#include "fx_chain.h"
#include "fx_pool.h"
#include "Platform/memory_layout.h"

typedef struct __attribute__((aligned(32))) {
    float gain;
    float pan;
    float gain_current;
    float pan_current;
    float mute_gain_current;
    uint8_t mute;

    uint8_t route_master;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
    float send_level_current[MIXER_NUM_SENDS];
    float group_fx_level[2];
    float group_fx_level_current[2];
} mixer_audio_track_runtime_t;

_Static_assert(sizeof(mixer_audio_track_runtime_t) == 64U,
               "GROUP local levels must stay inside mixer track padding");

typedef enum
{
    MIXER_FILTER_DSP_STEREO = 0U,
    MIXER_FILTER_DSP_MONO = 1U
} mixer_filter_dsp_format_t;

typedef enum
{
    MIXER_CONT_MORPH = 0,
    MIXER_CONT_CUTOFF,
    MIXER_CONT_RESONANCE,
    MIXER_CONT_EG_AMOUNT,
    MIXER_CONT_FILTER_ATTACK,
    MIXER_CONT_FILTER_DECAY,
    MIXER_CONT_FILTER_SUSTAIN,
    MIXER_CONT_FILTER_RELEASE,
    MIXER_CONT_VCA_ATTACK,
    MIXER_CONT_VCA_DECAY,
    MIXER_CONT_VCA_SUSTAIN,
    MIXER_CONT_VCA_RELEASE,
    MIXER_CONT_COUNT
} mixer_continuous_param_t;

typedef struct __attribute__((aligned(32))) {
    union {
        fx_biquad_filter_t biquad;
        fx_biquad_filter_mono_t biquad_mono;
        fx_deluge_filter_t deluge;
    };
    env_adsr_t filter_env;
    env_adsr_t vca_env;
    vca_env_t synth_vca_env;
    float sample_rate;
    float cutoff_hz;
    float cutoff_target_hz;
    float cutoff_mod_hz;
    float cutoff_mod_target_hz;
    float resonance;
    float resonance_target;
    float eg_amount;
    float keytrack;
    float keytrack_ratio;
    float keytrack_ratio_target;
    float morph;
    float morph_target;
    float morph_step;
    float vca_env_value;
    float filter_env_value;
    uint32_t config_version;
    uint32_t continuous_epoch;
    uint32_t continuous_version[MIXER_CONT_COUNT];
    int16_t filter_env_prepared_first[AUDIO_BLOCK_SIZE / 8U];
    int16_t filter_env_prepared_terminal[AUDIO_BLOCK_SIZE / 8U];
    uint16_t filter_env_prepared_frames;
    uint8_t note_active;
    uint8_t current_note;
    uint8_t vca_enabled;
    uint8_t vca_note_active;
    uint8_t vca_note_count;
    uint8_t vca_current_note;
    uint8_t vca_gate;
    uint8_t filter_retrigger_hard;
    uint8_t vca_retrigger_hard;
    uint8_t filter_mode;
    uint8_t morph_ramp_remaining;
    uint8_t filter_env_prepared_count;
    uint8_t filter_env_prepared_consumed;
    uint8_t dsp_format;
} mixer_track_filter_t;

_Static_assert(_Alignof(mixer_track_filter_t) >= 32U,
               "mixer track filter DSP storage must stay 32-byte aligned");
_Static_assert((sizeof(mixer_track_filter_t) % 32U) == 0U,
               "mixer track filter array stride must preserve DSP alignment");
_Static_assert(sizeof(mixer_track_filter_t) == 416U,
               "mixer track filter size changed; remeasure before accepting it");

typedef enum
{
    MIXER_LANE_SOURCE_NONE = 0U,
    MIXER_LANE_SOURCE_HW_STEREO,
    MIXER_LANE_SOURCE_EXT_STEREO,
    MIXER_LANE_SOURCE_EXT_MONO_NATIVE,
    MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO,
    MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO
} mixer_lane_source_kind_t;

typedef enum
{
    MIXER_LANE_EXEC_STEREO = 0U,
    MIXER_LANE_EXEC_MONO_NATIVE
} mixer_lane_exec_kind_t;

typedef struct
{
    uint8_t active;
    uint8_t hw_enabled;
    uint8_t ext_enabled;
    uint8_t ext_format;
    uint32_t ext_frames;
    mixer_lane_source_kind_t source_kind;
    mixer_lane_exec_kind_t exec_kind;
} mixer_lane_plan_t;

static mixer_audio_track_runtime_t g_tracks[MIXER_MAX_TRACKS];
static int8_t g_send_fx_slot[MIXER_NUM_SENDS];
static AUDIO_HOT mixer_track_filter_t g_track_filters[MIXER_MAX_TRACKS];
static uint32_t g_mixer_filter_config_version;
AUDIO_HOT static mixer_track_filter_t g_poly_filters_hot[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
typedef struct
{
    float effective_hz;
    uint8_t valid;
} mixer_poly_cutoff_override_t;
AUDIO_HOT static mixer_poly_cutoff_override_t
    g_poly_cutoff_override[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
static AUDIO_HOT float g_external_track_mono[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_l[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_r[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static uint8_t g_external_track_enabled[MIXER_MAX_TRACKS];
static uint32_t g_external_lane_mask;
static uint8_t g_external_poly_initialized[MIXER_MAX_TRACKS];
static uint8_t g_external_track_format[MIXER_MAX_TRACKS];
static uint16_t g_external_track_frames_valid[MIXER_MAX_TRACKS];
volatile uint32_t g_mixer_lane_rebind_count[MIXER_MAX_TRACKS];
static float g_looper_xfade_smoothed = 0.0f;
static float g_looper_xfade_prev = 0.0f;

enum
{
    MIXER_STATIC_GROUP_CHILD = 1U << 0,
    MIXER_STATIC_LOOPER = 1U << 1,
    MIXER_STATIC_ROUTE_MAIN = 1U << 2,
    MIXER_STATIC_AUDIO_FX_ACTIVE = 1U << 3,
    MIXER_STATIC_AUDIO_FX_PRE_FILTER = 1U << 4,
    MIXER_STATIC_REQUIRES_STEREO = 1U << 5,
    MIXER_STATIC_INSERT_STAGE = 1U << 6,
    MIXER_STATIC_AUDIO_FX_COMP = 1U << 7
};

static uint8_t g_mixer_static_lane_flags[MIXER_MAX_TRACKS];
static uint8_t g_mixer_static_group_active;

/* Private implementation fragments intentionally share this translation unit.
 * This preserves the existing static state, symbol visibility and DSP order. */
#include "Mixer/mixer_plan.inc"

#include "Mixer/mixer_track_dsp.inc"

#include "Mixer/mixer_poly_dsp.inc"

#include "Mixer/mixer_global_fx.inc"

#include "Mixer/mixer_track_io.inc"

#include "Mixer/mixer_process.inc"
