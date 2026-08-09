/**
 * @file brick6_braids_runtime.cpp
 * @brief Minimal track-aware Prism runtime wrapper around the internal Braids engine.
 */

#include "Core/brick6_braids_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Storage/memory_layout.h"

#include "braids/macro_oscillator.h"
#include "braids/macro_oscillator_shape.h"

namespace {

constexpr uint32_t kBraidsRenderBlockSize = 24U;
constexpr float kBraidsPitchCoarseRange = 48.0f;
constexpr float kBraidsPitchFineRange = 2.0f;
constexpr float kBraidsPitchFmRange = 24.0f;
constexpr float kBraidsReleaseCoeff = 0.995f;
constexpr float kBraidsEditMax = (float)BRICK6_PRISM_LAST_MODEL;
constexpr float kBraidsSampleRate = 48000.0f;
constexpr float kBraidsTailMinSeconds = 0.001f;
constexpr float kBraidsTailMaxSeconds = 5.0f;
constexpr float kBraidsTailSafetyMultiplier = 6.0f;
constexpr float kBraidsTailSafetyFloorSeconds = 0.050f;
constexpr float BRAIDS_OUTPUT_TRIM = 0.30f;
constexpr uint8_t kBraidsOscCount = 2U;
constexpr float kBraidsOscActiveEpsilon = 1.0e-5f;

static const braids::MacroOscillatorShape kBraidsShapeMap[] = {
    braids::MACRO_OSC_SHAPE_CSAW,
    braids::MACRO_OSC_SHAPE_MORPH,
    braids::MACRO_OSC_SHAPE_SAW_SQUARE,
    braids::MACRO_OSC_SHAPE_SINE_TRIANGLE,
    braids::MACRO_OSC_SHAPE_BUZZ,
    braids::MACRO_OSC_SHAPE_SQUARE_SUB,
    braids::MACRO_OSC_SHAPE_SAW_SUB,
    braids::MACRO_OSC_SHAPE_SQUARE_SYNC,
    braids::MACRO_OSC_SHAPE_SAW_SYNC,
    braids::MACRO_OSC_SHAPE_TRIPLE_SAW,
    braids::MACRO_OSC_SHAPE_TRIPLE_SQUARE,
    braids::MACRO_OSC_SHAPE_TRIPLE_TRIANGLE,
    braids::MACRO_OSC_SHAPE_TRIPLE_SINE,
    braids::MACRO_OSC_SHAPE_TRIPLE_RING_MOD,
    braids::MACRO_OSC_SHAPE_SAW_SWARM,
    braids::MACRO_OSC_SHAPE_TOY,
    braids::MACRO_OSC_SHAPE_VOSIM,
    braids::MACRO_OSC_SHAPE_VOWEL,
    braids::MACRO_OSC_SHAPE_VOWEL_FOF,
    // Keep the historical slot stable for persisted Prism values.  The
    // removed model now falls back to CSAW and is no longer rendered.
    braids::MACRO_OSC_SHAPE_CSAW,
    braids::MACRO_OSC_SHAPE_FM,
    braids::MACRO_OSC_SHAPE_FEEDBACK_FM,
    braids::MACRO_OSC_SHAPE_CHAOTIC_FEEDBACK_FM,
    braids::MACRO_OSC_SHAPE_WAVETABLES,
    braids::MACRO_OSC_SHAPE_WAVE_MAP,
    braids::MACRO_OSC_SHAPE_WAVE_LINE,
    braids::MACRO_OSC_SHAPE_WAVE_PARAPHONIC,
    braids::MACRO_OSC_SHAPE_FILTERED_NOISE,
    braids::MACRO_OSC_SHAPE_TWIN_PEAKS_NOISE,
    braids::MACRO_OSC_SHAPE_CLOCKED_NOISE,
    braids::MACRO_OSC_SHAPE_GRANULAR_CLOUD,
    braids::MACRO_OSC_SHAPE_PARTICLE_NOISE,
    braids::MACRO_OSC_SHAPE_DIGITAL_MODULATION,
    braids::MACRO_OSC_SHAPE_QUESTION_MARK,
};

static_assert((sizeof(kBraidsShapeMap) / sizeof(kBraidsShapeMap[0])) == BRICK6_PRISM_MODEL_COUNT,
              "Prism catalogue and Braids shape map must stay compact and aligned");

typedef struct
{
    brick6_braids_runtime_voice_t voice;
    uint8_t phase_reset_enabled;
    uint8_t phase_reset_pending;
    float osc_level;
    float osc_level_current;
    float parameter_timbre_current;
    float parameter_color_current;
    float pitch_current_q7;
    braids::MacroOscillator oscillator;
} brick6_braids_runtime_osc_t;

typedef struct
{
    brick6_braids_runtime_osc_t osc[kBraidsOscCount];
    float note;
    float velocity;
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
    uint8_t has_note;
    float level;
    float vca_release_s;
    uint32_t tail_samples_remaining;
    uint32_t config_version;
    uint32_t synced_config_version;
} brick6_braids_runtime_instance_t;

AUDIO_HOT static brick6_braids_runtime_instance_t
    g_braids_runtime[BRICK6_BRAIDS_MAX_INSTANCES];
AUDIO_HOT static brick6_braids_runtime_instance_t
    g_braids_poly_d2[BRICK6_BRAIDS_VOICE_INSTANCE_COUNT - BRICK6_BRAIDS_MAX_INSTANCES];

static float brick6_braids_runtime_clamp(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static int16_t brick6_braids_runtime_float_to_u15(float value)
{
    const float clamped = brick6_braids_runtime_clamp(value, 0.0f, 1.0f);
    return (int16_t)(clamped * 32767.0f + 0.5f);
}

static int16_t brick6_braids_runtime_pitch_to_q7(const brick6_braids_runtime_voice_t *voice)
{
    const float coarse = (brick6_braids_runtime_clamp(voice->coarse, 0.0f, 1.0f) - 0.5f) * kBraidsPitchCoarseRange;
    const float fine = (brick6_braids_runtime_clamp(voice->fine, 0.0f, 1.0f) - 0.5f) * kBraidsPitchFineRange;
    const float fm_mod = brick6_braids_runtime_clamp(voice->fm, 0.0f, 1.0f)
        * brick6_braids_runtime_clamp(voice->modulation, 0.0f, 1.0f)
        * kBraidsPitchFmRange;
    const float note = brick6_braids_runtime_clamp(voice->note + coarse + fine + fm_mod, 0.0f, 127.0f);
    return (int16_t)(note * 128.0f + 0.5f);
}

static braids::MacroOscillatorShape brick6_braids_runtime_shape_from_edit(float edit)
{
    const int index = (int)(brick6_braids_runtime_clamp(edit, 0.0f, kBraidsEditMax) + 0.5f);
    return kBraidsShapeMap[index];
}

static uint32_t brick6_braids_runtime_compute_tail_samples(float release_s)
{
    const float clamped_release = brick6_braids_runtime_clamp(release_s, kBraidsTailMinSeconds, kBraidsTailMaxSeconds);
    const float tail_s = (clamped_release * kBraidsTailSafetyMultiplier) + kBraidsTailSafetyFloorSeconds;
    return (uint32_t)(tail_s * kBraidsSampleRate + 0.5f);
}

static brick6_braids_runtime_instance_t *brick6_braids_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_BRAIDS_VOICE_INSTANCE_COUNT)
    {
        return NULL;
    }
    if (instance_id < BRICK6_BRAIDS_MAX_INSTANCES)
    {
        return &g_braids_runtime[instance_id];
    }
    const uint8_t extra = (uint8_t)(instance_id - BRICK6_BRAIDS_MAX_INSTANCES);
    return &g_braids_poly_d2[extra];
}

static void brick6_braids_runtime_touch_config(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance =
        brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->config_version++;
    if (instance->config_version == 0U)
    {
        instance->config_version = 1U;
    }
}

static void brick6_braids_runtime_init_instance(brick6_braids_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
    {
        brick6_braids_runtime_osc_t *const osc = &instance->osc[osc_index];
        osc->voice.edit = 0.0f;
        osc->voice.fine = 0.5f;
        osc->voice.coarse = 0.5f;
        osc->voice.fm = 0.0f;
        osc->voice.timbre = 0.5f;
        osc->voice.modulation = 0.5f;
        osc->voice.color = 0.5f;
        osc->voice.note = 60.0f;
        osc->voice.velocity = 0.8f;
        osc->voice.active_note = 60U;
        osc->voice.has_active_note = 0U;
        osc->voice.gate = 0U;
        osc->voice.trigger = 0U;
        osc->phase_reset_enabled = 0U;
        osc->phase_reset_pending = 0U;
        osc->osc_level = (osc_index == 0U) ? 1.0f : 0.0f;
        osc->osc_level_current = osc->osc_level;
        osc->parameter_timbre_current = osc->voice.timbre;
        osc->parameter_color_current = osc->voice.color;
        osc->oscillator.Init();
        osc->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(osc->voice.edit));
        osc->pitch_current_q7 = (float)brick6_braids_runtime_pitch_to_q7(&osc->voice);
        osc->oscillator.set_pitch((int16_t)osc->pitch_current_q7);
        osc->oscillator.set_parameters(
            brick6_braids_runtime_float_to_u15(osc->voice.timbre),
            brick6_braids_runtime_float_to_u15(osc->voice.color));
    }
    instance->note = 60.0f;
    instance->velocity = 0.8f;
    instance->active_note = 60U;
    instance->has_active_note = 0U;
    instance->gate = 0U;
    instance->trigger = 0U;
    instance->has_note = 0U;
    instance->level = 0.0f;
    instance->vca_release_s = 0.001f;
    instance->tail_samples_remaining = 0U;
    instance->config_version = 1U;
    instance->synced_config_version = 0U;
}

}  // namespace

extern "C" {

void brick6_braids_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_BRAIDS_VOICE_INSTANCE_COUNT; ++instance)
    {
        brick6_braids_runtime_init_instance(
            brick6_braids_runtime_get_instance_mut(instance));
    }
}

void brick6_braids_runtime_reset_instance(uint8_t instance_id)
{
    brick6_braids_runtime_init_instance(brick6_braids_runtime_get_instance_mut(instance_id));
}

void brick6_braids_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance)
{
    brick6_braids_runtime_instance_t *const src =
        brick6_braids_runtime_get_instance_mut(track_instance);
    brick6_braids_runtime_instance_t *const dst =
        brick6_braids_runtime_get_instance_mut(voice_instance);
    if ((src == NULL) || (dst == NULL) || (src == dst))
    {
        return;
    }
    if (dst->synced_config_version == src->config_version)
    {
        return;
    }
    dst->vca_release_s = src->vca_release_s;
    for (uint8_t osc = 0U; osc < kBraidsOscCount; ++osc)
    {
        dst->osc[osc].voice.edit = src->osc[osc].voice.edit;
        dst->osc[osc].voice.fine = src->osc[osc].voice.fine;
        dst->osc[osc].voice.coarse = src->osc[osc].voice.coarse;
        dst->osc[osc].voice.fm = src->osc[osc].voice.fm;
        dst->osc[osc].voice.timbre = src->osc[osc].voice.timbre;
        dst->osc[osc].voice.modulation = src->osc[osc].voice.modulation;
        dst->osc[osc].voice.color = src->osc[osc].voice.color;
        dst->osc[osc].osc_level = src->osc[osc].osc_level;
        dst->osc[osc].phase_reset_enabled = src->osc[osc].phase_reset_enabled;
        dst->osc[osc].oscillator.set_shape(
            brick6_braids_runtime_shape_from_edit(dst->osc[osc].voice.edit));
    }
    dst->synced_config_version = src->config_version;
}

static brick6_braids_runtime_osc_t *brick6_braids_runtime_get_osc_mut(uint8_t instance_id, uint8_t osc)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= kBraidsOscCount))
    {
        return NULL;
    }
    return &instance->osc[osc];
}

void brick6_braids_runtime_set_osc_edit(uint8_t instance_id, uint8_t osc_index, float edit)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(edit, 0.0f, kBraidsEditMax);
        if (osc->voice.edit == next) return;
        osc->voice.edit = next;
        osc->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(osc->voice.edit));
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_fine(uint8_t instance_id, uint8_t osc_index, float fine)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(fine, 0.0f, 1.0f);
        if (osc->voice.fine == next) return;
        osc->voice.fine = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_coarse(uint8_t instance_id, uint8_t osc_index, float coarse)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(coarse, 0.0f, 1.0f);
        if (osc->voice.coarse == next) return;
        osc->voice.coarse = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_fm(uint8_t instance_id, uint8_t osc_index, float fm)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(fm, 0.0f, 1.0f);
        if (osc->voice.fm == next) return;
        osc->voice.fm = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_timbre(uint8_t instance_id, uint8_t osc_index, float timbre)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(timbre, 0.0f, 1.0f);
        if (osc->voice.timbre == next) return;
        osc->voice.timbre = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_modulation(uint8_t instance_id, uint8_t osc_index, float modulation)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(modulation, 0.0f, 1.0f);
        if (osc->voice.modulation == next) return;
        osc->voice.modulation = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_color(uint8_t instance_id, uint8_t osc_index, float color)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(color, 0.0f, 1.0f);
        if (osc->voice.color == next) return;
        osc->voice.color = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_phase_reset(uint8_t instance_id, uint8_t osc_index, uint8_t enabled)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const uint8_t next = (enabled != 0U) ? 1U : 0U;
        if (osc->phase_reset_enabled == next) return;
        osc->phase_reset_enabled = next;
        if (osc->phase_reset_enabled == 0U)
        {
            osc->phase_reset_pending = 0U;
        }
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_osc_level(uint8_t instance_id, uint8_t osc_index, float level)
{
    brick6_braids_runtime_osc_t *const osc = brick6_braids_runtime_get_osc_mut(instance_id, osc_index);
    if (osc != NULL)
    {
        const float next = brick6_braids_runtime_clamp(level, 0.0f, 1.0f);
        if (osc->osc_level == next) return;
        osc->osc_level = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_set_edit(uint8_t instance_id, float edit) { brick6_braids_runtime_set_osc_edit(instance_id, 0U, edit); }
void brick6_braids_runtime_set_fine(uint8_t instance_id, float fine) { brick6_braids_runtime_set_osc_fine(instance_id, 0U, fine); }
void brick6_braids_runtime_set_coarse(uint8_t instance_id, float coarse) { brick6_braids_runtime_set_osc_coarse(instance_id, 0U, coarse); }
void brick6_braids_runtime_set_fm(uint8_t instance_id, float fm) { brick6_braids_runtime_set_osc_fm(instance_id, 0U, fm); }
void brick6_braids_runtime_set_timbre(uint8_t instance_id, float timbre) { brick6_braids_runtime_set_osc_timbre(instance_id, 0U, timbre); }
void brick6_braids_runtime_set_modulation(uint8_t instance_id, float modulation) { brick6_braids_runtime_set_osc_modulation(instance_id, 0U, modulation); }
void brick6_braids_runtime_set_color(uint8_t instance_id, float color) { brick6_braids_runtime_set_osc_color(instance_id, 0U, color); }
void brick6_braids_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled) { brick6_braids_runtime_set_osc_phase_reset(instance_id, 0U, enabled); }
void brick6_braids_runtime_set_level(uint8_t instance_id, float level) { brick6_braids_runtime_set_osc_level(instance_id, 0U, level); }

void brick6_braids_runtime_set_vca_release_seconds(uint8_t instance_id, float release_s)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        const float next = brick6_braids_runtime_clamp(
            release_s, kBraidsTailMinSeconds, kBraidsTailMaxSeconds);
        if (instance->vca_release_s == next) return;
        instance->vca_release_s = next;
        brick6_braids_runtime_touch_config(instance_id);
    }
}

void brick6_braids_runtime_note_on(uint8_t instance_id, float note, float velocity)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    const float clamped_velocity = brick6_braids_runtime_clamp(velocity, 0.0f, 1.0f);
    const uint8_t midi_note = (uint8_t)brick6_braids_runtime_clamp(note, 0.0f, 127.0f);
    if (clamped_velocity <= 0.0f)
    {
        brick6_braids_runtime_note_off(instance_id, midi_note);
        return;
    }

    instance->note = brick6_braids_runtime_clamp(note, 0.0f, 127.0f);
    instance->velocity = clamped_velocity;
    instance->active_note = midi_note;
    instance->has_active_note = 1U;
    instance->gate = 1U;
    instance->trigger = 1U;
    for (uint8_t osc = 0U; osc < kBraidsOscCount; ++osc)
    {
        instance->osc[osc].voice.note = instance->note;
        instance->osc[osc].voice.velocity = clamped_velocity;
        instance->osc[osc].voice.active_note = midi_note;
        instance->osc[osc].voice.has_active_note = 1U;
        instance->osc[osc].voice.gate = 1U;
        instance->osc[osc].voice.trigger = 1U;
        instance->osc[osc].pitch_current_q7 =
            (float)brick6_braids_runtime_pitch_to_q7(&instance->osc[osc].voice);
        if (instance->osc[osc].phase_reset_enabled != 0U)
        {
            instance->osc[osc].phase_reset_pending = 1U;
        }
    }
    instance->has_note = 1U;
    instance->tail_samples_remaining = 0U;
}

void brick6_braids_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    if ((instance->has_active_note == 0U) || (instance->active_note != note))
    {
        return;
    }

    instance->has_active_note = 0U;
    instance->gate = 0U;
    instance->trigger = 0U;
    for (uint8_t osc = 0U; osc < kBraidsOscCount; ++osc)
    {
        instance->osc[osc].voice.has_active_note = 0U;
        instance->osc[osc].voice.gate = 0U;
        instance->osc[osc].voice.trigger = 0U;
    }
    instance->tail_samples_remaining = brick6_braids_runtime_compute_tail_samples(instance->vca_release_s);
}

void brick6_braids_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    instance->has_active_note = 0U;
    instance->gate = 0U;
    instance->trigger = 0U;
    for (uint8_t osc = 0U; osc < kBraidsOscCount; ++osc)
    {
        instance->osc[osc].voice.has_active_note = 0U;
        instance->osc[osc].voice.gate = 0U;
        instance->osc[osc].voice.trigger = 0U;
        instance->osc[osc].phase_reset_pending = 0U;
    }
    instance->tail_samples_remaining = 0U;
}

void brick6_braids_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->trigger = 0U;
        for (uint8_t osc = 0U; osc < kBraidsOscCount; ++osc)
        {
            instance->osc[osc].voice.trigger = 0U;
            instance->osc[osc].phase_reset_pending = 0U;
        }
    }
}

uint8_t brick6_braids_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if ((out_mono == NULL) || (frames == 0U))
    {
        return 0U;
    }

    if (instance == NULL)
    {
        memset(out_mono, 0, frames * sizeof(float));
        return 0U;
    }

    if ((instance->has_note == 0U) && (instance->gate == 0U) && (instance->trigger == 0U) && (instance->level <= 1.0e-5f))
    {
        return 0U;
    }

    const float velocity_gain = 0.2f + (brick6_braids_runtime_clamp(instance->velocity, 0.0f, 1.0f) * 0.8f);
    const float gate_target = ((instance->gate != 0U) || (instance->tail_samples_remaining > 0U)) ? velocity_gain : 0.0f;
    float osc_level_start[kBraidsOscCount];
    float osc_level_step[kBraidsOscCount];
    float pitch_target_q7[kBraidsOscCount];
    float parameter_timbre_target[kBraidsOscCount];
    float parameter_color_target[kBraidsOscCount];
    uint8_t any_osc_level = 0U;
    uint8_t audible_mask = 0U;
    uint8_t levels_stable = 1U;
    for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
    {
        brick6_braids_runtime_osc_t *const osc = &instance->osc[osc_index];
        osc_level_start[osc_index] =
            brick6_braids_runtime_clamp(osc->osc_level_current, 0.0f, 1.0f);
        osc_level_step[osc_index] =
            (brick6_braids_runtime_clamp(osc->osc_level, 0.0f, 1.0f)
                - osc_level_start[osc_index]) / (float)frames;
        if ((osc->osc_level > kBraidsOscActiveEpsilon)
                || (osc->osc_level_current > kBraidsOscActiveEpsilon))
        {
            any_osc_level = 1U;
            audible_mask = (uint8_t)(audible_mask | (uint8_t)(1U << osc_index));
        }
        if (osc_level_step[osc_index] != 0.0f)
        {
            levels_stable = 0U;
        }
    }
    if (any_osc_level == 0U)
    {
        instance->trigger = 0U;
        for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
        {
            instance->osc[osc_index].voice.trigger = 0U;
        }
        return 0U;
    }
    for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
    {
        brick6_braids_runtime_osc_t *const osc = &instance->osc[osc_index];
        parameter_timbre_target[osc_index] = brick6_braids_runtime_clamp(
            osc->voice.timbre + ((osc->voice.modulation - 0.5f) * 0.5f),
            0.0f,
            1.0f);
        parameter_color_target[osc_index] = brick6_braids_runtime_clamp(
            osc->voice.color, 0.0f, 1.0f);
        if ((audible_mask & (uint8_t)(1U << osc_index)) == 0U)
        {
            continue;
        }
        osc->voice.note = instance->note;
        osc->voice.velocity = instance->velocity;
        osc->voice.active_note = instance->active_note;
        osc->voice.has_active_note = instance->has_active_note;
        osc->voice.gate = instance->gate;
        pitch_target_q7[osc_index] =
            (float)brick6_braids_runtime_pitch_to_q7(&osc->voice);
        osc->oscillator.set_shape(brick6_braids_runtime_shape_from_edit(osc->voice.edit));
    }
    uint32_t offset = 0U;
    uint8_t sync_block[kBraidsRenderBlockSize] = {};
    int16_t sample_block[kBraidsOscCount][kBraidsRenderBlockSize];
    uint8_t trigger_pending = instance->trigger;
    const uint8_t single_osc = (audible_mask == 1U) ? 0U : ((audible_mask == 2U) ? 1U : 0xffU);
    float stable_mix_norm = 1.0f;
    if ((single_osc == 0xffU) && (levels_stable != 0U))
    {
        const float level_sum = osc_level_start[0] + osc_level_start[1];
        if (level_sum > 1.0f)
        {
            stable_mix_norm = 1.0f / level_sum;
        }
    }

    while (offset < frames)
    {
        const uint32_t remaining = frames - offset;
        const uint8_t render_count =
            (remaining > kBraidsRenderBlockSize)
                ? (uint8_t)kBraidsRenderBlockSize
                : (uint8_t)remaining;
        for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
        {
            if ((audible_mask & (uint8_t)(1U << osc_index)) == 0U)
            {
                continue;
            }
            brick6_braids_runtime_osc_t *const osc = &instance->osc[osc_index];
            if (osc->phase_reset_pending != 0U)
            {
                sync_block[0] = 1U;
                osc->phase_reset_pending = 0U;
            }
            if (trigger_pending != 0U)
            {
                osc->oscillator.Strike();
            }
            osc->voice.trigger = trigger_pending;
            const float pitch_progress =
                ((uint32_t)render_count >= remaining)
                    ? 1.0f
                    : ((float)render_count / (float)remaining);
            osc->pitch_current_q7 +=
                (pitch_target_q7[osc_index] - osc->pitch_current_q7) * pitch_progress;
            osc->parameter_timbre_current +=
                (parameter_timbre_target[osc_index] - osc->parameter_timbre_current)
                * pitch_progress;
            osc->parameter_color_current +=
                (parameter_color_target[osc_index] - osc->parameter_color_current)
                * pitch_progress;
            osc->oscillator.set_pitch((int16_t)(osc->pitch_current_q7 + 0.5f));
            osc->oscillator.set_parameters(
                brick6_braids_runtime_float_to_u15(osc->parameter_timbre_current),
                brick6_braids_runtime_float_to_u15(osc->parameter_color_current));
            const uint8_t *const sync_input =
                (osc->phase_reset_enabled != 0U) ? sync_block : NULL;
            osc->oscillator.Render(
                sync_input, sample_block[osc_index], (size_t)render_count);
            sync_block[0] = 0U;
        }
        if (trigger_pending != 0U)
        {
            trigger_pending = 0U;
            instance->trigger = 0U;
        }

        for (uint8_t i = 0U; i < render_count; ++i)
        {
            const float coeff = (gate_target > instance->level) ? 0.05f : (1.0f - kBraidsReleaseCoeff);
            instance->level += (gate_target - instance->level) * coeff;
            float mixed;
            float mix_norm = stable_mix_norm;
            if (single_osc != 0xffU)
            {
                brick6_braids_runtime_osc_t *const osc = &instance->osc[single_osc];
                osc_level_start[single_osc] += osc_level_step[single_osc];
                osc->osc_level_current = osc_level_start[single_osc];
                const float osc_level = osc->osc_level_current;
                if (osc_level <= kBraidsOscActiveEpsilon)
                {
                    mixed = 0.0f;
                }
                else
                {
                    const int16_t sample = sample_block[single_osc][i];
                    const float converted = (float)sample / 32768.0f;
                    mixed = converted * osc_level;
                }
            }
            else
            {
                mixed = 0.0f;
                float osc_level_sum = 0.0f;
                for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
                {
                    brick6_braids_runtime_osc_t *const osc = &instance->osc[osc_index];
                    osc_level_start[osc_index] += osc_level_step[osc_index];
                    osc->osc_level_current = osc_level_start[osc_index];
                    const float osc_level = osc->osc_level_current;
                    if (osc_level <= kBraidsOscActiveEpsilon)
                    {
                        continue;
                    }
                    const int16_t sample = sample_block[osc_index][i];
                    const float converted = (float)sample / 32768.0f;
                    mixed += converted * osc_level;
                    osc_level_sum += osc_level;
                }
                if (levels_stable == 0U)
                {
                    mix_norm = (osc_level_sum > 1.0f) ? (1.0f / osc_level_sum) : 1.0f;
                }
            }
            out_mono[offset + i] = brick6_braids_runtime_clamp(mixed * mix_norm * instance->level, -1.0f, 1.0f) * BRAIDS_OUTPUT_TRIM;
            if ((instance->gate == 0U) && (instance->tail_samples_remaining > 0U))
            {
                instance->tail_samples_remaining--;
            }
        }

        offset += (uint32_t)render_count;
    }
    for (uint8_t osc_index = 0U; osc_index < kBraidsOscCount; ++osc_index)
    {
        instance->osc[osc_index].osc_level_current =
            instance->osc[osc_index].osc_level;
        instance->osc[osc_index].parameter_timbre_current =
            parameter_timbre_target[osc_index];
        instance->osc[osc_index].parameter_color_current =
            parameter_color_target[osc_index];
    }
    if ((instance->gate == 0U) && (instance->tail_samples_remaining == 0U) && (instance->level <= 1.0e-5f))
    {
        instance->level = 0.0f;
        instance->has_note = 0U;
    }
    return 1U;
}

const brick6_braids_runtime_voice_t *brick6_braids_runtime_get_voice(uint8_t instance_id)
{
    brick6_braids_runtime_instance_t *const instance = brick6_braids_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return NULL;
    }
    instance->osc[0].voice.note = instance->note;
    instance->osc[0].voice.velocity = instance->velocity;
    instance->osc[0].voice.active_note = instance->active_note;
    instance->osc[0].voice.has_active_note = instance->has_active_note;
    instance->osc[0].voice.gate = instance->gate;
    instance->osc[0].voice.trigger = instance->trigger;
    return &instance->osc[0].voice;
}

}  // extern "C"
