/*
 * BRICK runtime integration for the Synthstrom Deluge basic oscillator port.
 * Deluge-derived DSP and tables are GPL-3.0; see
 * LICENSES/DelugeFirmware-GPL-3.0.txt.
 */
#include "Core/brick6_deluge_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Audio/deluge_oscillator.h"
#include "Audio/audio_float.h"
#include "Storage/memory_layout.h"

#define DELUGE_DEFAULT_NOTE       60U
#define DELUGE_BASE_C4_INC        23409859UL
#define DELUGE_OUTPUT_SCALE       (1.0f / 2147483648.0f)
#define DELUGE_OUTPUT_TRIM        0.45f
#define BRICK6_DELUGE_OUTPUT_GAIN  0.44157045f

typedef struct
{
    uint8_t initialized;
    uint8_t gate;
    uint8_t active_note;
    uint8_t retrig;
    uint8_t pitch_dirty;
    uint8_t width_is_manual;
    uint8_t model;
    uint8_t oscillator_type;
    float velocity;
    float level_target;
    float level_current;
    float tune_semitones;
    float fine_cents;
    float width_value;
    float phase_degrees;
    uint32_t phase;
    uint32_t phase_increment;
    uint32_t phase_increment_current;
    uint32_t native_pulse_width;
    int32_t effective_note_cents;
} brick6_deluge_runtime_instance_t;

static AUDIO_HOT brick6_deluge_runtime_instance_t
    g_deluge_instances[BRICK6_DELUGE_MAX_INSTANCES];
static SEQ_STATE_D2 brick6_deluge_runtime_instance_t
    g_deluge_poly_instances[BRICK6_DELUGE_VOICE_INSTANCE_COUNT - BRICK6_DELUGE_MAX_INSTANCES];
static AUDIO_HOT int32_t g_deluge_render_q31[AUDIO_BLOCK_SIZE];

static const uint16_t k_semitone_ratio_q15[12] = {
    32768U, 34716U, 36781U, 38968U, 41285U, 43740U,
    46341U, 49097U, 52016U, 55109U, 58386U, 61858U
};

static uint8_t instance_valid(uint8_t instance_id)
{
    return (instance_id < (uint8_t)BRICK6_DELUGE_VOICE_INSTANCE_COUNT) ? 1U : 0U;
}

static brick6_deluge_runtime_instance_t *instance_mut(uint8_t instance_id)
{
    if (instance_valid(instance_id) == 0U)
    {
        return NULL;
    }
    return (instance_id < BRICK6_DELUGE_MAX_INSTANCES)
        ? &g_deluge_instances[instance_id]
        : &g_deluge_poly_instances[instance_id - BRICK6_DELUGE_MAX_INSTANCES];
}

static float clampf(float value, float lo, float hi)
{
    if (value < lo) { return lo; }
    if (value > hi) { return hi; }
    return value;
}

static int32_t round_to_i32(float value)
{
    return (value >= 0.0f) ? (int32_t)(value + 0.5f) : (int32_t)(value - 0.5f);
}

/*
 * BRICK 48 kHz Q32 phase-increment generator. This mirrors the bounded
 * semitone/cents interpolation used by the Stack runtime and performs no
 * transcendental calculation in note-on, p-lock, modulation, or audio paths.
 */
static uint32_t note_cents_to_phase_increment(int32_t note_cents)
{
    int32_t delta_cents = note_cents - ((int32_t)DELUGE_DEFAULT_NOTE * 100L);
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
    const uint32_t ratio0 = k_semitone_ratio_q15[semitone];
    const uint32_t ratio1 = (semitone < 11U)
        ? k_semitone_ratio_q15[semitone + 1U]
        : ((uint32_t)k_semitone_ratio_q15[0] << 1);
    const uint32_t ratio = ratio0
        + (uint32_t)((((int32_t)ratio1 - (int32_t)ratio0) * (int32_t)cents + 50L) / 100L);
    uint64_t increment = ((uint64_t)DELUGE_BASE_C4_INC * (uint64_t)ratio) >> 15;

    if (octave > 0)
    {
        for (int8_t i = 0; i < octave; ++i)
        {
            increment <<= 1;
            if (increment > UINT32_C(0x7FFFFFFF))
            {
                increment = UINT32_C(0x7FFFFFFF);
                break;
            }
        }
    }
    else
    {
        for (int8_t i = 0; i > octave; --i)
        {
            increment >>= 1;
        }
    }

    if (increment == 0U) { increment = 1U; }
    if (increment > UINT32_C(0x7FFFFFFF)) { increment = UINT32_C(0x7FFFFFFF); }
    return (uint32_t)increment;
}

static void update_phase_increment_if_dirty(brick6_deluge_runtime_instance_t *instance)
{
    if (instance->pitch_dirty == 0U)
    {
        return;
    }

    const int32_t tune_cents = round_to_i32(instance->tune_semitones * 100.0f)
        + round_to_i32(instance->fine_cents);
    const int32_t effective_note_cents =
        ((int32_t)instance->active_note * 100L) + tune_cents;
    if (instance->effective_note_cents != effective_note_cents)
    {
        instance->effective_note_cents = effective_note_cents;
        instance->phase_increment = note_cents_to_phase_increment(effective_note_cents);
    }
    instance->pitch_dirty = 0U;
}

static uint32_t start_phase_q32(float degrees)
{
    const double normalized = (double)clampf(degrees, 0.0f, 360.0f) * (1.0 / 360.0);
    const double scaled = normalized * 4294967296.0;
    if (scaled >= 4294967295.5)
    {
        return 0U;
    }
    return (uint32_t)(scaled + 0.5);
}

/*
 * Deluge's manual pulse-width menu is half precision: menu 0..50 maps
 * to native 0..INT32_MAX through computeFinalValueForHalfPrecisionMenuItem().
 * Preserve those exact integer results for the manual SKEW grid. Matrix
 * values below zero remain continuous down to INT32_MIN.
 */
static int32_t native_skew_q31(float value, uint8_t manual)
{
    const float clamped = clampf(value, -1.0f, 1.0f);
    if (clamped < 0.0f)
    {
        if (clamped <= -1.0f)
        {
            return INT32_MIN;
        }
        return (int32_t)((double)clamped * 2147483648.0 - 0.5);
    }

    if (manual != 0U)
    {
        const uint32_t menu_value = (uint32_t)(clamped * 50.0f + 0.5f);
        if (menu_value >= 50U)
        {
            return INT32_MAX;
        }
        return (int32_t)((menu_value * (UINT32_C(2147483648) / 25U)) >> 1);
    }
    return (int32_t)((double)clamped * 2147483647.0 + 0.5);
}

static int32_t native_square_width_q31(float width)
{
    const double bipolar = ((double)clampf(width, 0.0f, 1.0f) * 2.0) - 1.0;
    if (bipolar <= -1.0)
    {
        return INT32_MIN;
    }
    if (bipolar >= 1.0)
    {
        return INT32_MAX;
    }
    return (int32_t)((bipolar >= 0.0)
        ? (bipolar * 2147483647.0 + 0.5)
        : (bipolar * 2147483648.0 - 0.5));
}

static void update_native_pulse_width(brick6_deluge_runtime_instance_t *instance)
{
    const int32_t native = (instance->model == (uint8_t)BRICK6_DELUGE_MODEL_SQUARE)
        ? native_square_width_q31(instance->width_value)
        : native_skew_q31(instance->width_value, instance->width_is_manual);
    instance->native_pulse_width = (uint32_t)native;
}

static deluge_osc_type_t oscillator_type_from_model(brick6_deluge_model_t model)
{
    switch (model)
    {
        case BRICK6_DELUGE_MODEL_SINE: return DELUGE_OSC_SINE;
        case BRICK6_DELUGE_MODEL_TRI: return DELUGE_OSC_TRIANGLE;
        case BRICK6_DELUGE_MODEL_ANALOG_SQUARE: return DELUGE_OSC_ANALOG_SQUARE;
        case BRICK6_DELUGE_MODEL_SAW: return DELUGE_OSC_SAW;
        case BRICK6_DELUGE_MODEL_ANALOG_SAW: return DELUGE_OSC_ANALOG_SAW;
        case BRICK6_DELUGE_MODEL_SQUARE:
        default:
            return DELUGE_OSC_SQUARE;
    }
}

void brick6_deluge_runtime_reset_instance(uint8_t instance_id)
{
    if (instance_valid(instance_id) == 0U)
    {
        return;
    }

    brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
    memset(instance, 0, sizeof(*instance));
    instance->active_note = DELUGE_DEFAULT_NOTE;
    instance->model = (uint8_t)BRICK6_DELUGE_MODEL_SQUARE;
    instance->oscillator_type = (uint8_t)DELUGE_OSC_SQUARE;
    instance->width_value = 0.5f;
    instance->width_is_manual = 1U;
    instance->level_target = 1.0f;
    instance->level_current = 1.0f;
    instance->effective_note_cents = (int32_t)DELUGE_DEFAULT_NOTE * 100L;
    instance->phase_increment =
        note_cents_to_phase_increment(instance->effective_note_cents);
    instance->phase_increment_current = instance->phase_increment;
    update_native_pulse_width(instance);
    instance->initialized = 1U;
}

void brick6_deluge_runtime_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)BRICK6_DELUGE_VOICE_INSTANCE_COUNT; ++i)
    {
        brick6_deluge_runtime_reset_instance(i);
    }
}

void brick6_deluge_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance)
{
    brick6_deluge_runtime_instance_t *const src = instance_mut(track_instance);
    brick6_deluge_runtime_instance_t *const dst = instance_mut(voice_instance);
    if ((src == NULL) || (dst == NULL) || (src == dst))
    {
        return;
    }
    dst->retrig = src->retrig;
    dst->model = src->model;
    dst->oscillator_type = src->oscillator_type;
    dst->level_target = src->level_target;
    dst->tune_semitones = src->tune_semitones;
    dst->fine_cents = src->fine_cents;
    dst->width_value = src->width_value;
    dst->width_is_manual = src->width_is_manual;
    dst->phase_degrees = src->phase_degrees;
    dst->pitch_dirty = 1U;
    update_native_pulse_width(dst);
}

void brick6_deluge_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    if (instance_valid(instance_id) == 0U)
    {
        return;
    }

    brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
    if (instance->initialized == 0U)
    {
        brick6_deluge_runtime_reset_instance(instance_id);
    }
    if (instance->active_note != note)
    {
        instance->active_note = note;
        instance->pitch_dirty = 1U;
    }
    update_phase_increment_if_dirty(instance);
    instance->phase_increment_current = instance->phase_increment;
    instance->velocity = (float)velocity * (1.0f / 127.0f);
    instance->gate = (velocity != 0U) ? 1U : 0U;
    if ((instance->gate != 0U) && (instance->retrig != 0U))
    {
        instance->phase = start_phase_q32(instance->phase_degrees);
    }
}

void brick6_deluge_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    if ((instance_valid(instance_id) != 0U)
            && (instance_mut(instance_id)->active_note == note))
    {
        instance_mut(instance_id)->gate = 0U;
    }
}

void brick6_deluge_runtime_all_notes_off(uint8_t instance_id)
{
    if (instance_valid(instance_id) != 0U)
    {
        instance_mut(instance_id)->gate = 0U;
        instance_mut(instance_id)->velocity = 0.0f;
    }
}

void brick6_deluge_runtime_set_model(uint8_t instance_id, brick6_deluge_model_t model)
{
    if (instance_valid(instance_id) == 0U)
    {
        return;
    }
    if (model >= BRICK6_DELUGE_MODEL_COUNT)
    {
        model = BRICK6_DELUGE_MODEL_SQUARE;
    }
    brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
    if (instance->model != (uint8_t)model)
    {
        const uint8_t was_square =
            (instance->model == (uint8_t)BRICK6_DELUGE_MODEL_SQUARE) ? 1U : 0U;
        const uint8_t is_square =
            (model == BRICK6_DELUGE_MODEL_SQUARE) ? 1U : 0U;
        if ((was_square != 0U) && (is_square == 0U))
        {
            const float bipolar = (instance->width_value * 2.0f) - 1.0f;
            instance->width_value = (bipolar < 0.0f) ? -bipolar : bipolar;
        }
        else if ((was_square == 0U) && (is_square != 0U))
        {
            instance->width_value = 0.5f + (instance->width_value * 0.5f);
        }
        instance->model = (uint8_t)model;
        instance->oscillator_type = (uint8_t)oscillator_type_from_model(model);
        update_native_pulse_width(instance);
    }
}

void brick6_deluge_runtime_set_level(uint8_t instance_id, float level)
{
    if (instance_valid(instance_id) != 0U)
    {
        brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
        instance->level_target = clampf(level, 0.0f, 1.0f);
        if (instance->velocity <= 0.0f)
        {
            instance->level_current = instance->level_target;
        }
    }
}

void brick6_deluge_runtime_set_tune(uint8_t instance_id, float semitones)
{
    if (instance_valid(instance_id) != 0U)
    {
        brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
        const float clamped = clampf(semitones, -48.0f, 48.0f);
        if (instance->tune_semitones != clamped)
        {
            instance->tune_semitones = clamped;
            instance->pitch_dirty = 1U;
        }
    }
}

void brick6_deluge_runtime_set_fine(uint8_t instance_id, float cents)
{
    if (instance_valid(instance_id) != 0U)
    {
        brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
        const float clamped = clampf(cents, -100.0f, 100.0f);
        if (instance->fine_cents != clamped)
        {
            instance->fine_cents = clamped;
            instance->pitch_dirty = 1U;
        }
    }
}

void brick6_deluge_runtime_set_width(uint8_t instance_id, float value)
{
    if (instance_valid(instance_id) != 0U)
    {
        brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
        const float clamped = (instance->model == (uint8_t)BRICK6_DELUGE_MODEL_SQUARE)
            ? clampf(value, 0.0f, 1.0f)
            : clampf(value, -1.0f, 1.0f);
        if (instance->width_value != clamped)
        {
            instance->width_value = clamped;
        }
        instance->width_is_manual = 1U;
        update_native_pulse_width(instance);
    }
}

void brick6_deluge_runtime_set_width_modulated(uint8_t instance_id, float value)
{
    if (instance_valid(instance_id) != 0U)
    {
        brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
        instance->width_value = (instance->model == (uint8_t)BRICK6_DELUGE_MODEL_SQUARE)
            ? clampf(value, 0.0f, 1.0f)
            : clampf(value, -1.0f, 1.0f);
        instance->width_is_manual = 0U;
        update_native_pulse_width(instance);
    }
}

void brick6_deluge_runtime_set_phase(uint8_t instance_id, float degrees)
{
    if (instance_valid(instance_id) != 0U)
    {
        instance_mut(instance_id)->phase_degrees = clampf(degrees, 0.0f, 360.0f);
    }
}

void brick6_deluge_runtime_set_retrig(uint8_t instance_id, uint8_t enabled)
{
    if (instance_valid(instance_id) != 0U)
    {
        instance_mut(instance_id)->retrig = (enabled != 0U) ? 1U : 0U;
    }
}

uint8_t brick6_deluge_runtime_prepare_block(uint8_t instance_id,
                                            uint32_t frames,
                                            uint8_t downstream_source_required)
{
    if ((frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (instance_valid(instance_id) == 0U))
    {
        return 0U;
    }

    brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
    if (instance->initialized == 0U)
    {
        return 0U;
    }

    update_phase_increment_if_dirty(instance);

    if (instance->velocity <= 0.0f)
    {
        if (instance->retrig == 0U)
        {
            instance->phase += instance->phase_increment * frames;
        }
        instance->phase_increment_current = instance->phase_increment;
        return 0U;
    }

    if ((instance->level_target <= 0.0f) && (instance->level_current <= 0.0f))
    {
        instance->phase += instance->phase_increment * frames;
        instance->phase_increment_current = instance->phase_increment;
        return 0U;
    }

    if ((instance->gate == 0U) && (downstream_source_required == 0U))
    {
        instance->phase += instance->phase_increment * frames;
        instance->level_current = instance->level_target;
        instance->phase_increment_current = instance->phase_increment;
        return 0U;
    }

    return 1U;
}

uint8_t brick6_deluge_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    if ((out_mono == NULL) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (instance_valid(instance_id) == 0U))
    {
        return 0U;
    }

    brick6_deluge_runtime_instance_t *const instance = instance_mut(instance_id);
    if (instance->initialized == 0U)
    {
        return 0U;
    }

    update_phase_increment_if_dirty(instance);

    if (instance->velocity <= 0.0f)
    {
        if (instance->retrig == 0U)
        {
            instance->phase += instance->phase_increment * frames;
        }
        instance->phase_increment_current = instance->phase_increment;
        return 0U;
    }

    if ((instance->level_target <= 0.0f) && (instance->level_current <= 0.0f))
    {
        instance->phase += instance->phase_increment * frames;
        instance->phase_increment_current = instance->phase_increment;
        return 0U;
    }

    const uint32_t increment_start = instance->phase_increment_current;
    for (uint32_t offset = 0U; offset < frames; offset += 8U)
    {
        uint32_t chunk = frames - offset;
        if (chunk > 8U)
        {
            chunk = 8U;
        }
        const uint32_t progress = offset + chunk;
        const int64_t delta =
            (int64_t)instance->phase_increment - (int64_t)increment_start;
        const uint32_t increment = (uint32_t)((int64_t)increment_start
            + ((delta * (int64_t)progress) / (int64_t)frames));
        deluge_oscillator_render((deluge_osc_type_t)instance->oscillator_type,
                                 &g_deluge_render_q31[offset],
                                 chunk,
                                 increment,
                                 instance->native_pulse_width,
                                 &instance->phase);
    }
    instance->phase_increment_current = instance->phase_increment;

    float level = instance->level_current;
    const float velocity_level = instance->velocity * DELUGE_OUTPUT_TRIM;
    if (level == instance->level_target)
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const float rendered = (float)g_deluge_render_q31[i] * DELUGE_OUTPUT_SCALE
                * level * velocity_level;
            out_mono[i] = rendered * BRICK6_DELUGE_OUTPUT_GAIN;
        }
    }
    else
    {
        const float level_increment = (instance->level_target - level) / (float)frames;
        for (uint32_t i = 0U; i < frames; ++i)
        {
            level += level_increment;
            const float rendered = (float)g_deluge_render_q31[i] * DELUGE_OUTPUT_SCALE
                * level * velocity_level;
            out_mono[i] = rendered * BRICK6_DELUGE_OUTPUT_GAIN;
        }
    }
    instance->level_current = instance->level_target;
    return 1U;
}
