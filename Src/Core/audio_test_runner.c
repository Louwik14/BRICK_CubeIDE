#include "Core/audio_test_runner.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Audio/audio_track_diag.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/cpu_load.h"
#include "Core/track_runtime.h"
#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "Mod/mod_matrix.h"
#include "Param/param_registry.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/audio_test_csv.h"
#include "Storage/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "UI/ui_core.h"
#include "UI/ui_hall_mode_state.h"
#include "stm32h7xx_hal.h"

#define AUDIO_TEST_WARNING_MS 3000U
#define AUDIO_TEST_WARMUP_MS 300U
#define AUDIO_TEST_ATTACK_MS 100U
#define AUDIO_TEST_STABILIZE_AFTER_ATTACK_MS 200U
#define AUDIO_TEST_PERCUSSIVE_MIN_MS 300U
#define AUDIO_TEST_PERCUSSIVE_MAX_MS 3000U
#define AUDIO_TEST_PERCUSSIVE_POLL_MS 50U
#define AUDIO_TEST_PERCUSSIVE_SILENCE_POLLS 3U
#define AUDIO_TEST_SILENCE_RMS 0.00001f
#define AUDIO_TEST_MEASURE_MS 500U
#define AUDIO_TEST_ENGINE_MEASURE_MS 1000U
#define AUDIO_TEST_FX_WARMUP_MS 1000U
#define AUDIO_TEST_FX_ACTIVE_MS 2000U
#define AUDIO_TEST_FX_TAIL_EARLY_MS 1000U
#define AUDIO_TEST_FX_TAIL_LATE_MS 2000U
#define AUDIO_TEST_FX_TAIL_TOTAL_MS \
    (AUDIO_TEST_FX_TAIL_EARLY_MS + AUDIO_TEST_FX_TAIL_LATE_MS)
#define AUDIO_TEST_ENGINE_TRACKS 12U
#define AUDIO_TEST_NOTE_MAX 12U
#define AUDIO_TEST_CAL_PRISM_MODELS 39U
#define AUDIO_TEST_CAL_STACK_MODELS 13U
#define AUDIO_TEST_CAL_DELUGE_MODELS 6U
#define AUDIO_TEST_CAL_MODEL_COUNT 62U
#define AUDIO_TEST_CAL_ENGINE_CASES 3726U
#define AUDIO_TEST_LEGACY_CASES 26U
#define AUDIO_TEST_CAL_MAX_OBSERVATIONS 256U
#define AUDIO_TEST_CAL_TARGET_DBFS (-18.0f)

typedef enum
{
    TEST_ENGINE_PRISM = 0,
    TEST_ENGINE_STACK,
    TEST_ENGINE_DELUGE,
    TEST_ENGINE_WAVE,
    TEST_ENGINE_SAMPLER,
    TEST_ENGINE_DRUM_MD,
    TEST_ENGINE_DRUM_ANALOG
} test_engine_t;

typedef enum
{
    TEST_SOUND_CONTINUOUS = 0,
    TEST_SOUND_PERCUSSIVE,
    TEST_SOUND_NOISY_RANDOM
} test_sound_type_t;

typedef struct
{
    test_engine_t engine;
    uint8_t model;
    uint8_t source_count;
    uint8_t track_count;
    uint8_t voice_count;
    uint8_t note_count;
    uint8_t notes[AUDIO_TEST_NOTE_MAX];
    uint8_t filter_type;
    uint8_t filter_cutoff;
    uint8_t filter_resonance;
    uint8_t delay;
    uint8_t reverb;
    uint8_t master_fx;
    uint8_t master_gain_max;
    uint8_t musical;
    uint8_t fx_tail_test;
    uint8_t coherent_sum;
    uint8_t calibration;
    uint8_t model_key;
    uint8_t repetition;
    uint8_t oscillator_mode;
    uint8_t timbre_setting;
    uint8_t color_setting;
    uint8_t velocity;
    test_sound_type_t sound_type;
    float timbre;
    float color;
    float delay_send;
    float reverb_send;
    float delay_mix;
    float delay_feedback;
    float delay_time;
    float reverb_mix;
    float reverb_size;
    float reverb_decay;
    float reverb_damping;
    char phase[12];
    char name[32];
    char sources[32];
    char filter[32];
    char fx[32];
    char master[32];
} audio_test_case_t;

typedef struct
{
    uint16_t count;
    float weighted[AUDIO_TEST_CAL_MAX_OBSERVATIONS];
    float rms[AUDIO_TEST_CAL_MAX_OBSERVATIONS];
    float peak[AUDIO_TEST_CAL_MAX_OBSERVATIONS];
    float crest[AUDIO_TEST_CAL_MAX_OBSERVATIONS];
    float worst_dc;
    float max_peak;
    float weakest_weighted;
    float strongest_weighted;
    uint32_t total_clips;
    char weakest_scenario[32];
    char strongest_scenario[32];
} audio_test_cal_model_stats_t;

typedef struct
{
    audio_test_runner_state_t state;
    uint16_t index;
    uint16_t strongest_index;
    float strongest_peak;
    float strongest_prism_peak;
    float strongest_stack_peak;
    uint8_t strongest_prism_model;
    uint8_t strongest_stack_model;
    uint32_t state_since_ms;
    uint32_t run_id;
    uint8_t cancel_requested;
    uint8_t snapshot_valid;
    uint8_t restore_transport;
    uint8_t csv_ready;
    uint8_t saved_active_track;
    uint16_t calibration_sample_slot;
    uint16_t calibration_sample_global;
    uint16_t calibration_wavetable_slot;
    uint16_t calibration_wavetable_global;
    seq_step_id_t saved_playhead[SEQ_TRACK_COUNT];
    ui_hall_mode_t saved_hall_mode;
    audio_test_case_t current;
    audio_track_diag_snapshot_t track_capture;
    audio_global_diag_snapshot_t global_capture;
    audio_track_diag_snapshot_t attack_track_capture;
    audio_global_diag_snapshot_t attack_global_capture;
    audio_track_diag_snapshot_t tail_early_track;
    audio_global_diag_snapshot_t tail_early_global;
    audio_track_diag_snapshot_t tail_late_track;
    audio_global_diag_snapshot_t tail_late_global;
    audio_track_diag_snapshot_t tail_track_capture;
    audio_global_diag_snapshot_t tail_global_capture;
    cpu_load_metrics_t active_cpu;
    cpu_load_metrics_t attack_cpu;
    cpu_load_metrics_t tail_cpu;
    uint8_t write_phase;
    uint8_t sum_progression_fail;
    uint8_t coherent_sum_count;
    float coherent_sum_peak;
    float coherent_sum_rms;
    float sum_expected_ratio;
    float sum_peak_ratio;
    float sum_rms_ratio;
    uint32_t percussive_last_poll_ms;
    float percussive_last_energy;
    uint32_t percussive_last_samples;
    uint8_t percussive_silent_polls;
    uint8_t summary_index;
} audio_test_runner_t;

STORAGE_STATE_SDRAM static PatternSaveV1 g_saved_project_state;
STORAGE_STATE_SDRAM static audio_test_cal_model_stats_t
    g_cal_stats[AUDIO_TEST_CAL_MODEL_COUNT];
STORAGE_STATE_SDRAM static float
    g_cal_sort_scratch[AUDIO_TEST_CAL_MAX_OBSERVATIONS];
static audio_test_runner_t g_runner;

static uint8_t elapsed(uint32_t duration_ms)
{
    return ((uint32_t)(HAL_GetTick() - g_runner.state_since_ms) >= duration_ms) ? 1U : 0U;
}

static void set_state(audio_test_runner_state_t state)
{
    g_runner.state = state;
    g_runner.state_since_ms = HAL_GetTick();
}

static uint32_t active_measure_ms(void)
{
    if (g_runner.current.fx_tail_test != 0U)
    {
        return AUDIO_TEST_FX_ACTIVE_MS;
    }
    if (g_runner.current.calibration != 0U)
    {
        return AUDIO_TEST_ENGINE_MEASURE_MS;
    }
    return (strcmp(g_runner.current.phase, "ENGINES") == 0)
        ? AUDIO_TEST_ENGINE_MEASURE_MS : AUDIO_TEST_MEASURE_MS;
}

static void set_track_param(uint8_t track, param_id_t id, float value)
{
    (void)param_registry_apply_track_value(id, track, value);
}

static void all_test_notes_off(void)
{
    for (uint8_t track = 0U; track < AUDIO_TEST_ENGINE_TRACKS; ++track)
    {
        keyboard_engine_all_notes_off_for_track(track);
    }
    keyboard_runtime_all_notes_off();
    keyboard_engine_all_notes_off();
}

static uint8_t prism_is_percussive(uint8_t model)
{
    return ((model >= 23U) && (model <= 27U)) ? 1U : 0U;
}

static uint8_t prism_is_random(uint8_t model)
{
    switch (model)
    {
        case 22U: case 26U: case 27U: case 32U: case 33U:
        case 34U: case 35U: case 36U: case 38U:
            return 1U;
        default:
            return 0U;
    }
}

static uint16_t prism_model_case_count(uint8_t model)
{
    return (uint16_t)(45U * ((prism_is_random(model) != 0U) ? 3U : 1U));
}

static uint16_t stack_model_case_count(uint8_t model)
{
    return (uint16_t)(75U * ((model == (uint8_t)BRICK6_STACK_MODEL_SWARM)
                            ? 3U : 1U));
}

static const char *oscillator_mode_name(uint8_t mode)
{
    switch (mode)
    {
        case 0U: return "ONE";
        case 1U: return "TWO_IDENTICAL";
        case 2U: return "MAX_IDENTICAL";
        case 3U: return "TWO_DETUNED";
        case 4U: return "MAX_DETUNED";
        default: return "ONE";
    }
}

static void build_engine_case(uint16_t index, audio_test_case_t *out)
{
    static const uint8_t notes[3] = { 36U, 60U, 84U };
    static const float tone_values[5][2] = {
        { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 0.5f, 0.5f },
        { 1.0f, 0.0f }, { 1.0f, 1.0f }
    };
    memset(out, 0, sizeof(*out));
    out->calibration = 1U;
    out->track_count = 1U;
    out->voice_count = 1U;
    out->note_count = 1U;
    out->source_count = 1U;
    out->filter_cutoff = 127U;
    out->velocity = 127U;
    out->timbre = -1.0f;
    out->color = -1.0f;
    out->sound_type = TEST_SOUND_CONTINUOUS;
    (void)snprintf(out->phase, sizeof(out->phase), "CAL");
    (void)snprintf(out->filter, sizeof(out->filter), "OFF");
    (void)snprintf(out->fx, sizeof(out->fx), "DRY");
    (void)snprintf(out->master, sizeof(out->master), "NOMINAL");

    uint16_t local = index;
    for (uint8_t model = 0U; model < AUDIO_TEST_CAL_PRISM_MODELS; ++model)
    {
        const uint16_t count = prism_model_case_count(model);
        if (local < count)
        {
            const uint16_t base = (uint16_t)(local % 45U);
            out->engine = TEST_ENGINE_PRISM;
            out->model = model;
            out->model_key = model;
            out->repetition = (uint8_t)(local / 45U + 1U);
            out->oscillator_mode = (uint8_t)(base / 15U);
            const uint8_t tone = (uint8_t)(base % 15U);
            out->notes[0] = notes[tone / 5U];
            out->timbre_setting = (uint8_t)(tone % 5U);
            out->color_setting = out->timbre_setting;
            out->timbre = tone_values[out->timbre_setting][0];
            out->color = tone_values[out->color_setting][1];
            out->source_count =
                (out->oscillator_mode == 0U) ? 1U : 2U;
            out->sound_type = (prism_is_percussive(model) != 0U)
                ? TEST_SOUND_PERCUSSIVE
                : ((prism_is_random(model) != 0U)
                    ? TEST_SOUND_NOISY_RANDOM : TEST_SOUND_CONTINUOUS);
            (void)snprintf(out->name, sizeof(out->name),
                           "PRISM_%02u_N%u_T%u_O%u_R%u",
                           (unsigned)model, (unsigned)out->notes[0],
                           (unsigned)out->timbre_setting,
                           (unsigned)out->oscillator_mode,
                           (unsigned)out->repetition);
            (void)snprintf(out->sources, sizeof(out->sources), "%s %s",
                           (out->oscillator_mode == 0U) ? "ONE"
                         : (out->oscillator_mode == 1U)
                            ? "TWO_IDENTICAL" : "TWO_DETUNED",
                           (out->source_count == 1U) ? "L=1/0" : "L=1/1");
            return;
        }
        local = (uint16_t)(local - count);
    }
    for (uint8_t model = 0U; model < AUDIO_TEST_CAL_STACK_MODELS; ++model)
    {
        const uint16_t count = stack_model_case_count(model);
        if (local < count)
        {
            const uint16_t base = (uint16_t)(local % 75U);
            out->engine = TEST_ENGINE_STACK;
            out->model = model;
            out->model_key = (uint8_t)(39U + model);
            out->repetition = (uint8_t)(local / 75U + 1U);
            out->oscillator_mode = (uint8_t)(base / 15U);
            const uint8_t tone = (uint8_t)(base % 15U);
            out->notes[0] = notes[tone / 5U];
            out->timbre_setting = (uint8_t)(tone % 5U);
            out->color_setting = out->timbre_setting;
            out->timbre = tone_values[out->timbre_setting][0];
            out->color = tone_values[out->color_setting][1];
            out->source_count =
                ((out->oscillator_mode == 0U) ? 1U
                 : ((out->oscillator_mode == 1U)
                    || (out->oscillator_mode == 3U)) ? 2U : 3U);
            out->sound_type = (model == (uint8_t)BRICK6_STACK_MODEL_SWARM)
                ? TEST_SOUND_NOISY_RANDOM : TEST_SOUND_CONTINUOUS;
            (void)snprintf(out->name, sizeof(out->name),
                           "STACK_%02u_N%u_T%u_O%u_R%u",
                           (unsigned)model, (unsigned)out->notes[0],
                           (unsigned)out->timbre_setting,
                           (unsigned)out->oscillator_mode,
                           (unsigned)out->repetition);
            (void)snprintf(out->sources, sizeof(out->sources), "%s %s",
                           oscillator_mode_name(out->oscillator_mode),
                           (out->source_count == 1U) ? "L=1/0/0"
                         : (out->source_count == 2U) ? "L=1/1/0"
                                                   : "L=1/1/1");
            return;
        }
        local = (uint16_t)(local - count);
    }
    if (local < 18U)
    {
        out->engine = TEST_ENGINE_DELUGE;
        out->model = (uint8_t)(local / 3U);
        out->model_key = (uint8_t)(52U + out->model);
        out->notes[0] = notes[local % 3U];
        (void)snprintf(out->name, sizeof(out->name), "DELUGE_%02u_N%u",
                       (unsigned)out->model, (unsigned)out->notes[0]);
        (void)snprintf(out->sources, sizeof(out->sources), "ONE");
        return;
    }
    local = (uint16_t)(local - 18U);
    if (local < 9U)
    {
        out->engine = TEST_ENGINE_WAVE;
        out->model_key = 58U;
        out->oscillator_mode = (uint8_t)(local / 3U);
        out->source_count =
            (out->oscillator_mode == 0U) ? 1U : 2U;
        out->notes[0] = notes[local % 3U];
        (void)snprintf(out->name, sizeof(out->name), "WAVE_N%u_O%u",
                       (unsigned)out->notes[0],
                       (unsigned)out->oscillator_mode);
        (void)snprintf(out->sources, sizeof(out->sources), "%s %s",
                       (out->oscillator_mode == 0U) ? "ONE"
                     : (out->oscillator_mode == 1U)
                        ? "TWO_IDENTICAL" : "TWO_DETUNED",
                       (out->source_count == 1U) ? "L=1/0" : "L=1/1");
        return;
    }
    local = (uint16_t)(local - 9U);
    if (local < 3U)
    {
        out->engine = TEST_ENGINE_SAMPLER;
        out->model_key = 59U;
        out->notes[0] = notes[local];
        out->sound_type = TEST_SOUND_PERCUSSIVE;
        (void)snprintf(out->name, sizeof(out->name), "SAMPLER_N%u",
                       (unsigned)out->notes[0]);
        (void)snprintf(out->sources, sizeof(out->sources), "CAL_SAMPLE");
        return;
    }
    local = (uint16_t)(local - 3U);
    out->engine = (local < 3U) ? TEST_ENGINE_DRUM_MD
                               : TEST_ENGINE_DRUM_ANALOG;
    out->model = (local < 3U) ? 0U : 1U;
    out->model_key = (uint8_t)(60U + out->model);
    out->notes[0] = notes[local % 3U];
    out->sound_type = TEST_SOUND_PERCUSSIVE;
    (void)snprintf(out->name, sizeof(out->name), "%s_N%u",
                   (local < 3U) ? "DRUM_MD" : "DRUM_ANALOG",
                   (unsigned)out->notes[0]);
    (void)snprintf(out->sources, sizeof(out->sources), "STRIKE");
}

static void build_case(uint16_t index, audio_test_case_t *out)
{
    if (index < AUDIO_TEST_CAL_ENGINE_CASES)
    {
        build_engine_case(index, out);
        return;
    }
    build_engine_case(0U, out);
    out->calibration = 0U;
    index = (uint16_t)(index - AUDIO_TEST_CAL_ENGINE_CASES);
    if (index < 8U)
    {
        static const char *const names[8] = {
            "FILTER_OFF", "FILTER_LP_OPEN", "FILTER_LP_1K_MED_Q",
            "FILTER_LP_HIGH_Q", "FILTER_HP_REP", "FILTER_BP_REP",
            "FILTER_DJ_NEUTRAL", "FILTER_DJ_BOOST"
        };
        static const uint8_t types[8] = { 0U, 1U, 1U, 1U, 2U, 3U, 4U, 4U };
        static const uint8_t cutoff[8] = { 127U, 127U, 72U, 72U, 64U, 64U, 64U, 64U };
        static const uint8_t resonance[8] = { 0U, 0U, 64U, 112U, 48U, 64U, 0U, 0U };
        const uint8_t variant = (uint8_t)index;
        out->filter_type = types[variant];
        out->filter_cutoff = cutoff[variant];
        out->filter_resonance = resonance[variant];
        (void)snprintf(out->phase, sizeof(out->phase), "FILTER");
        (void)snprintf(out->name, sizeof(out->name), "%s", names[variant]);
        (void)snprintf(out->filter, sizeof(out->filter), "TYPE%u C%u Q%u",
                       (unsigned)out->filter_type, (unsigned)out->filter_cutoff,
                       (unsigned)out->filter_resonance);
        return;
    }
    if (index < 15U)
    {
        static const uint8_t counts[5] = { 1U, 2U, 4U, 8U, 12U };
        static const uint8_t musical[12] = {
            48U, 55U, 60U, 64U, 67U, 71U,
            74U, 79U, 52U, 59U, 62U, 76U
        };
        const uint8_t variant = (uint8_t)(index - 8U);
        out->track_count = (variant < 5U) ? counts[variant] : 12U;
        out->musical = (variant == 5U) ? 1U : 0U;
        out->coherent_sum = (variant < 5U) ? 1U : 0U;
        out->delay = (variant == 6U) ? 1U : 0U;
        out->reverb = (variant == 6U) ? 1U : 0U;
        out->fx_tail_test = (variant == 6U) ? 1U : 0U;
        out->delay_send = (variant == 6U) ? 0.35f : 0.0f;
        out->reverb_send = (variant == 6U) ? 0.35f : 0.0f;
        out->delay_mix = (variant == 6U) ? 0.55f : 0.0f;
        out->delay_feedback = (variant == 6U) ? 0.45f : 0.0f;
        out->delay_time = (variant == 6U) ? 7.0f : 0.0f;
        out->reverb_mix = (variant == 6U) ? 0.55f : 0.0f;
        out->reverb_size = (variant == 6U) ? 0.60f : 0.0f;
        out->reverb_decay = (variant == 6U) ? 0.58f : 0.0f;
        out->reverb_damping = (variant == 6U) ? 0.40f : 0.0f;
        out->note_count = out->track_count;
        out->voice_count = out->track_count;
        for (uint8_t i = 0U; i < out->note_count; ++i)
        {
            out->notes[i] = (out->musical != 0U) ? musical[i] : 60U;
        }
        (void)snprintf(out->phase, sizeof(out->phase), "SUM");
        (void)snprintf(out->name, sizeof(out->name), "%s",
            (variant == 0U) ? "MASTER_1TRACK_COHERENT"
          : (variant == 1U) ? "MASTER_2TRACKS_COHERENT"
          : (variant == 2U) ? "MASTER_4TRACKS_COHERENT"
          : (variant == 3U) ? "MASTER_8TRACKS_COHERENT"
          : (variant == 4U) ? "MASTER_12TRACKS_COHERENT"
          : (variant == 5U) ? "MASTER_12TRACKS_MUSICAL"
                            : "MASTER_12TRACKS_DELAY_REVERB");
        (void)snprintf(out->sources, sizeof(out->sources), "%s",
            (variant < 5U) ? "IDENTICAL_ENGINE_NOTE_PHASE"
          : (variant == 5U) ? "12TRACK_MUSICAL_NOTES"
                            : "12TRACK_BOTH_FX_SENDS");
        if (variant == 6U)
        {
            (void)snprintf(out->fx, sizeof(out->fx), "D1 R1");
        }
        return;
    }

    if (index < 20U)
    {
        const uint8_t variant = (uint8_t)(index - 15U);
        out->track_count = 12U;
        out->note_count = 12U;
        out->voice_count = 12U;
        for (uint8_t i = 0U; i < out->track_count; ++i)
        {
            out->notes[i] = 60U;
        }
        out->master_fx = (variant == 2U) ? 1U : 0U;
        out->master_gain_max = (variant == 4U) ? 1U : 0U;
        (void)snprintf(out->phase, sizeof(out->phase), "MASTER");
        (void)snprintf(out->name, sizeof(out->name), "%s",
            (variant == 0U) ? "MASTER_DRY"
          : (variant == 1U) ? "MASTER_FX_BYPASS"
          : (variant == 2U) ? "MASTER_FX_ACTIVE"
          : (variant == 3U) ? "MASTER_GAIN_NOMINAL"
                            : "MASTER_GAIN_MAX");
        (void)snprintf(out->fx, sizeof(out->fx), "D0 R0 MFX%u",
                       (unsigned)out->master_fx);
        (void)snprintf(out->master, sizeof(out->master), "%s",
                       (out->master_gain_max != 0U) ? "MAX" : "NOMINAL");
        return;
    }

    const uint8_t variant = (uint8_t)(index - 20U);
    out->fx_tail_test = 1U;
    out->delay = ((variant == 0U) || (variant == 1U)
                  || (variant == 4U) || (variant == 5U)) ? 1U : 0U;
    out->reverb = (variant >= 2U) ? 1U : 0U;
    out->track_count = (variant == 5U) ? 12U : 1U;
    out->note_count = out->track_count;
    out->voice_count = out->track_count;
    for (uint8_t i = 0U; i < out->track_count; ++i)
    {
        out->notes[i] = 60U;
    }
    out->delay_send = (out->delay != 0U)
        ? ((variant == 5U) ? 0.35f : ((variant == 1U) ? 0.60f : 0.50f))
        : 0.0f;
    out->reverb_send = (out->reverb != 0U)
        ? ((variant == 5U) ? 0.35f : 0.50f)
        : 0.0f;
    out->delay_mix = (out->delay != 0U) ? 0.55f : 0.0f;
    out->delay_feedback = (variant == 1U) ? 0.82f
        : ((out->delay != 0U) ? 0.45f : 0.0f);
    out->delay_time = 7.0f;
    out->reverb_mix = (out->reverb != 0U) ? 0.55f : 0.0f;
    out->reverb_size = ((variant == 3U) ? 0.85f
        : ((out->reverb != 0U) ? 0.60f : 0.0f));
    out->reverb_decay = ((variant == 3U) ? 0.90f
        : ((out->reverb != 0U) ? 0.58f : 0.0f));
    out->reverb_damping = (out->reverb != 0U) ? 0.40f : 0.0f;
    (void)snprintf(out->phase, sizeof(out->phase), "FX");
    (void)snprintf(out->name, sizeof(out->name), "%s",
        (variant == 0U) ? "FX_DELAY_MODERATE"
      : (variant == 1U) ? "FX_DELAY_HIGH_FEEDBACK"
      : (variant == 2U) ? "FX_REVERB_MODERATE_DECAY"
      : (variant == 3U) ? "FX_REVERB_HIGH_DECAY"
      : (variant == 4U) ? "FX_DELAY_REVERB"
                        : "FX_MULTI_TRACK_DELAY_REVERB");
    (void)snprintf(out->fx, sizeof(out->fx), "D%u R%u",
                   (unsigned)out->delay, (unsigned)out->reverb);
    (void)snprintf(out->master, sizeof(out->master), "NOMINAL");
}

static uint8_t engine_family_type(test_engine_t engine,
                                  ui_track_family_t *family,
                                  ui_track_type_t *type)
{
    if ((family == 0) || (type == 0))
    {
        return 0U;
    }
    *family = UI_TRACK_FAMILY_SYNTH;
    switch (engine)
    {
        case TEST_ENGINE_PRISM: *type = UI_TRACK_TYPE_PRISM; break;
        case TEST_ENGINE_STACK: *type = UI_TRACK_TYPE_STACK; break;
        case TEST_ENGINE_DELUGE: *type = UI_TRACK_TYPE_DELUGE; break;
        case TEST_ENGINE_WAVE: *type = UI_TRACK_TYPE_WAVE; break;
        case TEST_ENGINE_SAMPLER:
            *family = UI_TRACK_FAMILY_SAMPLER;
            *type = UI_TRACK_TYPE_RAM;
            break;
        case TEST_ENGINE_DRUM_MD:
            *family = UI_TRACK_FAMILY_DRUM;
            *type = UI_TRACK_TYPE_DRUM_MD;
            break;
        case TEST_ENGINE_DRUM_ANALOG:
            *family = UI_TRACK_FAMILY_DRUM;
            *type = UI_TRACK_TYPE_DRUM_BD_ANALOG;
            break;
        default:
            return 0U;
    }
    return 1U;
}

static void neutralize_track(uint8_t track)
{
    set_track_param(track, PARAM_MIX_LEVEL, 1.0f);
    set_track_param(track, PARAM_MIX_PAN, 0.0f);
    set_track_param(track, PARAM_MIX_SEND1, 0.0f);
    set_track_param(track, PARAM_MIX_SEND2, 0.0f);
    set_track_param(track, PARAM_FILTER_TYPE, 0.0f);
    set_track_param(track, PARAM_FILTER_CUTOFF, 127.0f);
    set_track_param(track, PARAM_FILTER_RESONANCE, 0.0f);
    set_track_param(track, PARAM_FILTER_DRIVE, 0.0f);
    set_track_param(track, PARAM_FILTER_EQ_LOW, 64.0f);
    set_track_param(track, PARAM_FILTER_EQ_MID, 64.0f);
    set_track_param(track, PARAM_FILTER_EQ_HIGH, 64.0f);
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        (void)mod_matrix_set_slot_source(track, slot, 0.0f);
        (void)mod_matrix_set_slot_depth(track, slot, 0.0f);
        (void)mod_matrix_set_slot_destination_index(track, slot, (float)PARAM_COUNT);
    }
    mod_matrix_rebuild_route_cache_track(track);
}

static void configure_engine_params(uint8_t track, const audio_test_case_t *test)
{
    switch (test->engine)
    {
        case TEST_ENGINE_PRISM:
            set_track_param(track, PARAM_PRISM_EDIT, (float)test->model);
            set_track_param(track, PARAM_PRISM_OSC2_EDIT, (float)test->model);
            set_track_param(track, PARAM_PRISM_LEVEL, 1.0f);
            set_track_param(track, PARAM_PRISM_OSC2_LEVEL,
                            (test->source_count >= 2U) ? 1.0f : 0.0f);
            set_track_param(track, PARAM_PRISM_FINE, 0.5f);
            set_track_param(track, PARAM_PRISM_OSC2_FINE,
                            (test->oscillator_mode == 2U) ? 0.51f : 0.5f);
            set_track_param(track, PARAM_PRISM_TIMBRE, test->timbre);
            set_track_param(track, PARAM_PRISM_COLOR, test->color);
            set_track_param(track, PARAM_PRISM_OSC2_TIMBRE, test->timbre);
            set_track_param(track, PARAM_PRISM_OSC2_COLOR, test->color);
            set_track_param(track, PARAM_PRISM_PHASE_RESET, 1.0f);
            set_track_param(track, PARAM_PRISM_OSC2_PHASE_RESET, 1.0f);
            break;
        case TEST_ENGINE_STACK:
            set_track_param(track, PARAM_STACK_OSC1_MODEL, (float)test->model);
            set_track_param(track, PARAM_STACK_OSC2_MODEL, (float)test->model);
            set_track_param(track, PARAM_STACK_OSC3_MODEL, (float)test->model);
            set_track_param(track, PARAM_STACK_OSC1_LEVEL, 1.0f);
            set_track_param(track, PARAM_STACK_OSC2_LEVEL,
                            (test->source_count >= 2U) ? 1.0f : 0.0f);
            set_track_param(track, PARAM_STACK_OSC3_LEVEL,
                            (test->source_count >= 3U) ? 1.0f : 0.0f);
            set_track_param(track, PARAM_STACK_OSC1_TIMBRE, test->timbre);
            set_track_param(track, PARAM_STACK_OSC1_COLOR, test->color);
            set_track_param(track, PARAM_STACK_OSC2_TIMBRE, test->timbre);
            set_track_param(track, PARAM_STACK_OSC2_COLOR, test->color);
            set_track_param(track, PARAM_STACK_OSC3_TIMBRE, test->timbre);
            set_track_param(track, PARAM_STACK_OSC3_COLOR, test->color);
            set_track_param(track, PARAM_STACK_OSC1_PARAM3, test->color);
            set_track_param(track, PARAM_STACK_OSC2_PARAM3, test->color);
            set_track_param(track, PARAM_STACK_OSC3_PARAM3, test->color);
            set_track_param(track, PARAM_STACK_OSC1_TUNE, 0.0f);
            set_track_param(track, PARAM_STACK_OSC2_TUNE, 0.0f);
            set_track_param(track, PARAM_STACK_OSC3_TUNE, 0.0f);
            set_track_param(track, PARAM_STACK_OSC_DETUNE,
                            (test->oscillator_mode >= 3U) ? 0.02f : 0.0f);
            set_track_param(track, PARAM_STACK_NOISE_LEVEL, 0.0f);
            set_track_param(track, PARAM_STACK_PHASE_RESET, 1.0f);
            break;
        case TEST_ENGINE_DELUGE:
            set_track_param(track, PARAM_DELUGE_MODEL, (float)test->model);
            set_track_param(track, PARAM_DELUGE_LEVEL, 1.0f);
            set_track_param(track, PARAM_DELUGE_PHASE, 0.0f);
            set_track_param(track, PARAM_DELUGE_RETRIG, 1.0f);
            break;
        case TEST_ENGINE_WAVE:
            set_track_param(track, PARAM_WAVE_OSC1_TABLE,
                            (float)g_runner.calibration_wavetable_global);
            set_track_param(track, PARAM_WAVE_OSC2_TABLE,
                            (float)g_runner.calibration_wavetable_global);
            set_track_param(track, PARAM_WAVE_OSC1_LEVEL, 1.0f);
            set_track_param(track, PARAM_WAVE_OSC2_LEVEL,
                            (test->source_count >= 2U) ? 1.0f : 0.0f);
            set_track_param(track, PARAM_WAVE_OSC1_TUNE, 0.0f);
            set_track_param(track, PARAM_WAVE_OSC2_TUNE,
                            (test->oscillator_mode == 2U) ? 0.12f : 0.0f);
            set_track_param(track, PARAM_WAVE_OSC1_PHASE, 0.0f);
            set_track_param(track, PARAM_WAVE_OSC2_PHASE, 0.0f);
            break;
        case TEST_ENGINE_SAMPLER:
            set_track_param(track, PARAM_SAMPLER_SAMPLE,
                            (float)g_runner.calibration_sample_global);
            set_track_param(track, PARAM_SAMPLER_GAIN, 1.0f);
            break;
        default:
            break;
    }
}

static uint8_t configure_current(void)
{
    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];
    ui_track_family_t engine_family;
    ui_track_type_t engine_type;
    uint8_t fx_track = 0U;
    if (engine_family_type(g_runner.current.engine, &engine_family, &engine_type) == 0U)
    {
        return 0U;
    }
    if (track_topology_find_special(TRACK_TOPOLOGY_ROLE_FX, 0U, &fx_track) == 0U)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        family[track] = (uint8_t)UI_TRACK_FAMILY_OFF;
        type[track] = (uint8_t)UI_TRACK_TYPE_AUDIO;
        midi_channel[track] = (uint8_t)(track + 1U);
        midi_source[track] = (uint8_t)UI_TRACK_MIDI_SRC_INT;
    }
    for (uint8_t track = 0U;
         (track < g_runner.current.track_count) && (track < AUDIO_TEST_ENGINE_TRACKS);
         ++track)
    {
        family[track] = (uint8_t)engine_family;
        type[track] = (uint8_t)engine_type;
    }
    if (!ui_restore_track_config_bulk(family, type, midi_channel, midi_source))
    {
        return 0U;
    }

    param_registry_batch_begin();
    for (uint8_t track = 0U; track < g_runner.current.track_count; ++track)
    {
        neutralize_track(track);
        configure_engine_params(track, &g_runner.current);
        set_track_param(track, PARAM_FILTER_TYPE, (float)g_runner.current.filter_type);
        set_track_param(track, PARAM_FILTER_CUTOFF, (float)g_runner.current.filter_cutoff);
        set_track_param(track, PARAM_FILTER_RESONANCE, (float)g_runner.current.filter_resonance);
        if ((g_runner.current.filter_type == 4U)
            && (strstr(g_runner.current.name, "BOOST") != 0))
        {
            set_track_param(track, PARAM_FILTER_EQ_LOW, 96.0f);
            set_track_param(track, PARAM_FILTER_EQ_MID, 64.0f);
            set_track_param(track, PARAM_FILTER_EQ_HIGH, 96.0f);
        }
        set_track_param(track, PARAM_MIX_SEND1, g_runner.current.delay_send);
        set_track_param(track, PARAM_MIX_SEND2, g_runner.current.reverb_send);
    }
    param_set(PARAM_CFG_METRO, 0.0f);
    param_set(PARAM_MIX_DELAY_VOL, g_runner.current.delay_mix);
    param_set(PARAM_MIX_DELAY_FEEDBACK, g_runner.current.delay_feedback);
    param_set(PARAM_MIX_DELAY_TIME, g_runner.current.delay_time);
    param_set(PARAM_MIX_REVERB_WET, g_runner.current.reverb_mix);
    param_set(PARAM_MIX_REVERB_SIZE, g_runner.current.reverb_size);
    param_set(PARAM_MIX_REVERB_DECAY, g_runner.current.reverb_decay);
    param_set(PARAM_MIX_REVERB_DAMP, g_runner.current.reverb_damping);
    param_set(PARAM_MASTER_GAIN, g_runner.current.master_gain_max ? 1.0f : 0.75f);
    set_track_param(fx_track, PARAM_MASTER_FX1_TYPE,
                    g_runner.current.master_fx ? 1.0f : 0.0f);
    set_track_param(fx_track, PARAM_MASTER_FX1_LEVEL,
                    g_runner.current.master_fx ? 127.0f : 0.0f);
    set_track_param(fx_track, PARAM_MASTER_FX2_LEVEL, 0.0f);
    set_track_param(fx_track, PARAM_MASTER_FX3_LEVEL, 0.0f);
    set_track_param(fx_track, PARAM_MASTER_FX4_LEVEL, 0.0f);
    param_registry_batch_end();

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(0U);
    if ((ctx == 0) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }
    uint16_t used_mix_tracks = 0U;
    for (uint8_t track = 0U; track < g_runner.current.track_count; ++track)
    {
        const track_runtime_ctx_t *const active_ctx =
            track_runtime_get_ctx(track);
        if ((active_ctx == 0)
            || (active_ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (active_ctx->engine != ctx->engine)
            || (active_ctx->type != ctx->type)
            || (active_ctx->mix_track_id >= SEQ_TRACK_COUNT)
            || ((used_mix_tracks & (uint16_t)(1U << active_ctx->mix_track_id))
                != 0U))
        {
            return 0U;
        }
        used_mix_tracks |= (uint16_t)(1U << active_ctx->mix_track_id);
    }
    audio_track_diag_open(0U, ctx->mix_track_id,
                          (g_runner.current.engine == TEST_ENGINE_STACK) ? 1U : 0U);
    audio_track_diag_reset_all();
    audio_global_diag_reset();
    return 1U;
}

static void notes_on(void)
{
    if (g_runner.current.track_count > 1U)
    {
        for (uint8_t track = 0U; track < g_runner.current.track_count; ++track)
        {
            keyboard_engine_note_on_for_track(track, g_runner.current.notes[track], 127U);
        }
        return;
    }
    for (uint8_t i = 0U; i < g_runner.current.note_count; ++i)
    {
        keyboard_engine_note_on_for_track(0U, g_runner.current.notes[i], 127U);
    }
}

static void notes_off(void)
{
    for (uint8_t track = 0U; track < g_runner.current.track_count; ++track)
    {
        keyboard_engine_all_notes_off_for_track(track);
    }
}

static float global_stage_peak(const audio_global_diag_snapshot_t *snapshot,
                               audio_global_diag_stage_t stage)
{
    const float left = snapshot->peak_l[stage];
    const float right = snapshot->peak_r[stage];
    return (left > right) ? left : right;
}

static float global_stage_rms(const audio_global_diag_snapshot_t *snapshot,
                              audio_global_diag_stage_t stage)
{
    if (snapshot->samples[stage] == 0U)
    {
        return 0.0f;
    }
    const float mean_energy =
        (snapshot->energy_l[stage] + snapshot->energy_r[stage])
        / (2.0f * (float)snapshot->samples[stage]);
    return (mean_energy > 0.0f) ? sqrtf(mean_energy) : 0.0f;
}

static float track_stage_rms(const audio_track_diag_snapshot_t *snapshot,
                             audio_track_diag_stage_t stage)
{
    if ((snapshot == 0) || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    return sqrtf(snapshot->rms_energy[stage]
                 / (float)snapshot->samples[stage]);
}

static float track_stage_weighted(
    const audio_track_diag_snapshot_t *snapshot,
    audio_track_diag_stage_t stage)
{
    if ((snapshot == 0) || (snapshot->samples[stage] == 0U))
    {
        return 0.0f;
    }
    return sqrtf(snapshot->k_weighted_energy[stage]
                 / (float)snapshot->samples[stage]);
}

static const char *sound_type_name(const audio_test_case_t *test)
{
    if ((test->engine == TEST_ENGINE_PRISM)
        && (prism_is_random(test->model) != 0U)
        && (prism_is_percussive(test->model) != 0U))
    {
        return "PERCUSSIVE_RANDOM";
    }
    switch (test->sound_type)
    {
        case TEST_SOUND_PERCUSSIVE: return "PERCUSSIVE";
        case TEST_SOUND_NOISY_RANDOM: return "NOISY_RANDOM";
        default: return "CONTINUOUS";
    }
}

static const char *test_engine_name(test_engine_t engine)
{
    switch (engine)
    {
        case TEST_ENGINE_PRISM: return "PRISM";
        case TEST_ENGINE_STACK: return "STACK";
        case TEST_ENGINE_DELUGE: return "DELUGE";
        case TEST_ENGINE_WAVE: return "WAVE";
        case TEST_ENGINE_SAMPLER: return "SAMPLER";
        case TEST_ENGINE_DRUM_MD: return "DRUM";
        case TEST_ENGINE_DRUM_ANALOG: return "DRUM";
        default: return "UNKNOWN";
    }
}

static void model_identity(uint8_t key, test_engine_t *engine,
                           uint8_t *model, const char **sound_type)
{
    if (key < 39U)
    {
        *engine = TEST_ENGINE_PRISM;
        *model = key;
        *sound_type = (prism_is_percussive(key) != 0U)
            ? ((prism_is_random(key) != 0U)
                ? "PERCUSSIVE_RANDOM" : "PERCUSSIVE")
            : ((prism_is_random(key) != 0U)
                ? "NOISY_RANDOM" : "CONTINUOUS");
    }
    else if (key < 52U)
    {
        *engine = TEST_ENGINE_STACK;
        *model = (uint8_t)(key - 39U);
        *sound_type = (*model == (uint8_t)BRICK6_STACK_MODEL_SWARM)
            ? "NOISY_RANDOM" : "CONTINUOUS";
    }
    else if (key < 58U)
    {
        *engine = TEST_ENGINE_DELUGE;
        *model = (uint8_t)(key - 52U);
        *sound_type = "CONTINUOUS";
    }
    else if (key == 58U)
    {
        *engine = TEST_ENGINE_WAVE;
        *model = 0U;
        *sound_type = "CONTINUOUS";
    }
    else if (key == 59U)
    {
        *engine = TEST_ENGINE_SAMPLER;
        *model = 0U;
        *sound_type = "PERCUSSIVE";
    }
    else
    {
        *engine = (key == 60U) ? TEST_ENGINE_DRUM_MD
                               : TEST_ENGINE_DRUM_ANALOG;
        *model = (uint8_t)(key - 60U);
        *sound_type = "PERCUSSIVE";
    }
}

static uint32_t calibration_clip_count(
    const audio_track_diag_snapshot_t *track,
    const audio_global_diag_snapshot_t *global)
{
    return track->soft_clip_count + track->filter_clip_count
        + track->insert_clip_count + global->final_clip_count
        + global->master_fx_clamp_count + global->delay_clamp_count;
}

static void collect_calibration_observation(void)
{
    if ((g_runner.current.calibration == 0U)
        || (g_runner.current.model_key >= AUDIO_TEST_CAL_MODEL_COUNT))
    {
        return;
    }
    audio_test_cal_model_stats_t *const stats =
        &g_cal_stats[g_runner.current.model_key];
    if (stats->count >= AUDIO_TEST_CAL_MAX_OBSERVATIONS)
    {
        return;
    }
    const float rms = track_stage_rms(
        &g_runner.track_capture, AUDIO_TRACK_DIAG_ENG);
    const float weighted = track_stage_weighted(
        &g_runner.track_capture, AUDIO_TRACK_DIAG_ENG);
    const float peak =
        g_runner.track_capture.peak[AUDIO_TRACK_DIAG_ENG];
    const float crest = (rms > 0.0000001f) ? (peak / rms) : 0.0f;
    const uint16_t slot = stats->count++;
    stats->weighted[slot] = weighted;
    stats->rms[slot] = rms;
    stats->peak[slot] = peak;
    stats->crest[slot] = crest;
    const uint32_t samples =
        g_runner.track_capture.samples[AUDIO_TRACK_DIAG_ENG];
    const float dc = (samples != 0U)
        ? (g_runner.track_capture.signed_sum[AUDIO_TRACK_DIAG_ENG]
           / (float)samples) : 0.0f;
    const float abs_dc = fabsf(dc);
    if (abs_dc > stats->worst_dc)
    {
        stats->worst_dc = abs_dc;
    }
    if (peak > stats->max_peak)
    {
        stats->max_peak = peak;
    }
    stats->total_clips += calibration_clip_count(
        &g_runner.track_capture, &g_runner.global_capture);
    if ((slot == 0U) || (weighted < stats->weakest_weighted))
    {
        stats->weakest_weighted = weighted;
        (void)snprintf(stats->weakest_scenario,
                       sizeof(stats->weakest_scenario), "%s",
                       g_runner.current.name);
    }
    if ((slot == 0U) || (weighted > stats->strongest_weighted))
    {
        stats->strongest_weighted = weighted;
        (void)snprintf(stats->strongest_scenario,
                       sizeof(stats->strongest_scenario), "%s",
                       g_runner.current.name);
    }
}

static float sorted_percentile(const float *values, uint16_t count,
                               uint8_t percentile)
{
    if (count == 0U)
    {
        return 0.0f;
    }
    memcpy(g_cal_sort_scratch, values, (size_t)count * sizeof(float));
    for (uint16_t i = 1U; i < count; ++i)
    {
        const float value = g_cal_sort_scratch[i];
        uint16_t j = i;
        while ((j != 0U) && (g_cal_sort_scratch[j - 1U] > value))
        {
            g_cal_sort_scratch[j] = g_cal_sort_scratch[j - 1U];
            --j;
        }
        g_cal_sort_scratch[j] = value;
    }
    const uint32_t index =
        ((uint32_t)(count - 1U) * (uint32_t)percentile + 50U) / 100U;
    return g_cal_sort_scratch[index];
}

static float linear_db(float value)
{
    return (value > 0.000001f) ? (20.0f * log10f(value)) : -120.0f;
}

static void build_calibration_summary(uint8_t key,
                                      audio_test_csv_summary_t *summary,
                                      char *model_name,
                                      uint32_t model_name_size)
{
    memset(summary, 0, sizeof(*summary));
    test_engine_t engine;
    uint8_t model;
    const char *sound_type;
    model_identity(key, &engine, &model, &sound_type);
    const audio_test_cal_model_stats_t *const stats = &g_cal_stats[key];
    const float weighted_median =
        sorted_percentile(stats->weighted, stats->count, 50U);
    const float weighted_p10 =
        sorted_percentile(stats->weighted, stats->count, 10U);
    const float weighted_p90 =
        sorted_percentile(stats->weighted, stats->count, 90U);
    const float rms_median =
        sorted_percentile(stats->rms, stats->count, 50U);
    const float peak_p95 =
        sorted_percentile(stats->peak, stats->count, 95U);
    const float crest_median =
        sorted_percentile(stats->crest, stats->count, 50U);
    float gain_db = AUDIO_TEST_CAL_TARGET_DBFS - linear_db(weighted_median);
    const float max_peak_db = linear_db(stats->max_peak);
    const float safe_gain_min = -max_peak_db - 6.0f;
    const float safe_gain_max = -max_peak_db - 3.0f;
    const uint8_t insufficient =
        (stats->count < 3U) || (weighted_median < 0.001f)
        || (safe_gain_min > 12.0f);
    if (insufficient != 0U)
    {
        gain_db = 0.0f;
    }
    else
    {
        if (gain_db < safe_gain_min) gain_db = safe_gain_min;
        if (gain_db > safe_gain_max) gain_db = safe_gain_max;
        if (gain_db > 12.0f) gain_db = 12.0f;
        if (gain_db < -12.0f) gain_db = -12.0f;
    }
    const float variability_db =
        linear_db(weighted_p90) - linear_db(weighted_p10);
    const char *status =
        (stats->total_clips != 0U) ? "CLIPPING"
      : (insufficient != 0U) ? "INSUFFICIENT_SIGNAL"
      : (variability_db > 12.0f) ? "TOO_VARIABLE" : "VALID";
    (void)snprintf(model_name, model_name_size, "%s_%02u",
                   test_engine_name(engine), (unsigned)model);
    summary->run_id = g_runner.run_id;
    summary->test_total = AUDIO_TEST_RUNNER_TEST_TOTAL;
    summary->engine = test_engine_name(engine);
    summary->model_name = model_name;
    summary->sound_type = sound_type;
    summary->model_id = model;
    summary->observation_count = stats->count;
    summary->weighted_median = weighted_median;
    summary->rms_median = rms_median;
    summary->peak_high = peak_p95;
    summary->crest_representative = crest_median;
    summary->worst_dc = stats->worst_dc;
    summary->total_clips = stats->total_clips;
    summary->weakest_scenario = stats->weakest_scenario;
    summary->strongest_scenario = stats->strongest_scenario;
    summary->recommended_gain_db = gain_db;
    summary->remaining_headroom_db =
        -max_peak_db - gain_db;
    summary->status = status;
}

static void evaluate_coherent_sum_progression(void)
{
    g_runner.sum_progression_fail = 0U;
    g_runner.sum_expected_ratio = 0.0f;
    g_runner.sum_peak_ratio = 0.0f;
    g_runner.sum_rms_ratio = 0.0f;
    if (g_runner.current.coherent_sum == 0U)
    {
        return;
    }

    const float peak = global_stage_peak(
        &g_runner.global_capture, AUDIO_GLOBAL_DIAG_DRY_SUM);
    const float rms = global_stage_rms(
        &g_runner.global_capture, AUDIO_GLOBAL_DIAG_DRY_SUM);
    if ((g_runner.coherent_sum_count == 0U)
        || (g_runner.coherent_sum_peak <= 0.0f)
        || (g_runner.coherent_sum_rms <= 0.0f))
    {
        g_runner.sum_progression_fail =
            (!isfinite(peak) || !isfinite(rms)
             || (peak <= 0.000001f) || (rms <= 0.000001f)) ? 1U : 0U;
    }
    else
    {
        g_runner.sum_expected_ratio =
            (float)g_runner.current.track_count
            / (float)g_runner.coherent_sum_count;
        g_runner.sum_peak_ratio = peak / g_runner.coherent_sum_peak;
        g_runner.sum_rms_ratio = rms / g_runner.coherent_sum_rms;
        const float minimum_ratio = g_runner.sum_expected_ratio * 0.80f;
        if (!isfinite(g_runner.sum_peak_ratio)
            || !isfinite(g_runner.sum_rms_ratio)
            || (g_runner.sum_peak_ratio < minimum_ratio)
            || (g_runner.sum_rms_ratio < minimum_ratio))
        {
            g_runner.sum_progression_fail = 1U;
        }
    }
    g_runner.coherent_sum_count = g_runner.current.track_count;
    g_runner.coherent_sum_peak = peak;
    g_runner.coherent_sum_rms = rms;
}

static float wet_peak(const audio_global_diag_snapshot_t *snapshot)
{
    const float delay = global_stage_peak(
        snapshot, AUDIO_GLOBAL_DIAG_DELAY_RETURN);
    const float reverb = global_stage_peak(
        snapshot, AUDIO_GLOBAL_DIAG_REVERB_RETURN);
    return (delay > reverb) ? delay : reverb;
}

static void merge_track_snapshots(
    const audio_track_diag_snapshot_t *first,
    const audio_track_diag_snapshot_t *second,
    audio_track_diag_snapshot_t *out)
{
    *out = *first;
    for (uint8_t stage = 0U; stage < AUDIO_TRACK_DIAG_STAGE_COUNT; ++stage)
    {
        if (second->peak[stage] > out->peak[stage])
        {
            out->peak[stage] = second->peak[stage];
        }
        out->rms_energy[stage] += second->rms_energy[stage];
        out->k_weighted_energy[stage] += second->k_weighted_energy[stage];
        out->signed_sum[stage] += second->signed_sum[stage];
        out->samples[stage] += second->samples[stage];
    }
    out->soft_clip_count += second->soft_clip_count;
    out->filter_clip_count += second->filter_clip_count;
    out->insert_clip_count += second->insert_clip_count;
}

static void merge_global_snapshots(
    const audio_global_diag_snapshot_t *first,
    const audio_global_diag_snapshot_t *second,
    audio_global_diag_snapshot_t *out)
{
    *out = *first;
    for (uint8_t stage = 0U; stage < AUDIO_GLOBAL_DIAG_STAGE_COUNT; ++stage)
    {
        if (second->peak_l[stage] > out->peak_l[stage])
        {
            out->peak_l[stage] = second->peak_l[stage];
        }
        if (second->peak_r[stage] > out->peak_r[stage])
        {
            out->peak_r[stage] = second->peak_r[stage];
        }
        out->energy_l[stage] += second->energy_l[stage];
        out->energy_r[stage] += second->energy_r[stage];
        out->samples[stage] += second->samples[stage];
        out->nonfinite_count[stage] += second->nonfinite_count[stage];
        out->over_full_scale_count[stage] +=
            second->over_full_scale_count[stage];
        if (second->state[stage] > out->state[stage])
        {
            out->state[stage] = second->state[stage];
        }
    }
    out->final_clip_count += second->final_clip_count;
    if (second->final_clip_max_over > out->final_clip_max_over)
    {
        out->final_clip_max_over = second->final_clip_max_over;
    }
    out->master_fx_clamp_count += second->master_fx_clamp_count;
    if (second->master_fx_clamp_max_over > out->master_fx_clamp_max_over)
    {
        out->master_fx_clamp_max_over = second->master_fx_clamp_max_over;
    }
    out->delay_clamp_count += second->delay_clamp_count;
    if (second->delay_clamp_max_over > out->delay_clamp_max_over)
    {
        out->delay_clamp_max_over = second->delay_clamp_max_over;
    }
}

static uint32_t global_nonfinite_count(
    const audio_global_diag_snapshot_t *snapshot)
{
    uint32_t count = 0U;
    for (uint8_t stage = 0U; stage < AUDIO_GLOBAL_DIAG_STAGE_COUNT; ++stage)
    {
        count += snapshot->nonfinite_count[stage];
    }
    return count;
}

static uint32_t return_over_full_scale_count(
    const audio_global_diag_snapshot_t *snapshot)
{
    return snapshot->over_full_scale_count[AUDIO_GLOBAL_DIAG_DELAY_RETURN]
        + snapshot->over_full_scale_count[AUDIO_GLOBAL_DIAG_REVERB_RETURN];
}

static uint8_t restore_saved_state(void)
{
    all_test_notes_off();
    audio_track_diag_close();
    if (g_runner.calibration_sample_slot != SAMPLER_RAM_POOL_INVALID_SLOT)
    {
        sampler_ram_pool_clear(g_runner.calibration_sample_slot);
        g_runner.calibration_sample_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        g_runner.calibration_sample_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if (g_runner.calibration_wavetable_slot != WAVETABLE_POOL_INVALID_SLOT)
    {
        wavetable_pool_clear(g_runner.calibration_wavetable_slot);
        g_runner.calibration_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        g_runner.calibration_wavetable_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if (g_runner.snapshot_valid == 0U)
    {
        return (g_runner.cancel_requested != 0U) ? 1U : 0U;
    }
    if (pattern_live_apply_snapshot(&g_saved_project_state, 0U) == 0U)
    {
        return 0U;
    }
    ui_restore_active_track(g_runner.saved_active_track);
    ui_set_hall_mode(g_runner.saved_hall_mode);
    if (g_runner.restore_transport != 0U)
    {
        seq_runtime_start();
    }
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_set_playhead_step(
            track, g_runner.saved_playhead[track]);
    }
    g_runner.snapshot_valid = 0U;
    return 1U;
}

void audio_test_runner_init(void)
{
    memset(&g_runner, 0, sizeof(g_runner));
    g_runner.calibration_sample_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    g_runner.calibration_sample_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_runner.calibration_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    g_runner.calibration_wavetable_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_runner.state = AUDIO_TEST_RUNNER_IDLE;
}

uint8_t audio_test_runner_start(void)
{
    if (audio_test_runner_is_active() != 0U)
    {
        return 0U;
    }
    memset(&g_runner, 0, sizeof(g_runner));
    memset(g_cal_stats, 0, sizeof(g_cal_stats));
    g_runner.calibration_sample_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    g_runner.calibration_sample_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_runner.calibration_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    g_runner.calibration_wavetable_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_runner.state = AUDIO_TEST_RUNNER_PREPARE;
    g_runner.state_since_ms = HAL_GetTick();
    g_runner.run_id = HAL_GetTick();
    g_runner.strongest_index = 0U;
    g_runner.saved_active_track = ui_get_active_track();
    g_runner.saved_hall_mode = ui_get_hall_mode();
    g_runner.restore_transport = seq_runtime_is_running();
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        (void)seq_runtime_get_playhead_step(
            track, &g_runner.saved_playhead[track]);
    }
    return 1U;
}

void audio_test_runner_cancel(void)
{
    if (audio_test_runner_is_active() == 0U)
    {
        return;
    }
    g_runner.cancel_requested = 1U;
    all_test_notes_off();
    set_state(AUDIO_TEST_RUNNER_RESTORE);
}

uint8_t audio_test_runner_is_active(void)
{
    return ((g_runner.state >= AUDIO_TEST_RUNNER_PREPARE)
        && (g_runner.state <= AUDIO_TEST_RUNNER_RESTORE)) ? 1U : 0U;
}

void audio_test_runner_tick(void)
{
    const audio_test_csv_result_t csv_result = audio_test_csv_take_result();
    if (csv_result == AUDIO_TEST_CSV_RESULT_SESSION_OK)
    {
        g_runner.csv_ready = 1U;
    }
    if (csv_result == AUDIO_TEST_CSV_RESULT_ERROR)
    {
        all_test_notes_off();
        set_state(AUDIO_TEST_RUNNER_RESTORE);
    }

    switch (g_runner.state)
    {
        case AUDIO_TEST_RUNNER_PREPARE:
            if (pattern_live_capture_current(&g_saved_project_state) == 0U)
            {
                set_state(AUDIO_TEST_RUNNER_ERROR);
                break;
            }
            g_runner.snapshot_valid = 1U;
            seq_runtime_stop();
            all_test_notes_off();
            if ((sampler_ram_pool_create_audio_test_calibration(
                    &g_runner.calibration_sample_slot,
                    &g_runner.calibration_sample_global)
                 != SAMPLER_RAM_RESULT_OK)
                || (wavetable_pool_create_audio_test_calibration(
                    &g_runner.calibration_wavetable_slot,
                    &g_runner.calibration_wavetable_global)
                 != WAVETABLE_RESULT_OK))
            {
                set_state(AUDIO_TEST_RUNNER_RESTORE);
                break;
            }
            audio_test_csv_begin_session();
            set_state(AUDIO_TEST_RUNNER_VOLUME_WARNING);
            break;

        case AUDIO_TEST_RUNNER_VOLUME_WARNING:
            if (csv_result == AUDIO_TEST_CSV_RESULT_ERROR)
            {
                set_state(AUDIO_TEST_RUNNER_RESTORE);
            }
            else if ((g_runner.csv_ready != 0U)
                     && (elapsed(AUDIO_TEST_WARNING_MS) != 0U))
            {
                set_state(AUDIO_TEST_RUNNER_CONFIGURE);
            }
            break;

        case AUDIO_TEST_RUNNER_CONFIGURE:
            build_case(g_runner.index, &g_runner.current);
            if (configure_current() == 0U)
            {
                set_state(AUDIO_TEST_RUNNER_RESTORE);
            }
            else
            {
                set_state(AUDIO_TEST_RUNNER_NOTE_ON);
            }
            break;

        case AUDIO_TEST_RUNNER_NOTE_ON:
            if (g_runner.current.calibration != 0U)
            {
                audio_track_diag_reset_all();
                audio_global_diag_reset();
                cpu_load_reset_measurement();
            }
            notes_on();
            set_state((g_runner.current.calibration != 0U)
                ? AUDIO_TEST_RUNNER_ATTACK_MEASURE
                : AUDIO_TEST_RUNNER_WARMUP);
            break;

        case AUDIO_TEST_RUNNER_ATTACK_MEASURE:
            if (elapsed(AUDIO_TEST_ATTACK_MS) != 0U)
            {
                set_state(AUDIO_TEST_RUNNER_ATTACK_CAPTURE);
            }
            break;

        case AUDIO_TEST_RUNNER_ATTACK_CAPTURE:
            memset(&g_runner.attack_track_capture, 0,
                   sizeof(g_runner.attack_track_capture));
            memset(&g_runner.attack_global_capture, 0,
                   sizeof(g_runner.attack_global_capture));
            if (audio_track_diag_read_coherent(
                    0U, &g_runner.attack_track_capture,
                    &g_runner.attack_global_capture) == 0U)
            {
                set_state(AUDIO_TEST_RUNNER_RESTORE);
                break;
            }
            cpu_load_get_metrics(&g_runner.attack_cpu);
            if (g_runner.current.sound_type == TEST_SOUND_PERCUSSIVE)
            {
                g_runner.percussive_last_poll_ms = HAL_GetTick();
                g_runner.percussive_last_energy =
                    g_runner.attack_track_capture.rms_energy[
                        AUDIO_TRACK_DIAG_ENG];
                g_runner.percussive_last_samples =
                    g_runner.attack_track_capture.samples[
                        AUDIO_TRACK_DIAG_ENG];
                g_runner.percussive_silent_polls = 0U;
                set_state(AUDIO_TEST_RUNNER_PERCUSSIVE_DECAY);
            }
            else
            {
                set_state(AUDIO_TEST_RUNNER_WARMUP);
            }
            break;

        case AUDIO_TEST_RUNNER_PERCUSSIVE_DECAY:
        {
            const uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - g_runner.percussive_last_poll_ms)
                >= AUDIO_TEST_PERCUSSIVE_POLL_MS)
            {
                audio_track_diag_snapshot_t poll_track;
                audio_global_diag_snapshot_t poll_global;
                if (audio_track_diag_read_coherent(
                        0U, &poll_track, &poll_global) == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                    break;
                }
                const float energy =
                    poll_track.rms_energy[AUDIO_TRACK_DIAG_ENG];
                const uint32_t samples =
                    poll_track.samples[AUDIO_TRACK_DIAG_ENG];
                const float delta_energy =
                    energy - g_runner.percussive_last_energy;
                const uint32_t delta_samples =
                    samples - g_runner.percussive_last_samples;
                const float delta_rms = (delta_samples != 0U)
                    ? sqrtf(fmaxf(delta_energy, 0.0f)
                            / (float)delta_samples) : 0.0f;
                if ((elapsed(AUDIO_TEST_PERCUSSIVE_MIN_MS) != 0U)
                    && (delta_rms < AUDIO_TEST_SILENCE_RMS))
                {
                    ++g_runner.percussive_silent_polls;
                }
                else
                {
                    g_runner.percussive_silent_polls = 0U;
                }
                g_runner.percussive_last_poll_ms = now;
                g_runner.percussive_last_energy = energy;
                g_runner.percussive_last_samples = samples;
                if ((g_runner.percussive_silent_polls
                     >= AUDIO_TEST_PERCUSSIVE_SILENCE_POLLS)
                    || (elapsed(AUDIO_TEST_PERCUSSIVE_MAX_MS) != 0U))
                {
                    g_runner.track_capture = poll_track;
                    g_runner.global_capture = poll_global;
                    cpu_load_get_metrics(&g_runner.active_cpu);
                    collect_calibration_observation();
                    notes_off();
                    g_runner.write_phase = 0U;
                    set_state(AUDIO_TEST_RUNNER_WRITE);
                }
            }
            break;
        }

        case AUDIO_TEST_RUNNER_WARMUP:
            if (elapsed((g_runner.current.calibration != 0U)
                        ? AUDIO_TEST_STABILIZE_AFTER_ATTACK_MS
                        : (g_runner.current.fx_tail_test != 0U)
                        ? AUDIO_TEST_FX_WARMUP_MS
                        : AUDIO_TEST_WARMUP_MS) != 0U)
            {
                audio_track_diag_reset_all();
                audio_global_diag_reset();
                cpu_load_reset_measurement();
                set_state(AUDIO_TEST_RUNNER_MEASURE);
            }
            break;

        case AUDIO_TEST_RUNNER_MEASURE:
            if (elapsed(active_measure_ms()) != 0U)
            {
                set_state(AUDIO_TEST_RUNNER_CAPTURE);
            }
            break;

        case AUDIO_TEST_RUNNER_CAPTURE:
            memset(&g_runner.track_capture, 0, sizeof(g_runner.track_capture));
            memset(&g_runner.global_capture, 0, sizeof(g_runner.global_capture));
            if (audio_track_diag_read_coherent(0U, &g_runner.track_capture,
                                               &g_runner.global_capture) == 0U)
            {
                set_state(AUDIO_TEST_RUNNER_RESTORE);
                break;
            }
            cpu_load_get_metrics(&g_runner.active_cpu);
            evaluate_coherent_sum_progression();
            collect_calibration_observation();
            set_state(AUDIO_TEST_RUNNER_NOTE_OFF);
            break;

        case AUDIO_TEST_RUNNER_NOTE_OFF:
            notes_off();
            if (g_runner.current.fx_tail_test != 0U)
            {
                audio_track_diag_reset_all();
                audio_global_diag_reset();
                cpu_load_reset_measurement();
                set_state(AUDIO_TEST_RUNNER_FX_TAIL_EARLY);
            }
            else
            {
                g_runner.write_phase = 0U;
                set_state(AUDIO_TEST_RUNNER_WRITE);
            }
            break;

        case AUDIO_TEST_RUNNER_FX_TAIL_EARLY:
            if (elapsed(AUDIO_TEST_FX_TAIL_EARLY_MS) != 0U)
            {
                if (audio_track_diag_read_coherent(
                        0U, &g_runner.tail_early_track,
                        &g_runner.tail_early_global) == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                    break;
                }
                audio_track_diag_reset_all();
                audio_global_diag_reset();
                set_state(AUDIO_TEST_RUNNER_FX_TAIL_LATE);
            }
            break;

        case AUDIO_TEST_RUNNER_FX_TAIL_LATE:
            if (elapsed(AUDIO_TEST_FX_TAIL_LATE_MS) != 0U)
            {
                if (audio_track_diag_read_coherent(
                        0U, &g_runner.tail_late_track,
                        &g_runner.tail_late_global) == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                    break;
                }
                merge_track_snapshots(
                    &g_runner.tail_early_track,
                    &g_runner.tail_late_track,
                    &g_runner.tail_track_capture);
                merge_global_snapshots(
                    &g_runner.tail_early_global,
                    &g_runner.tail_late_global,
                    &g_runner.tail_global_capture);
                cpu_load_get_metrics(&g_runner.tail_cpu);
                g_runner.write_phase = 0U;
                set_state(AUDIO_TEST_RUNNER_WRITE);
            }
            break;

        case AUDIO_TEST_RUNNER_WRITE:
            if (audio_test_csv_is_busy() == 0U)
            {
                char notes[48];
                uint32_t offset = 0U;
                notes[0] = '\0';
                for (uint8_t i = 0U; i < g_runner.current.note_count; ++i)
                {
                    const int count = snprintf(&notes[offset], sizeof(notes) - offset,
                                               "%s%u", (i == 0U) ? "" : "/",
                                               (unsigned)g_runner.current.notes[i]);
                    if ((count < 0) || ((uint32_t)count >= (sizeof(notes) - offset)))
                    {
                        break;
                    }
                    offset += (uint32_t)count;
                }
                const uint8_t tail_row =
                    ((g_runner.current.fx_tail_test != 0U)
                     && (g_runner.write_phase != 0U)) ? 1U : 0U;
                const uint8_t attack_row =
                    ((g_runner.current.calibration != 0U)
                     && (g_runner.write_phase == 0U)) ? 1U : 0U;
                const audio_track_diag_snapshot_t *const track_snapshot =
                    (attack_row != 0U) ? &g_runner.attack_track_capture
                  : (tail_row != 0U) ? &g_runner.tail_track_capture
                                     : &g_runner.track_capture;
                const audio_global_diag_snapshot_t *const global_snapshot =
                    (attack_row != 0U) ? &g_runner.attack_global_capture
                  : (tail_row != 0U) ? &g_runner.tail_global_capture
                                     : &g_runner.global_capture;
                const cpu_load_metrics_t *const cpu =
                    (attack_row != 0U) ? &g_runner.attack_cpu
                  : (tail_row != 0U) ? &g_runner.tail_cpu
                                     : &g_runner.active_cpu;
                const float early_wet =
                    (tail_row != 0U) ? wet_peak(&g_runner.tail_early_global)
                                     : 0.0f;
                const float late_wet =
                    (tail_row != 0U) ? wet_peak(&g_runner.tail_late_global)
                                     : 0.0f;
                const float active_wet = wet_peak(&g_runner.global_capture);
                const uint8_t tail_cut =
                    (tail_row != 0U)
                    && (((active_wet > 0.0001f)
                         && (early_wet < (active_wet * 0.0001f)))
                        || ((early_wet > 0.0001f)
                            && (late_wet < (early_wet * 0.0001f))));
                const uint8_t tail_rising =
                    (tail_row != 0U) && (late_wet > 0.00001f)
                    && (late_wet > (early_wet * 1.05f));
                const uint32_t return_over =
                    return_over_full_scale_count(global_snapshot);
                const uint32_t nonfinite =
                    global_nonfinite_count(global_snapshot);
                const uint8_t final_saturation =
                    (global_snapshot->final_clip_count != 0U)
                    || (global_snapshot->master_fx_clamp_count != 0U)
                    || (global_snapshot->delay_clamp_count != 0U)
                    || (return_over != 0U);
                const uint8_t irq_overload =
                    (cpu->over_100_count != 0U)
                    || (cpu->peak_permille >= 1000U);
                const uint8_t headroom_exceeded =
                    global_stage_peak(global_snapshot,
                        AUDIO_GLOBAL_DIAG_POST_RETURNS) > 0.89125094f;
                const uint8_t fail = g_runner.sum_progression_fail
                    || tail_cut || final_saturation || irq_overload
                    || (nonfinite != 0U)
                    || ((g_runner.current.calibration != 0U)
                        && ((track_snapshot->samples[AUDIO_TRACK_DIAG_ENG] == 0U)
                            || (track_stage_rms(track_snapshot,
                                    AUDIO_TRACK_DIAG_ENG)
                                < AUDIO_TEST_SILENCE_RMS)))
                    || (track_snapshot->filter_clip_count != 0U)
                    || (track_snapshot->insert_clip_count != 0U);
                const uint8_t warning = tail_rising || headroom_exceeded
                    || (track_snapshot->soft_clip_count != 0U);
                const audio_test_csv_case_t csv_case = {
                    .run_id = g_runner.run_id,
                    .test_index = (uint16_t)(g_runner.index + 1U),
                    .test_total = AUDIO_TEST_RUNNER_TEST_TOTAL,
                    .test_phase = (g_runner.current.fx_tail_test == 0U)
                        ? ((g_runner.current.calibration != 0U)
                            ? ((attack_row != 0U) ? "CAL_ATTACK"
                               : (g_runner.current.sound_type
                                    == TEST_SOUND_PERCUSSIVE)
                                    ? "CAL_STRIKE" : "CAL_SUSTAIN")
                            : g_runner.current.phase)
                        : ((tail_row != 0U) ? "FX_TAIL" : "FX_ACTIVE"),
                    .test_name = g_runner.current.name,
                    .test_status = fail ? "FAIL" : (warning ? "WARN" : "PASS"),
                    .warmup_ms = (g_runner.current.fx_tail_test != 0U)
                        ? AUDIO_TEST_FX_WARMUP_MS
                        : ((g_runner.current.calibration != 0U)
                            ? ((attack_row != 0U) ? 0U
                               : (g_runner.current.sound_type
                                    == TEST_SOUND_PERCUSSIVE)
                                    ? 0U : AUDIO_TEST_WARMUP_MS)
                            : AUDIO_TEST_WARMUP_MS),
                    .measure_ms = (g_runner.current.calibration != 0U)
                        ? ((attack_row != 0U) ? AUDIO_TEST_ATTACK_MS
                           : (g_runner.current.sound_type
                                == TEST_SOUND_PERCUSSIVE)
                                ? (g_runner.track_capture.samples[
                                        AUDIO_TRACK_DIAG_ENG] / 48U)
                                : AUDIO_TEST_ENGINE_MEASURE_MS)
                        : (g_runner.current.fx_tail_test == 0U)
                            ? active_measure_ms()
                        : ((tail_row != 0U) ? AUDIO_TEST_FX_TAIL_TOTAL_MS
                                           : AUDIO_TEST_FX_ACTIVE_MS),
                    .track_count = g_runner.current.track_count,
                    .voice_count = g_runner.current.voice_count,
                    .notes = notes,
                    .source_config = g_runner.current.sources,
                    .filter_config = g_runner.current.filter,
                    .fx_config = g_runner.current.fx,
                    .master_config = g_runner.current.master,
                    .cpu_metrics = *cpu,
                    .delay_send = g_runner.current.delay_send,
                    .reverb_send = g_runner.current.reverb_send,
                    .delay_mix = g_runner.current.delay_mix,
                    .delay_feedback = g_runner.current.delay_feedback,
                    .delay_time = g_runner.current.delay_time,
                    .reverb_mix = g_runner.current.reverb_mix,
                    .reverb_size = g_runner.current.reverb_size,
                    .reverb_decay = g_runner.current.reverb_decay,
                    .reverb_damping = g_runner.current.reverb_damping,
                    .tail_early_wet_peak = early_wet,
                    .tail_late_wet_peak = late_wet,
                    .return_over_full_scale_count = return_over,
                    .nonfinite_count = nonfinite,
                    .tail_cut_detected = tail_cut,
                    .tail_rising_detected = tail_rising,
                    .final_saturation_detected = final_saturation,
                    .irq_overload_detected = irq_overload,
                    .headroom_exceeded = headroom_exceeded,
                    .sum_expected_ratio = g_runner.sum_expected_ratio,
                    .sum_peak_ratio = g_runner.sum_peak_ratio,
                    .sum_rms_ratio = g_runner.sum_rms_ratio,
                    .sum_progression_fail = g_runner.sum_progression_fail,
                    .row_type = (g_runner.current.calibration != 0U)
                        ? "CAL_RAW" : "TEST",
                    .sound_type = (g_runner.current.calibration != 0U)
                        ? sound_type_name(&g_runner.current) : "",
                    .measurement_phase =
                        (g_runner.current.calibration == 0U) ? ""
                        : (attack_row != 0U) ? "ATTACK"
                        : (g_runner.current.sound_type
                            == TEST_SOUND_PERCUSSIVE) ? "STRIKE" : "SUSTAIN",
                    .note = g_runner.current.notes[0],
                    .velocity = g_runner.current.velocity,
                    .model_id = g_runner.current.model,
                    .timbre = g_runner.current.timbre,
                    .color = g_runner.current.color,
                    .oscillator_count = g_runner.current.source_count,
                    .oscillator_mode = g_runner.current.sources,
                    .repetition = g_runner.current.repetition
                };
                if (audio_test_csv_enqueue_auto(0U, &csv_case,
                                                track_snapshot,
                                                global_snapshot) == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                }
            }
            if (audio_test_csv_is_busy() != 0U)
            {
                set_state(AUDIO_TEST_RUNNER_NEXT);
            }
            break;

        case AUDIO_TEST_RUNNER_NEXT:
            if (csv_result == AUDIO_TEST_CSV_RESULT_ROW_OK)
            {
                if ((g_runner.current.calibration != 0U)
                    && (g_runner.write_phase == 0U))
                {
                    g_runner.write_phase = 1U;
                    set_state(AUDIO_TEST_RUNNER_WRITE);
                    break;
                }
                if ((g_runner.current.fx_tail_test != 0U)
                    && (g_runner.write_phase == 0U))
                {
                    g_runner.write_phase = 1U;
                    set_state(AUDIO_TEST_RUNNER_WRITE);
                    break;
                }
                ++g_runner.index;
                if (g_runner.index >= AUDIO_TEST_RUNNER_TEST_TOTAL)
                {
                    g_runner.summary_index = 0U;
                    set_state(AUDIO_TEST_RUNNER_SUMMARY_WRITE);
                }
                else
                {
                    set_state(AUDIO_TEST_RUNNER_CONFIGURE);
                }
            }
            break;

        case AUDIO_TEST_RUNNER_SUMMARY_WRITE:
            if (audio_test_csv_is_busy() == 0U)
            {
                audio_test_csv_summary_t summary;
                char model_name[32];
                build_calibration_summary(g_runner.summary_index, &summary,
                                          model_name, sizeof(model_name));
                if (audio_test_csv_enqueue_summary(&summary) == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                }
                else
                {
                    set_state(AUDIO_TEST_RUNNER_SUMMARY_NEXT);
                }
            }
            break;

        case AUDIO_TEST_RUNNER_SUMMARY_NEXT:
            if (csv_result == AUDIO_TEST_CSV_RESULT_ROW_OK)
            {
                ++g_runner.summary_index;
                if (g_runner.summary_index >= AUDIO_TEST_CAL_MODEL_COUNT)
                {
                    set_state(AUDIO_TEST_RUNNER_RESTORE);
                }
                else
                {
                    set_state(AUDIO_TEST_RUNNER_SUMMARY_WRITE);
                }
            }
            break;

        case AUDIO_TEST_RUNNER_RESTORE:
            if (audio_test_csv_is_busy() == 0U)
            {
                const uint8_t restored = restore_saved_state();
                if (restored == 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_ERROR);
                }
                else if (g_runner.cancel_requested != 0U)
                {
                    set_state(AUDIO_TEST_RUNNER_STOPPED);
                }
                else if (g_runner.index >= AUDIO_TEST_RUNNER_TEST_TOTAL)
                {
                    set_state(AUDIO_TEST_RUNNER_DONE);
                }
                else
                {
                    set_state(AUDIO_TEST_RUNNER_ERROR);
                }
            }
            break;

        default:
            break;
    }
}

void audio_test_runner_get_view(audio_test_runner_view_t *out_view)
{
    if (out_view == 0)
    {
        return;
    }
    memset(out_view, 0, sizeof(*out_view));
    out_view->state = g_runner.state;
    out_view->test_index = (g_runner.index < AUDIO_TEST_RUNNER_TEST_TOTAL)
        ? (uint16_t)(g_runner.index + 1U) : AUDIO_TEST_RUNNER_TEST_TOTAL;
    out_view->test_total = AUDIO_TEST_RUNNER_TEST_TOTAL;
    out_view->progress_12 = (uint8_t)(((uint32_t)g_runner.index * 12U)
                                     / AUDIO_TEST_RUNNER_TEST_TOTAL);
    (void)snprintf(out_view->phase, sizeof(out_view->phase), "%s",
                   g_runner.current.phase);
    (void)snprintf(out_view->test_name, sizeof(out_view->test_name), "%s",
                   g_runner.current.name);
    (void)snprintf(out_view->status, sizeof(out_view->status), "%s",
        (g_runner.state == AUDIO_TEST_RUNNER_VOLUME_WARNING) ? "TURN VOL DOWN"
      : (g_runner.state == AUDIO_TEST_RUNNER_DONE) ? "AUTO DONE"
      : (g_runner.state == AUDIO_TEST_RUNNER_STOPPED) ? "AUTO STOP"
      : (g_runner.state == AUDIO_TEST_RUNNER_ERROR) ? "AUTO ERR" : "STOP");
}
