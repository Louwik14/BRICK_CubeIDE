/**
 * @file brick6_stack_runtime.c
 * @brief Minimal independent Stack runtime, without MacroOscillator ownership.
 */

#include "Core/brick6_stack_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Core/brick6_stack_waveform.h"
#include "Audio/audio_float.h"
#include "Audio/deluge_oscillator.h"
#include "Storage/memory_layout.h"

#define STACK_DEFAULT_RNG_SEED 0x6D2B79F5UL
#define STACK_DEFAULT_NOTE 60U
#define STACK_BASE_C4_INC 23428135UL
#define STACK_OSC_DETUNE_MAX_RATIO_Q15 228U
#define STACK_OUTPUT_TRIM 0.30f
#define STACK_SQUARE_COMPAT_BLOCK_SIZE 24U
#define STACK_SOURCE_NOISE_BIT (1U << BRICK6_STACK_SLOT_COUNT)

enum
{
    STACK_RENDERER_DELUGE_SINE = 0,
    STACK_RENDERER_DELUGE_TRI,
    STACK_RENDERER_DELUGE_SQUARE,
    STACK_RENDERER_DELUGE_SAW,
    STACK_RENDERER_SHAPE,
    STACK_RENDERER_TRIPLE_SAW,
    STACK_RENDERER_SILENT,
    STACK_RENDERER_COUNT
};

typedef struct
{
    const char *name;
    uint8_t family;
    uint8_t kernel_id;
    uint8_t renderer_id;
} brick6_stack_model_desc_t;

enum
{
    STACK_CONT_LEVEL_BASE = 0,
    STACK_CONT_TUNE_BASE = STACK_CONT_LEVEL_BASE + BRICK6_STACK_SLOT_COUNT,
    STACK_CONT_TIMBRE_BASE = STACK_CONT_TUNE_BASE + BRICK6_STACK_SLOT_COUNT,
    STACK_CONT_COLOR_BASE = STACK_CONT_TIMBRE_BASE + BRICK6_STACK_SLOT_COUNT,
    STACK_CONT_NOISE = STACK_CONT_COLOR_BASE + BRICK6_STACK_SLOT_COUNT,
    STACK_CONT_COUNT
};

typedef struct
{
    brick6_stack_runtime_voice_t voice;
    uint8_t release_source_active;
    stack_osc_slot_t slots[BRICK6_STACK_SLOT_COUNT];
    uint16_t noise_level_q15;
    uint16_t noise_level_current_q15;
    uint16_t osc_detune_q15;
    uint32_t rng;
    int16_t osc_detune_offset_q15[BRICK6_STACK_SLOT_COUNT];
    uint8_t active_source_mask;
    uint8_t ramp_mask;
    uint8_t phase_reset;
    uint32_t config_version;
    uint32_t synced_config_version;
    uint32_t continuous_epoch;
    uint32_t continuous_version[STACK_CONT_COUNT];
} brick6_stack_runtime_instance_t;

_Static_assert(sizeof(stack_osc_slot_t) <= 192U, "stack_osc_slot_t RAM budget exceeded");
_Static_assert(sizeof(brick6_stack_runtime_instance_t) <= 768U, "brick6_stack_runtime_instance_t RAM budget exceeded");

AUDIO_HOT static brick6_stack_runtime_instance_t g_stack_runtime[BRICK6_STACK_MAX_INSTANCES];
enum { STACK_POLY_D2_COUNT = BRICK6_STACK_VOICE_INSTANCE_COUNT - BRICK6_STACK_MAX_INSTANCES };
AUDIO_HOT static brick6_stack_runtime_instance_t g_stack_poly_runtime_d2[STACK_POLY_D2_COUNT];
static uint32_t g_stack_continuous_version;
AUDIO_HOT static int32_t g_stack_native_scratch[AUDIO_BLOCK_SIZE];
AUDIO_HOT static int32_t g_stack_acc_scratch[AUDIO_BLOCK_SIZE];

static const brick6_stack_model_desc_t k_stack_model_catalog[BRICK6_STACK_MODEL_COUNT] = {
    [BRICK6_STACK_MODEL_SINE] = {
        "SINE", BRICK6_STACK_FAMILY_DELUGE, BRICK6_STACK_KERNEL_DELUGE, STACK_RENDERER_DELUGE_SINE
    },
    [BRICK6_STACK_MODEL_TRI] = {
        "TRI", BRICK6_STACK_FAMILY_DELUGE, BRICK6_STACK_KERNEL_DELUGE, STACK_RENDERER_DELUGE_TRI
    },
    [BRICK6_STACK_MODEL_SQUARE] = {
        "SQUARE", BRICK6_STACK_FAMILY_DELUGE, BRICK6_STACK_KERNEL_DELUGE, STACK_RENDERER_DELUGE_SQUARE
    },
    [BRICK6_STACK_MODEL_SAW] = {
        "SAW", BRICK6_STACK_FAMILY_DELUGE, BRICK6_STACK_KERNEL_DELUGE, STACK_RENDERER_DELUGE_SAW
    },
    [BRICK6_STACK_MODEL_SHAPE] = {
        "SHAPE", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_PHASE_BASIC, STACK_RENDERER_SHAPE
    },
    [BRICK6_STACK_MODEL_TRIPLE_SAW] = {
        "TRIPLE SAW", BRICK6_STACK_FAMILY_ENSEMBLE, BRICK6_STACK_KERNEL_TRIPLE_ANALOG, STACK_RENDERER_TRIPLE_SAW
    },
};

static const uint16_t k_stack_semitone_ratio_q15[12] = {
    32768U, 34716U, 36781U, 38968U, 41285U, 43740U,
    46341U, 49097U, 52016U, 55109U, 58386U, 61858U
};

static uint16_t brick6_stack_float_to_q15(float value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }
    if (value >= 1.0f)
    {
        return 32767U;
    }
    return (uint16_t)(value * 32767.0f + 0.5f);
}

static brick6_stack_model_t brick6_stack_runtime_normalize_model(brick6_stack_model_t model)
{
    return ((uint8_t)model < (uint8_t)BRICK6_STACK_MODEL_COUNT)
        ? model : BRICK6_STACK_MODEL_SHAPE;
}

static const brick6_stack_model_desc_t *brick6_stack_runtime_model_desc(brick6_stack_model_t model)
{
    return &k_stack_model_catalog[(uint8_t)brick6_stack_runtime_normalize_model(model)];
}

static brick6_stack_runtime_instance_t *brick6_stack_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT)
    {
        return NULL;
    }
    if (instance_id < BRICK6_STACK_MAX_INSTANCES)
        return &g_stack_runtime[instance_id];
    uint8_t index = (uint8_t)(instance_id - BRICK6_STACK_MAX_INSTANCES);
    return &g_stack_poly_runtime_d2[index];
}

static const brick6_stack_runtime_instance_t *brick6_stack_runtime_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT)
    {
        return NULL;
    }
    return brick6_stack_runtime_get_instance_mut(instance_id);
}

static void brick6_stack_runtime_touch_config(brick6_stack_runtime_instance_t *instance)
{
    uint32_t version = instance->config_version + 1U;
    instance->config_version = (version != 0U) ? version : 1U;
}

static void brick6_stack_runtime_touch_continuous(
    brick6_stack_runtime_instance_t *instance,
    uint8_t param)
{
    if ((instance == NULL) || (param >= STACK_CONT_COUNT)) return;
    g_stack_continuous_version++;
    if (g_stack_continuous_version == 0U) g_stack_continuous_version = 1U;
    instance->continuous_version[param] = g_stack_continuous_version;
    instance->continuous_epoch = g_stack_continuous_version;
}

static uint32_t brick6_stack_note_cents_to_phase_inc(int32_t note_cents)
{
    int32_t delta_cents = note_cents - ((int32_t)STACK_DEFAULT_NOTE * 100L);
    int8_t octave = 0;
    while (delta_cents < 0)
    {
        delta_cents += 1200L;
        octave--;
    }
    while (delta_cents >= 1200L)
    {
        delta_cents -= 1200L;
        octave++;
    }

    const uint8_t semitone = (uint8_t)(delta_cents / 100L);
    const uint8_t cents = (uint8_t)(delta_cents - ((int32_t)semitone * 100L));
    const uint32_t ratio0 = k_stack_semitone_ratio_q15[semitone];
    const uint32_t ratio1 = (semitone < 11U)
        ? k_stack_semitone_ratio_q15[semitone + 1U]
        : ((uint32_t)k_stack_semitone_ratio_q15[0] << 1);
    const uint32_t ratio = ratio0 + (uint32_t)((((int32_t)ratio1 - (int32_t)ratio0) * (int32_t)cents + 50L) / 100L);
    uint64_t inc = ((uint64_t)STACK_BASE_C4_INC * (uint64_t)ratio) >> 15;
    if (octave > 0)
    {
        for (int8_t i = 0; i < octave; ++i)
        {
            inc <<= 1;
            if (inc > 0x7FFFFFFFULL)
            {
                inc = 0x7FFFFFFFULL;
                break;
            }
        }
    }
    else if (octave < 0)
    {
        for (int8_t i = 0; i > octave; --i)
        {
            inc >>= 1;
        }
    }

    if (inc == 0U)
    {
        inc = 1U;
    }
    if (inc > 0x7FFFFFFFULL)
    {
        inc = 0x7FFFFFFFULL;
    }
    return (uint32_t)inc;
}

static uint32_t brick6_stack_note_to_phase_inc(int16_t note)
{
    return brick6_stack_note_cents_to_phase_inc((int32_t)note * 100L);
}

static int16_t brick6_stack_tune_to_cents(float semitones)
{
    if (semitones < -24.0f)
    {
        semitones = -24.0f;
    }
    if (semitones > 24.0f)
    {
        semitones = 24.0f;
    }
    const float cents = semitones * 100.0f;
    return (int16_t)((cents >= 0.0f) ? (cents + 0.5f) : (cents - 0.5f));
}

static uint32_t brick6_stack_runtime_next_rng(brick6_stack_runtime_instance_t *instance)
{
    uint32_t x = instance->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    instance->rng = x;
    return x;
}

static uint32_t brick6_stack_apply_osc_detune(uint32_t inc, uint16_t amount_q15, int16_t offset_q15)
{
    if ((amount_q15 == 0U) || (offset_q15 == 0))
    {
        return inc;
    }

    const uint32_t span = (uint32_t)(((uint64_t)inc
        * STACK_OSC_DETUNE_MAX_RATIO_Q15) >> 15);
    int32_t delta = (int32_t)(((uint64_t)span * (uint64_t)amount_q15) >> 15);
    delta = (delta * (int32_t)offset_q15) >> 15;
    if (delta < 0)
    {
        const uint32_t abs_delta = (uint32_t)(-delta);
        return (inc > abs_delta) ? (inc - abs_delta) : 1U;
    }
    return (inc < (0x7FFFFFFFUL - (uint32_t)delta)) ? (inc + (uint32_t)delta) : 0x7FFFFFFFUL;
}

static void brick6_stack_runtime_prepare_slot(stack_osc_slot_t *slot);
static void brick6_stack_runtime_refresh_masks(brick6_stack_runtime_instance_t *instance);

static uint8_t brick6_stack_runtime_osc_detune_offsets_are_zero(const brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return 1U;
    }

    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        if (instance->osc_detune_offset_q15[slot] != 0)
        {
            return 0U;
        }
    }
    return 1U;
}

static void brick6_stack_runtime_generate_osc_detune_offsets(brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        const uint32_t r = brick6_stack_runtime_next_rng(instance);
        const int16_t offset = (int16_t)((int32_t)(r & 0xFFFFU) - 32768L);
        instance->osc_detune_offset_q15[slot] = offset;
    }
}

static void brick6_stack_runtime_update_slot_pitch(brick6_stack_runtime_instance_t *instance,
                                                   uint8_t slot)
{
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }

    const int32_t note_cents = ((int32_t)instance->voice.active_note * 100L)
                             + (int32_t)instance->slots[slot].tune_cents;
    instance->slots[slot].phase_inc = brick6_stack_apply_osc_detune(brick6_stack_note_cents_to_phase_inc(note_cents),
                                                                     instance->osc_detune_q15,
                                                                     instance->osc_detune_offset_q15[slot]);
    brick6_stack_runtime_prepare_slot(&instance->slots[slot]);
}

static void brick6_stack_runtime_update_all_pitches(brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        brick6_stack_runtime_update_slot_pitch(instance, slot);
    }
}

static void brick6_stack_runtime_init_slot(stack_osc_slot_t *slot, uint8_t enabled)
{
    if (slot == NULL)
    {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    const brick6_stack_model_desc_t *const desc = brick6_stack_runtime_model_desc(BRICK6_STACK_MODEL_SHAPE);
    slot->model = (uint8_t)BRICK6_STACK_MODEL_SHAPE;
    slot->family = desc->family;
    slot->kernel_id = desc->kernel_id;
    slot->renderer_id = desc->renderer_id;
    slot->level = enabled;
    slot->level_q15 = (enabled != 0U) ? 32767U : 0U;
    slot->level_current_q15 = slot->level_q15;
    slot->timbre = 127U;
    slot->color = 127U;
    slot->timbre_q15 = 16384U;
    slot->color_q15 = 16384U;
    slot->timbre_current_q15 = slot->timbre_q15;
    slot->color_current_q15 = slot->color_q15;
    slot->phase_inc = brick6_stack_note_to_phase_inc(STACK_DEFAULT_NOTE);
    slot->phase_inc_current = slot->phase_inc;
    slot->phase2 = 0x55555555UL;
    slot->phase3 = 0xAAAAAAAAUL;
}

static void brick6_stack_runtime_init_instance(brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }

    memset(instance, 0, sizeof(*instance));
    instance->voice.active_note = STACK_DEFAULT_NOTE;
    instance->voice.velocity_q15 = 32767U;
    brick6_stack_runtime_init_slot(&instance->slots[0], 1U);
    brick6_stack_runtime_init_slot(&instance->slots[1], 0U);
    brick6_stack_runtime_init_slot(&instance->slots[2], 0U);
    instance->noise_level_q15 = 0U;
    instance->noise_level_current_q15 = 0U;
    instance->osc_detune_q15 = 0U;
    instance->phase_reset = 0U;
    instance->rng = STACK_DEFAULT_RNG_SEED;
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        instance->osc_detune_offset_q15[slot] = 0;
    }
    brick6_stack_runtime_update_all_pitches(instance);
    instance->config_version = 1U;
    brick6_stack_runtime_refresh_masks(instance);
}

static int16_t brick6_stack_phase_saw(uint32_t phase)
{
    return brick6_stack_waveform_saw(phase);
}

static int32_t brick6_stack_average3_q15(int16_t a, int16_t b, int16_t c)
{
    return (((int32_t)a + (int32_t)b + (int32_t)c) * 10923L) >> 15;
}

static uint32_t brick6_stack_detune_inc(uint32_t inc, uint16_t detune_q15, int8_t sign)
{
    const uint32_t span = inc >> 6;
    const uint32_t delta = ((uint64_t)span * (uint64_t)detune_q15) >> 15;
    if (sign < 0)
    {
        return (inc > delta) ? (inc - delta) : 1U;
    }
    return (inc < (0x7FFFFFFFUL - delta)) ? (inc + delta) : 0x7FFFFFFFUL;
}

static void brick6_stack_runtime_prepare_slot(stack_osc_slot_t *slot)
{
    if (slot == NULL) return;
    if (slot->renderer_id == STACK_RENDERER_TRIPLE_SAW)
    {
        slot->phase_inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
        slot->phase_inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, 1);
    }
    else
    {
        slot->phase_inc2 = slot->phase_inc;
        slot->phase_inc3 = slot->phase_inc;
    }
}

static uint32_t brick6_stack_deluge_pulse_width(uint16_t timbre_q15)
{
    const int32_t bipolar = ((int32_t)timbre_q15 * 2) - 32767;
    return (uint32_t)((int64_t)bipolar * 65536LL);
}

static void brick6_stack_runtime_render_deluge(stack_osc_slot_t *slot,
                                               int32_t *out,
                                               uint32_t frames,
                                               deluge_osc_type_t type,
                                               uint8_t pulse_width_from_timbre)
{
    if ((slot == NULL) || (out == NULL) || (frames == 0U))
    {
        return;
    }
    deluge_oscillator_render(type,
                             out,
                             frames,
                             slot->phase_inc,
                             (pulse_width_from_timbre != 0U)
                                 ? brick6_stack_deluge_pulse_width(slot->timbre_q15) : 0U,
                             &slot->phase);
}

static uint8_t brick6_stack_runtime_render_native(stack_osc_slot_t *slot,
                                                  int32_t *out,
                                                  uint32_t frames)
{
    if ((slot == NULL) || (out == NULL) || (frames == 0U))
    {
        return 0U;
    }
    switch (slot->renderer_id)
    {
        case STACK_RENDERER_DELUGE_SINE:
            brick6_stack_runtime_render_deluge(slot, out, frames, DELUGE_OSC_SINE, 0U);
            return 1U;
        case STACK_RENDERER_DELUGE_TRI:
            brick6_stack_runtime_render_deluge(slot, out, frames, DELUGE_OSC_TRIANGLE, 0U);
            return 1U;
        case STACK_RENDERER_DELUGE_SQUARE:
            brick6_stack_runtime_render_deluge(slot, out, frames, DELUGE_OSC_ANALOG_SQUARE, 1U);
            return 1U;
        case STACK_RENDERER_DELUGE_SAW:
            brick6_stack_runtime_render_deluge(slot, out, frames, DELUGE_OSC_ANALOG_SAW, 0U);
            return 1U;
        case STACK_RENDERER_SHAPE:
            for (uint32_t i = 0U; i < frames; ++i)
            {
                slot->phase += slot->phase_inc;
                out[i] = brick6_stack_waveform_shape(
                    slot->phase, slot->timbre_q15, slot->color_q15);
            }
            return 0U;
        case STACK_RENDERER_TRIPLE_SAW:
            for (uint32_t i = 0U; i < frames; ++i)
            {
                slot->phase += slot->phase_inc;
                slot->phase2 += slot->phase_inc2;
                slot->phase3 += slot->phase_inc3;
                out[i] = brick6_stack_average3_q15(
                    brick6_stack_phase_saw(slot->phase),
                    brick6_stack_phase_saw(slot->phase2),
                    brick6_stack_phase_saw(slot->phase3));
            }
            return 0U;
        default:
            memset(out, 0, frames * sizeof(*out));
            return 0U;
    }
}

static void brick6_stack_runtime_advance_slot_free_running(stack_osc_slot_t *slot, uint8_t frames)
{
    if ((slot == NULL) || (frames == 0U))
    {
        return;
    }

    switch (slot->renderer_id)
    {
        case STACK_RENDERER_DELUGE_SINE:
        case STACK_RENDERER_DELUGE_TRI:
        case STACK_RENDERER_DELUGE_SQUARE:
        case STACK_RENDERER_DELUGE_SAW:
        case STACK_RENDERER_SHAPE:
            slot->phase += slot->phase_inc * (uint32_t)frames;
            break;

        case STACK_RENDERER_TRIPLE_SAW:
        {
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += slot->phase_inc2 * (uint32_t)frames;
            slot->phase3 += slot->phase_inc3 * (uint32_t)frames;
            break;
        }

        default:
            break;
    }
}

static void brick6_stack_runtime_advance_free_running(brick6_stack_runtime_instance_t *instance, uint8_t frames)
{
    if ((instance == NULL) || (frames == 0U) || (instance->phase_reset != 0U))
    {
        return;
    }

    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        brick6_stack_runtime_advance_slot_free_running(&instance->slots[slot], frames);
    }
}

static int16_t brick6_stack_runtime_render_noise_sample(brick6_stack_runtime_instance_t *instance)
{
    uint32_t x = instance->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    instance->rng = x;
    return (int16_t)(x >> 16);
}

typedef struct
{
    uint64_t whole;
    uint32_t remainder;
    uint32_t denominator;
    uint32_t remainder_acc;
    uint64_t magnitude;
    int8_t sign;
} brick6_stack_exact_ramp_t;

static __attribute__((noinline)) void brick6_stack_exact_ramp_init(
    brick6_stack_exact_ramp_t *ramp,
    int64_t delta,
    uint32_t denominator)
{
    const uint64_t magnitude = (delta < 0) ? (uint64_t)(-delta) : (uint64_t)delta;
    ramp->whole = magnitude / denominator;
    ramp->remainder = (uint32_t)(magnitude % denominator);
    ramp->denominator = denominator;
    ramp->remainder_acc = 0U;
    ramp->magnitude = 0U;
    ramp->sign = (delta < 0) ? -1 : 1;
}

static int64_t brick6_stack_exact_ramp_next(brick6_stack_exact_ramp_t *ramp)
{
    ramp->magnitude += ramp->whole;
    ramp->remainder_acc += ramp->remainder;
    if (ramp->remainder_acc >= ramp->denominator)
    {
        ramp->remainder_acc -= ramp->denominator;
        ramp->magnitude++;
    }
    return (ramp->sign < 0) ? -(int64_t)ramp->magnitude : (int64_t)ramp->magnitude;
}

static uint8_t brick6_stack_runtime_slot_is_ramping(const stack_osc_slot_t *slot)
{
    return (uint8_t)((slot->level_current_q15 != slot->level_q15)
        || (slot->phase_inc_current != slot->phase_inc)
        || (slot->timbre_current_q15 != slot->timbre_q15)
        || (slot->color_current_q15 != slot->color_q15));
}

static void brick6_stack_runtime_refresh_masks(brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL) return;
    uint8_t active = 0U;
    uint8_t ramps = 0U;
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        const stack_osc_slot_t *const osc = &instance->slots[slot];
        const uint8_t bit = (uint8_t)(1U << slot);
        if ((osc->level_q15 != 0U) || (osc->level_current_q15 != 0U)) active |= bit;
        if (brick6_stack_runtime_slot_is_ramping(osc) != 0U) ramps |= bit;
    }
    if ((instance->noise_level_q15 != 0U) || (instance->noise_level_current_q15 != 0U))
        active |= STACK_SOURCE_NOISE_BIT;
    if (instance->noise_level_q15 != instance->noise_level_current_q15)
        ramps |= STACK_SOURCE_NOISE_BIT;
    instance->active_source_mask = active;
    instance->ramp_mask = ramps;
}

static int32_t brick6_stack_runtime_native_to_q15(int32_t sample, uint8_t q31)
{
    return (q31 != 0U) ? (sample >> 16) : sample;
}

static void brick6_stack_runtime_accumulate_constant(
    brick6_stack_runtime_instance_t *instance,
    stack_osc_slot_t *osc,
    uint32_t acc_offset,
    uint32_t frames)
{
    const uint8_t q31 = brick6_stack_runtime_render_native(
        osc, g_stack_native_scratch, frames);
    const uint16_t effective_level = (uint16_t)(((uint32_t)osc->level_q15
        * (uint32_t)instance->voice.velocity_q15) >> 15);
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const int32_t sample_q15 = brick6_stack_runtime_native_to_q15(
            g_stack_native_scratch[i], q31);
        g_stack_acc_scratch[acc_offset + i] +=
            (sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_accumulate_level_ramp(
    brick6_stack_runtime_instance_t *instance,
    stack_osc_slot_t *osc,
    uint32_t acc_offset,
    uint32_t frames)
{
    const uint8_t q31 = brick6_stack_runtime_render_native(
        osc, g_stack_native_scratch, frames);
    const int32_t level_start = osc->level_current_q15;
    brick6_stack_exact_ramp_t level_ramp;
    brick6_stack_exact_ramp_init(
        &level_ramp, (int32_t)osc->level_q15 - level_start, frames);
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const int32_t sample_q15 = brick6_stack_runtime_native_to_q15(
            g_stack_native_scratch[i], q31);
        const int32_t velocity_sample =
            (sample_q15 * (int32_t)instance->voice.velocity_q15) >> 15;
        const uint16_t level_here = (uint16_t)(level_start
            + (int32_t)brick6_stack_exact_ramp_next(&level_ramp));
        g_stack_acc_scratch[acc_offset + i] +=
            (velocity_sample * (int32_t)level_here) >> 15;
    }
    osc->level_current_q15 = osc->level_q15;
}

static void brick6_stack_runtime_accumulate_control_ramp(
    brick6_stack_runtime_instance_t *instance,
    stack_osc_slot_t *osc,
    uint32_t acc_offset,
    uint32_t frames)
{
    const int32_t level_start = osc->level_current_q15;
    const uint32_t pitch_start = osc->phase_inc_current;
    const int32_t timbre_start = osc->timbre_current_q15;
    const int32_t color_start = osc->color_current_q15;
    const uint16_t level_target = osc->level_q15;
    const uint32_t pitch_target = osc->phase_inc;
    const uint16_t timbre_target = osc->timbre_q15;
    const uint16_t color_target = osc->color_q15;
    brick6_stack_exact_ramp_t level_ramp;
    brick6_stack_exact_ramp_t pitch_ramp;
    brick6_stack_exact_ramp_t timbre_ramp;
    brick6_stack_exact_ramp_t color_ramp;
    brick6_stack_exact_ramp_init(&level_ramp, (int32_t)level_target - level_start, frames);
    brick6_stack_exact_ramp_init(&pitch_ramp, (int64_t)pitch_target - pitch_start, frames);
    brick6_stack_exact_ramp_init(&timbre_ramp, (int32_t)timbre_target - timbre_start, frames);
    brick6_stack_exact_ramp_init(&color_ramp, (int32_t)color_target - color_start, frames);

    for (uint32_t offset = 0U; offset < frames;)
    {
        uint32_t chunk = frames - offset;
        if (chunk > 8U) chunk = 8U;
        int64_t level_progress = 0;
        int64_t pitch_progress = 0;
        int64_t timbre_progress = 0;
        int64_t color_progress = 0;
        for (uint32_t i = 0U; i < chunk; ++i)
        {
            level_progress = brick6_stack_exact_ramp_next(&level_ramp);
            pitch_progress = brick6_stack_exact_ramp_next(&pitch_ramp);
            timbre_progress = brick6_stack_exact_ramp_next(&timbre_ramp);
            color_progress = brick6_stack_exact_ramp_next(&color_ramp);
        }
        osc->phase_inc = (uint32_t)((int64_t)pitch_start + pitch_progress);
        osc->timbre_q15 = (uint16_t)(timbre_start + (int32_t)timbre_progress);
        osc->color_q15 = (uint16_t)(color_start + (int32_t)color_progress);
        brick6_stack_runtime_prepare_slot(osc);
        const uint8_t q31 = brick6_stack_runtime_render_native(
            osc, g_stack_native_scratch, chunk);
        const uint16_t level_here = (uint16_t)(level_start + (int32_t)level_progress);
        const uint16_t effective_level = (uint16_t)(((uint32_t)level_here
            * (uint32_t)instance->voice.velocity_q15) >> 15);
        for (uint32_t i = 0U; i < chunk; ++i)
        {
            const int32_t sample_q15 = brick6_stack_runtime_native_to_q15(
                g_stack_native_scratch[i], q31);
            g_stack_acc_scratch[acc_offset + offset + i] +=
                (sample_q15 * (int32_t)effective_level) >> 15;
        }
        offset += chunk;
    }
    osc->level_q15 = level_target;
    osc->phase_inc = pitch_target;
    osc->timbre_q15 = timbre_target;
    osc->color_q15 = color_target;
    osc->level_current_q15 = level_target;
    osc->phase_inc_current = pitch_target;
    osc->timbre_current_q15 = timbre_target;
    osc->color_current_q15 = color_target;
    brick6_stack_runtime_prepare_slot(osc);
}

static void brick6_stack_runtime_render_slot(
    brick6_stack_runtime_instance_t *instance,
    uint8_t slot,
    uint32_t frames)
{
    stack_osc_slot_t *const osc = &instance->slots[slot];
    uint32_t offset = 0U;
    while (offset < frames)
    {
        uint32_t chunk = frames - offset;
        if ((osc->renderer_id == STACK_RENDERER_DELUGE_SQUARE)
                && (chunk > STACK_SQUARE_COMPAT_BLOCK_SIZE))
            chunk = STACK_SQUARE_COMPAT_BLOCK_SIZE;
        const uint8_t control_ramp = (uint8_t)(
            (osc->phase_inc_current != osc->phase_inc)
            || (osc->timbre_current_q15 != osc->timbre_q15)
            || (osc->color_current_q15 != osc->color_q15));
        if (((control_ramp != 0U) || (osc->level_current_q15 != osc->level_q15))
                && (chunk > STACK_SQUARE_COMPAT_BLOCK_SIZE))
            chunk = STACK_SQUARE_COMPAT_BLOCK_SIZE;
        if (control_ramp != 0U)
            brick6_stack_runtime_accumulate_control_ramp(instance, osc, offset, chunk);
        else if (osc->level_current_q15 != osc->level_q15)
            brick6_stack_runtime_accumulate_level_ramp(instance, osc, offset, chunk);
        else
            brick6_stack_runtime_accumulate_constant(instance, osc, offset, chunk);
        offset += chunk;
    }
}

static void brick6_stack_runtime_render_noise(
    brick6_stack_runtime_instance_t *instance,
    uint32_t frames)
{
    if (instance->noise_level_current_q15 == instance->noise_level_q15)
    {
        const uint16_t effective = (uint16_t)(((uint32_t)instance->noise_level_q15
            * (uint32_t)instance->voice.velocity_q15) >> 15);
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const int32_t sample = brick6_stack_runtime_render_noise_sample(instance);
            g_stack_acc_scratch[i] += (sample * (int32_t)effective) >> 15;
        }
        return;
    }
    const uint32_t ramp_frames = (frames > STACK_SQUARE_COMPAT_BLOCK_SIZE)
        ? STACK_SQUARE_COMPAT_BLOCK_SIZE : frames;
    const int32_t start = instance->noise_level_current_q15;
    brick6_stack_exact_ramp_t ramp;
    brick6_stack_exact_ramp_init(
        &ramp, (int32_t)instance->noise_level_q15 - start, ramp_frames);
    for (uint32_t i = 0U; i < ramp_frames; ++i)
    {
        const uint16_t level = (uint16_t)(start
            + (int32_t)brick6_stack_exact_ramp_next(&ramp));
        const uint16_t effective = (uint16_t)(((uint32_t)level
            * (uint32_t)instance->voice.velocity_q15) >> 15);
        const int32_t sample = brick6_stack_runtime_render_noise_sample(instance);
        g_stack_acc_scratch[i] += (sample * (int32_t)effective) >> 15;
    }
    instance->noise_level_current_q15 = instance->noise_level_q15;
    const uint16_t effective = (uint16_t)(((uint32_t)instance->noise_level_q15
        * (uint32_t)instance->voice.velocity_q15) >> 15);
    for (uint32_t i = ramp_frames; i < frames; ++i)
    {
        const int32_t sample = brick6_stack_runtime_render_noise_sample(instance);
        g_stack_acc_scratch[i] += (sample * (int32_t)effective) >> 15;
    }
}

void brick6_stack_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_STACK_VOICE_INSTANCE_COUNT; ++instance)
    {
        brick6_stack_runtime_init_instance(
            brick6_stack_runtime_get_instance_mut(instance));
    }
}

void brick6_stack_runtime_reset_instance(uint8_t instance_id)
{
    brick6_stack_runtime_init_instance(brick6_stack_runtime_get_instance_mut(instance_id));
}

void brick6_stack_runtime_set_slot_level(uint8_t instance_id, uint8_t slot, float level)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    const uint16_t next = brick6_stack_float_to_q15(level);
    if (instance->slots[slot].level_q15 == next) return;
    instance->slots[slot].level_q15 = next;
    instance->slots[slot].level = (uint8_t)((instance->slots[slot].level_q15 * 127U) / 32767U);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_continuous(instance, (uint8_t)(STACK_CONT_LEVEL_BASE + slot));
}

void brick6_stack_runtime_set_slot_model(uint8_t instance_id, uint8_t slot, brick6_stack_model_t model)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }

    model = brick6_stack_runtime_normalize_model(model);
    const brick6_stack_model_desc_t *const desc = brick6_stack_runtime_model_desc(model);
    stack_osc_slot_t *const osc = &instance->slots[slot];
    if (osc->model == (uint8_t)model)
    {
        return;
    }

    osc->model = (uint8_t)model;
    osc->family = desc->family;
    osc->kernel_id = desc->kernel_id;
    osc->renderer_id = desc->renderer_id;
    osc->phase = 0U;
    osc->phase2 = 0x55555555UL;
    osc->phase3 = 0xAAAAAAAAUL;
    brick6_stack_runtime_update_slot_pitch(instance, slot);
    brick6_stack_runtime_prepare_slot(osc);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_config(instance);
}

void brick6_stack_runtime_set_slot_tune(uint8_t instance_id, uint8_t slot, float semitones)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    const int16_t next = brick6_stack_tune_to_cents(semitones);
    if (instance->slots[slot].tune_cents == next) return;
    instance->slots[slot].tune_cents = next;
    brick6_stack_runtime_update_slot_pitch(instance, slot);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_continuous(instance, (uint8_t)(STACK_CONT_TUNE_BASE + slot));
}

void brick6_stack_runtime_set_slot_timbre(uint8_t instance_id, uint8_t slot, float timbre)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    const uint16_t next = brick6_stack_float_to_q15(timbre);
    if (instance->slots[slot].timbre_q15 == next) return;
    instance->slots[slot].timbre_q15 = next;
    instance->slots[slot].timbre = (uint8_t)((instance->slots[slot].timbre_q15 * 127U) / 32767U);
    brick6_stack_runtime_prepare_slot(&instance->slots[slot]);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_continuous(instance, (uint8_t)(STACK_CONT_TIMBRE_BASE + slot));
}

void brick6_stack_runtime_set_slot_color(uint8_t instance_id, uint8_t slot, float color)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    const uint16_t next = brick6_stack_float_to_q15(color);
    if (instance->slots[slot].color_q15 == next) return;
    instance->slots[slot].color_q15 = next;
    instance->slots[slot].color = (uint8_t)((instance->slots[slot].color_q15 * 127U) / 32767U);
    brick6_stack_runtime_prepare_slot(&instance->slots[slot]);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_continuous(instance, (uint8_t)(STACK_CONT_COLOR_BASE + slot));
}

void brick6_stack_runtime_set_noise_level(uint8_t instance_id, float level)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    const uint16_t next = brick6_stack_float_to_q15(level);
    if (instance->noise_level_q15 == next) return;
    instance->noise_level_q15 = next;
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_continuous(instance, STACK_CONT_NOISE);
}

void brick6_stack_runtime_set_osc_detune(uint8_t instance_id, float detune)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    const uint16_t next = brick6_stack_float_to_q15(detune);
    if (instance->osc_detune_q15 == next) return;
    instance->osc_detune_q15 = next;
    if ((instance->osc_detune_q15 != 0U) && (brick6_stack_runtime_osc_detune_offsets_are_zero(instance) != 0U))
    {
        brick6_stack_runtime_generate_osc_detune_offsets(instance);
    }
    brick6_stack_runtime_update_all_pitches(instance);
    brick6_stack_runtime_refresh_masks(instance);
    brick6_stack_runtime_touch_config(instance);
}

void brick6_stack_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    if (instance->phase_reset == next) return;
    instance->phase_reset = next;
    brick6_stack_runtime_touch_config(instance);
}

uint8_t brick6_stack_runtime_model_count(void)
{
    return (uint8_t)BRICK6_STACK_MODEL_COUNT;
}

const char *brick6_stack_runtime_model_name(brick6_stack_model_t model)
{
    return brick6_stack_runtime_model_desc(model)->name;
}

brick6_stack_family_t brick6_stack_runtime_model_family(brick6_stack_model_t model)
{
    return (brick6_stack_family_t)brick6_stack_runtime_model_desc(model)->family;
}

brick6_stack_kernel_id_t brick6_stack_runtime_model_kernel(brick6_stack_model_t model)
{
    return (brick6_stack_kernel_id_t)brick6_stack_runtime_model_desc(model)->kernel_id;
}

void brick6_stack_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }

    instance->voice.active_note = note;
    instance->voice.has_active_note = 1U;
    instance->voice.gate = 1U;
    instance->voice.trigger = 1U;
    instance->release_source_active = 0U;
    instance->voice.velocity_q15 = (uint16_t)(((uint32_t)velocity * 32767U) / 127U);
    if (instance->osc_detune_q15 != 0U)
    {
        brick6_stack_runtime_generate_osc_detune_offsets(instance);
    }
    if (instance->phase_reset != 0U)
    {
        for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
        {
            instance->slots[slot].phase = (uint32_t)slot * 0x55555555UL;
            instance->slots[slot].phase2 = 0x55555555UL + ((uint32_t)slot * 0x11111111UL);
            instance->slots[slot].phase3 = 0xAAAAAAAAUL - ((uint32_t)slot * 0x11111111UL);
        }
    }
    brick6_stack_runtime_update_all_pitches(instance);
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        instance->slots[slot].phase_inc_current = instance->slots[slot].phase_inc;
    }
    brick6_stack_runtime_refresh_masks(instance);
}

void brick6_stack_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    if ((instance->voice.has_active_note != 0U) && (instance->voice.active_note == note))
    {
        instance->voice.gate = 0U;
        instance->voice.has_active_note = 0U;
        instance->release_source_active = 1U;
    }
}

void brick6_stack_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->voice.gate = 0U;
    instance->voice.has_active_note = 0U;
    instance->voice.trigger = 0U;
    instance->release_source_active = 0U;
}

void brick6_stack_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

void brick6_stack_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance)
{
    const brick6_stack_runtime_instance_t *const src =
        brick6_stack_runtime_get_instance(track_instance);
    brick6_stack_runtime_instance_t *const dst =
        brick6_stack_runtime_get_instance_mut(voice_instance);
    if ((src == NULL) || (dst == NULL) || (src == dst))
    {
        return;
    }
    if ((dst->synced_config_version == src->config_version)
            && (dst->continuous_epoch == src->continuous_epoch)) return;
    const uint8_t full = (dst->synced_config_version != src->config_version) ? 1U : 0U;
    if ((full != 0U) || (dst->continuous_version[STACK_CONT_NOISE]
            != src->continuous_version[STACK_CONT_NOISE]))
    {
        dst->noise_level_q15 = src->noise_level_q15;
        dst->continuous_version[STACK_CONT_NOISE] = src->continuous_version[STACK_CONT_NOISE];
    }
    const uint8_t detune_changed = (uint8_t)((full != 0U)
        && (dst->osc_detune_q15 != src->osc_detune_q15));
    if (full != 0U)
    {
        dst->osc_detune_q15 = src->osc_detune_q15;
        dst->phase_reset = src->phase_reset;
    }
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        stack_osc_slot_t *const out = &dst->slots[slot];
        const stack_osc_slot_t *const in = &src->slots[slot];
        if (full != 0U)
        {
            out->model = in->model;
            out->family = in->family;
            out->kernel_id = in->kernel_id;
            out->renderer_id = in->renderer_id;
        }
        const uint8_t level_param = (uint8_t)(STACK_CONT_LEVEL_BASE + slot);
        const uint8_t tune_param = (uint8_t)(STACK_CONT_TUNE_BASE + slot);
        const uint8_t timbre_param = (uint8_t)(STACK_CONT_TIMBRE_BASE + slot);
        const uint8_t color_param = (uint8_t)(STACK_CONT_COLOR_BASE + slot);
        if ((full != 0U) || (dst->continuous_version[level_param]
                != src->continuous_version[level_param]))
        {
            out->level_q15 = in->level_q15;
            dst->continuous_version[level_param] = src->continuous_version[level_param];
        }
        const uint8_t pitch_changed = (uint8_t)((detune_changed != 0U)
            || (full != 0U)
            || (dst->continuous_version[tune_param] != src->continuous_version[tune_param]));
        if (pitch_changed != 0U)
        {
            out->tune_cents = in->tune_cents;
            dst->continuous_version[tune_param] = src->continuous_version[tune_param];
        }
        if ((full != 0U) || (dst->continuous_version[timbre_param]
                != src->continuous_version[timbre_param]))
        {
            out->timbre_q15 = in->timbre_q15;
            dst->continuous_version[timbre_param] = src->continuous_version[timbre_param];
        }
        if ((full != 0U) || (dst->continuous_version[color_param]
                != src->continuous_version[color_param]))
        {
            out->color_q15 = in->color_q15;
            dst->continuous_version[color_param] = src->continuous_version[color_param];
        }
        if (pitch_changed != 0U)
        {
            brick6_stack_runtime_update_slot_pitch(dst, slot);
        }
        brick6_stack_runtime_prepare_slot(out);
    }
    if (full != 0U) dst->synced_config_version = src->config_version;
    dst->continuous_epoch = src->continuous_epoch;
    brick6_stack_runtime_refresh_masks(dst);
}

ITCM_TEXT uint8_t brick6_stack_runtime_render_instance(uint8_t instance_id,
                                             float *out_mono,
                                             uint32_t frames,
                                             uint8_t downstream_source_required)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (out_mono == NULL)
    {
        return 0U;
    }
    if (instance == NULL)
    {
        memset(out_mono, 0, frames * sizeof(float));
        return 0U;
    }

    if ((instance->release_source_active != 0U)
            && (downstream_source_required == 0U))
    {
        instance->release_source_active = 0U;
    }
    const uint8_t source_active =
        (uint8_t)((instance->voice.gate != 0U)
               || (instance->release_source_active != 0U));
    if (source_active == 0U)
    {
        uint32_t remaining = frames;
        while (remaining > 0U)
        {
            const uint8_t chunk = (remaining > UINT8_MAX)
                ? UINT8_MAX
                : (uint8_t)remaining;
            brick6_stack_runtime_advance_free_running(instance, chunk);
            remaining -= chunk;
        }
        return 0U;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }
    memset(g_stack_acc_scratch, 0, frames * sizeof(*g_stack_acc_scratch));
    uint8_t active = (uint8_t)(instance->active_source_mask
        & (uint8_t)((1U << BRICK6_STACK_SLOT_COUNT) - 1U));
    while (active != 0U)
    {
        const uint8_t slot = (uint8_t)__builtin_ctz((unsigned int)active);
        active &= (uint8_t)(active - 1U);
        brick6_stack_runtime_render_slot(instance, slot, frames);
    }
    if ((instance->active_source_mask & STACK_SOURCE_NOISE_BIT) != 0U)
    {
        brick6_stack_runtime_render_noise(instance, frames);
    }
    if (instance->ramp_mask != 0U)
    {
        brick6_stack_runtime_refresh_masks(instance);
    }
    for (uint32_t i = 0U; i < frames; ++i)
    {
        out_mono[i] = (float)g_stack_acc_scratch[i]
            * ((1.0f / 32768.0f) * STACK_OUTPUT_TRIM);
    }
    return 1U;
}

const brick6_stack_runtime_voice_t *brick6_stack_runtime_get_voice(uint8_t instance_id)
{
    const brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}
