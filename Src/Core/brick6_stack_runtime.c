/**
 * @file brick6_stack_runtime.c
 * @brief Minimal independent Stack runtime, without MacroOscillator ownership.
 */

#include "Core/brick6_stack_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Core/brick6_stack_waveform.h"
#include "Audio/deluge_oscillator.h"
#include "Audio/audio_track_diag.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define STACK_DEFAULT_RNG_SEED 0x6D2B79F5UL
#define STACK_DEFAULT_NOTE 60U
#define STACK_BASE_C4_INC 23428135UL
#define STACK_COMMAND_QUEUE_CAP 256U
#define STACK_OSC_DETUNE_MAX_SHIFT 8U
#define STACK_LEVEL_ENERGY_ONE_Q30 1073676289UL
#define STACK_LEVEL_ENERGY_TWO_Q30 2147352578UL
#define STACK_LEVEL_ENERGY_THREE_Q30 3221028867UL
#define STACK_OUTPUT_GAIN_ONE_Q15 32767U
#define STACK_OUTPUT_GAIN_TWO_Q15 23170U
#define STACK_OUTPUT_GAIN_THREE_Q15 18919U
#define STACK_SOFT_CLIP_LINEAR_Q15 28672L
#define STACK_SOFT_CLIP_RANGE_Q15 (32767L - STACK_SOFT_CLIP_LINEAR_Q15)

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

typedef enum
{
    STACK_COMMAND_NOTE_ON = 0,
    STACK_COMMAND_NOTE_OFF,
    STACK_COMMAND_ALL_NOTES_OFF,
    STACK_COMMAND_RESET,
    STACK_COMMAND_SET_SLOT_MODEL,
    STACK_COMMAND_SET_SLOT_LEVEL,
    STACK_COMMAND_SET_SLOT_TUNE,
    STACK_COMMAND_SET_SLOT_TIMBRE,
    STACK_COMMAND_SET_SLOT_COLOR,
    STACK_COMMAND_SET_NOISE_LEVEL,
    STACK_COMMAND_SET_OSC_DETUNE,
    STACK_COMMAND_SET_PHASE_RESET
} brick6_stack_runtime_command_type_t;

typedef struct
{
    uint8_t type;
    uint8_t instance_id;
    uint8_t note;
    uint8_t velocity;
    int16_t value_i16;
    uint16_t value_u16;
} brick6_stack_runtime_command_t;

typedef struct
{
    const char *name;
    uint8_t family;
    uint8_t kernel_id;
    uint8_t renderer_id;
} brick6_stack_model_desc_t;

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
    uint8_t pending_offset;
    uint8_t pending_count;
    uint8_t phase_reset;
    uint8_t reserved0;
    uint32_t config_version;
    uint32_t synced_config_version;
    int16_t pending_mono[BRICK6_STACK_RENDER_BLOCK_SIZE];
} brick6_stack_runtime_instance_t;

_Static_assert(sizeof(stack_osc_slot_t) <= 192U, "stack_osc_slot_t RAM budget exceeded");
_Static_assert(sizeof(brick6_stack_runtime_instance_t) <= 768U, "brick6_stack_runtime_instance_t RAM budget exceeded");

AUDIO_HOT static brick6_stack_runtime_instance_t g_stack_runtime[BRICK6_STACK_MAX_INSTANCES];
enum { STACK_POLY_D2_COUNT = BRICK6_STACK_VOICE_INSTANCE_COUNT - BRICK6_STACK_MAX_INSTANCES };
AUDIO_HOT static brick6_stack_runtime_instance_t g_stack_poly_runtime_d2[STACK_POLY_D2_COUNT];
AUDIO_HOT static volatile uint8_t g_stack_command_head;
AUDIO_HOT static volatile uint8_t g_stack_command_tail;
AUDIO_HOT static brick6_stack_runtime_command_t g_stack_command_queue[STACK_COMMAND_QUEUE_CAP];
AUDIO_HOT static volatile uint8_t g_stack_note_cancel_pending[BRICK6_STACK_MAX_INSTANCES];

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

static int16_t brick6_stack_sat16(int32_t value)
{
    if (value > 32767)
    {
        return 32767;
    }
    if (value < -32768)
    {
        return -32768;
    }
    return (int16_t)value;
}

static int16_t brick6_stack_soft_clip_q15(int32_t value, uint8_t *out_activated)
{
    if ((value <= STACK_SOFT_CLIP_LINEAR_Q15) && (value >= -STACK_SOFT_CLIP_LINEAR_Q15))
    {
        return (int16_t)value;
    }

    const uint8_t negative = (value < 0) ? 1U : 0U;
    uint32_t magnitude = negative ? (uint32_t)(-value) : (uint32_t)value;
    if (magnitude <= (uint32_t)STACK_SOFT_CLIP_LINEAR_Q15)
    {
        return (int16_t)value;
    }
    if (out_activated != NULL)
    {
        *out_activated = 1U;
    }

    const uint32_t excess = magnitude - (uint32_t)STACK_SOFT_CLIP_LINEAR_Q15;
    magnitude = (uint32_t)STACK_SOFT_CLIP_LINEAR_Q15
            + (uint32_t)((excess * (uint32_t)STACK_SOFT_CLIP_RANGE_Q15)
                    / (excess + (uint32_t)STACK_SOFT_CLIP_RANGE_Q15));
    if (magnitude > 32767U)
    {
        magnitude = 32767U;
    }
    return negative ? (int16_t)-(int32_t)magnitude : (int16_t)magnitude;
}

static uint32_t brick6_stack_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void brick6_stack_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t brick6_stack_runtime_submit_command(uint8_t type,
                                                   uint8_t instance_id,
                                                   uint8_t note,
                                                   uint8_t velocity,
                                                   int16_t value_i16,
                                                   uint16_t value_u16)
{
    if (instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT)
    {
        return 0U;
    }

    const uint32_t primask = brick6_stack_enter_critical();
    const uint8_t head = g_stack_command_head;
    const uint8_t next = (uint8_t)((head + 1U) % STACK_COMMAND_QUEUE_CAP);
    if (next == g_stack_command_tail)
    {
        brick6_stack_exit_critical(primask);
        return 0U;
    }

    g_stack_command_queue[head].type = type;
    g_stack_command_queue[head].instance_id = instance_id;
    g_stack_command_queue[head].note = note;
    g_stack_command_queue[head].velocity = velocity;
    g_stack_command_queue[head].value_i16 = value_i16;
    g_stack_command_queue[head].value_u16 = value_u16;
    g_stack_command_head = next;
    brick6_stack_exit_critical(primask);
    return 1U;
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

static void brick6_stack_runtime_flush_pending(brick6_stack_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }
    instance->pending_offset = 0U;
    instance->pending_count = 0U;
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

    const uint32_t span = inc >> STACK_OSC_DETUNE_MAX_SHIFT;
    int32_t delta = (int32_t)(((uint64_t)span * (uint64_t)amount_q15) >> 15);
    delta = (delta * (int32_t)offset_q15) >> 15;
    if (delta < 0)
    {
        const uint32_t abs_delta = (uint32_t)(-delta);
        return (inc > abs_delta) ? (inc - abs_delta) : 1U;
    }
    return (inc < (0x7FFFFFFFUL - (uint32_t)delta)) ? (inc + (uint32_t)delta) : 0x7FFFFFFFUL;
}

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

    int32_t sum = 0;
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        const uint32_t r = brick6_stack_runtime_next_rng(instance);
        const int16_t offset = (int16_t)((int32_t)(r & 0xFFFFU) - 32768L);
        instance->osc_detune_offset_q15[slot] = offset;
        sum += offset;
    }

    const int16_t mean = (int16_t)(sum / (int32_t)BRICK6_STACK_SLOT_COUNT);
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        instance->osc_detune_offset_q15[slot] = brick6_stack_sat16((int32_t)instance->osc_detune_offset_q15[slot] - mean);
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

static void brick6_stack_render_waveform(stack_osc_slot_t *slot,
                                         int32_t *acc,
                                         uint8_t frames,
                                         uint16_t effective_level,
                                         int16_t (*waveform)(stack_osc_slot_t *slot))
{
    if ((slot == NULL) || (acc == NULL) || (waveform == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        const int16_t sample_q15 = waveform(slot);
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static int16_t brick6_stack_wave_shape(stack_osc_slot_t *slot)
{
    return brick6_stack_waveform_shape(slot->phase, slot->timbre_q15, slot->color_q15);
}

static uint32_t brick6_stack_deluge_pulse_width(uint16_t timbre_q15)
{
    const int32_t bipolar = ((int32_t)timbre_q15 * 2) - 32767;
    return (uint32_t)((int64_t)bipolar * 65536LL);
}

static void brick6_stack_runtime_render_deluge(stack_osc_slot_t *slot,
                                               int32_t *acc,
                                               uint8_t frames,
                                               uint16_t effective_level,
                                               deluge_osc_type_t type,
                                               uint8_t pulse_width_from_timbre)
{
    if ((slot == NULL) || (acc == NULL) || (frames == 0U))
    {
        return;
    }

    int32_t samples[BRICK6_STACK_RENDER_BLOCK_SIZE];
    deluge_oscillator_render(type,
                             samples,
                             frames,
                             slot->phase_inc,
                             (pulse_width_from_timbre != 0U)
                                 ? brick6_stack_deluge_pulse_width(slot->timbre_q15) : 0U,
                             &slot->phase);
    for (uint8_t i = 0U; i < frames; ++i)
    {
        const int16_t sample_q15 = brick6_stack_sat16(samples[i] >> 16);
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_deluge_sine(stack_osc_slot_t *slot,
                                                    int32_t *acc,
                                                    uint8_t frames,
                                                    uint16_t effective_level)
{
    brick6_stack_runtime_render_deluge(slot, acc, frames, effective_level, DELUGE_OSC_SINE, 0U);
}

static void brick6_stack_runtime_render_deluge_tri(stack_osc_slot_t *slot,
                                                   int32_t *acc,
                                                   uint8_t frames,
                                                   uint16_t effective_level)
{
    brick6_stack_runtime_render_deluge(slot, acc, frames, effective_level, DELUGE_OSC_TRIANGLE, 0U);
}

static void brick6_stack_runtime_render_deluge_square(stack_osc_slot_t *slot,
                                                      int32_t *acc,
                                                      uint8_t frames,
                                                      uint16_t effective_level)
{
    brick6_stack_runtime_render_deluge(slot, acc, frames, effective_level, DELUGE_OSC_ANALOG_SQUARE, 1U);
}

static void brick6_stack_runtime_render_deluge_saw(stack_osc_slot_t *slot,
                                                   int32_t *acc,
                                                   uint8_t frames,
                                                   uint16_t effective_level)
{
    brick6_stack_runtime_render_deluge(slot, acc, frames, effective_level, DELUGE_OSC_ANALOG_SAW, 0U);
}

static void brick6_stack_runtime_render_shape(stack_osc_slot_t *slot,
                                                 int32_t *acc,
                                                 uint8_t frames,
                                                 uint16_t effective_level)
{
    brick6_stack_render_waveform(slot, acc, frames, effective_level, brick6_stack_wave_shape);
}

static void brick6_stack_runtime_render_triple_saw(stack_osc_slot_t *slot,
                                                   int32_t *acc,
                                                   uint8_t frames,
                                                   uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
    const uint32_t inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, 1);
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += inc2;
        slot->phase3 += inc3;
        const int32_t mixed = brick6_stack_average3_q15(brick6_stack_phase_saw(slot->phase),
                                                        brick6_stack_phase_saw(slot->phase2),
                                                        brick6_stack_phase_saw(slot->phase3));
        acc[i] += (mixed * (int32_t)effective_level) >> 15;
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
            const uint32_t inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
            const uint32_t inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, 1);
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += inc2 * (uint32_t)frames;
            slot->phase3 += inc3 * (uint32_t)frames;
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

static void brick6_stack_runtime_render_silent(stack_osc_slot_t *slot,
                                               int32_t *acc,
                                               uint8_t frames,
                                               uint16_t effective_level)
{
    (void)slot;
    (void)acc;
    (void)frames;
    (void)effective_level;
}

typedef void (*brick6_stack_slot_renderer_t)(stack_osc_slot_t *slot,
                                             int32_t *acc,
                                             uint8_t frames,
                                             uint16_t effective_level);

static const brick6_stack_slot_renderer_t k_stack_renderers[STACK_RENDERER_COUNT] = {
    brick6_stack_runtime_render_deluge_sine,
    brick6_stack_runtime_render_deluge_tri,
    brick6_stack_runtime_render_deluge_square,
    brick6_stack_runtime_render_deluge_saw,
    brick6_stack_runtime_render_shape,
    brick6_stack_runtime_render_triple_saw,
    brick6_stack_runtime_render_silent,
};

static void brick6_stack_runtime_render_slot_chunk(stack_osc_slot_t *slot,
                                                   int32_t *acc,
                                                   uint8_t frames,
                                                   uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL) || (slot->renderer_id >= STACK_RENDERER_COUNT))
    {
        return;
    }

    k_stack_renderers[slot->renderer_id](slot, acc, frames, effective_level);
}

static uint16_t brick6_stack_runtime_energy_gain_q15(uint32_t energy_q30)
{
    if (energy_q30 <= STACK_LEVEL_ENERGY_ONE_Q30)
    {
        return STACK_OUTPUT_GAIN_ONE_Q15;
    }
    if (energy_q30 <= STACK_LEVEL_ENERGY_TWO_Q30)
    {
        const uint32_t position = energy_q30 - STACK_LEVEL_ENERGY_ONE_Q30;
        const uint32_t range = STACK_LEVEL_ENERGY_TWO_Q30 - STACK_LEVEL_ENERGY_ONE_Q30;
        const uint32_t drop = (uint32_t)(((uint64_t)(STACK_OUTPUT_GAIN_ONE_Q15 - STACK_OUTPUT_GAIN_TWO_Q15)
                * (uint64_t)position) / range);
        return (uint16_t)(STACK_OUTPUT_GAIN_ONE_Q15 - drop);
    }
    if (energy_q30 < STACK_LEVEL_ENERGY_THREE_Q30)
    {
        const uint32_t position = energy_q30 - STACK_LEVEL_ENERGY_TWO_Q30;
        const uint32_t range = STACK_LEVEL_ENERGY_THREE_Q30 - STACK_LEVEL_ENERGY_TWO_Q30;
        const uint32_t drop = (uint32_t)(((uint64_t)(STACK_OUTPUT_GAIN_TWO_Q15 - STACK_OUTPUT_GAIN_THREE_Q15)
                * (uint64_t)position) / range);
        return (uint16_t)(STACK_OUTPUT_GAIN_TWO_Q15 - drop);
    }
    return STACK_OUTPUT_GAIN_THREE_Q15;
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

static void brick6_stack_runtime_generate_pending(uint8_t instance_id,
                                                  brick6_stack_runtime_instance_t *instance,
                                                  uint8_t frames,
                                                  uint8_t source_active)
{
    int32_t acc[BRICK6_STACK_RENDER_BLOCK_SIZE];

    if ((instance == NULL) || (frames == 0U))
    {
        return;
    }
    if (frames > BRICK6_STACK_RENDER_BLOCK_SIZE)
    {
        frames = BRICK6_STACK_RENDER_BLOCK_SIZE;
    }

    memset(acc, 0, sizeof(acc));
    uint32_t level_energy_q30 = 0U;
    if (source_active != 0U)
    {
        for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
        {
            stack_osc_slot_t *const osc = &instance->slots[slot];
            if ((osc->level_q15 == 0U) && (osc->level_current_q15 == 0U))
            {
                continue;
            }

            const uint16_t effective_level = (uint16_t)(((uint32_t)osc->level_q15
                    * (uint32_t)instance->voice.velocity_q15) >> 15);
            level_energy_q30 += (uint32_t)effective_level * (uint32_t)effective_level;
            if ((osc->level_current_q15 == osc->level_q15)
                    && (osc->phase_inc_current == osc->phase_inc)
                    && (osc->timbre_current_q15 == osc->timbre_q15)
                    && (osc->color_current_q15 == osc->color_q15))
            {
                brick6_stack_runtime_render_slot_chunk(osc,
                                                       acc,
                                                       frames,
                                                       effective_level);
            }
            else if ((osc->phase_inc_current == osc->phase_inc)
                    && (osc->timbre_current_q15 == osc->timbre_q15)
                    && (osc->color_current_q15 == osc->color_q15))
            {
                int32_t slot_acc[BRICK6_STACK_RENDER_BLOCK_SIZE] = {0};
                const int32_t level_delta =
                    (int32_t)osc->level_q15 - (int32_t)osc->level_current_q15;
                brick6_stack_exact_ramp_t level_ramp;
                brick6_stack_exact_ramp_init(&level_ramp, level_delta, frames);
                brick6_stack_runtime_render_slot_chunk(
                    osc,
                    slot_acc,
                    frames,
                    instance->voice.velocity_q15);
                for (uint8_t i = 0U; i < frames; ++i)
                {
                    const uint16_t level_here = (uint16_t)(
                        (int32_t)osc->level_current_q15
                        + (int32_t)brick6_stack_exact_ramp_next(&level_ramp));
                    acc[i] += ((int32_t)slot_acc[i] * (int32_t)level_here) >> 15;
                }
                osc->level_current_q15 = osc->level_q15;
            }
            else
            {
                const int32_t level_delta =
                    (int32_t)osc->level_q15 - (int32_t)osc->level_current_q15;
                const int64_t pitch_delta =
                    (int64_t)osc->phase_inc - (int64_t)osc->phase_inc_current;
                const int32_t timbre_delta =
                    (int32_t)osc->timbre_q15 - (int32_t)osc->timbre_current_q15;
                const int32_t color_delta =
                    (int32_t)osc->color_q15 - (int32_t)osc->color_current_q15;
                const uint32_t pitch_start = osc->phase_inc_current;
                brick6_stack_exact_ramp_t level_ramp;
                brick6_stack_exact_ramp_t pitch_ramp;
                brick6_stack_exact_ramp_t timbre_ramp;
                brick6_stack_exact_ramp_t color_ramp;
                brick6_stack_exact_ramp_init(&level_ramp, level_delta, frames);
                brick6_stack_exact_ramp_init(&pitch_ramp, pitch_delta, frames);
                brick6_stack_exact_ramp_init(&timbre_ramp, timbre_delta, frames);
                brick6_stack_exact_ramp_init(&color_ramp, color_delta, frames);
                for (uint8_t i = 0U; i < frames; ++i)
                {
                    const uint16_t level_here = (uint16_t)(
                        (int32_t)osc->level_current_q15
                        + (int32_t)brick6_stack_exact_ramp_next(&level_ramp));
                    osc->phase_inc = (uint32_t)((int64_t)pitch_start
                        + brick6_stack_exact_ramp_next(&pitch_ramp));
                    osc->timbre_q15 = (uint16_t)((int32_t)osc->timbre_current_q15
                        + (int32_t)brick6_stack_exact_ramp_next(&timbre_ramp));
                    osc->color_q15 = (uint16_t)((int32_t)osc->color_current_q15
                        + (int32_t)brick6_stack_exact_ramp_next(&color_ramp));
                    const uint16_t effective_here = (uint16_t)(
                        ((uint32_t)level_here
                            * (uint32_t)instance->voice.velocity_q15) >> 15);
                    brick6_stack_runtime_render_slot_chunk(
                        osc, &acc[i], 1U, effective_here);
                }
                osc->phase_inc =
                    (uint32_t)((int64_t)pitch_start + pitch_delta);
                osc->level_current_q15 = osc->level_q15;
                osc->phase_inc_current = osc->phase_inc;
                osc->timbre_current_q15 = osc->timbre_q15;
                osc->color_current_q15 = osc->color_q15;
            }
        }
    }
    else
    {
        brick6_stack_runtime_advance_free_running(instance, frames);
    }

    if ((source_active != 0U)
            && ((instance->noise_level_q15 != 0U)
                || (instance->noise_level_current_q15 != 0U)))
    {
        const uint16_t effective_noise = (uint16_t)(((uint32_t)instance->noise_level_q15
                * (uint32_t)instance->voice.velocity_q15) >> 15);
        if (level_energy_q30 == 0U)
        {
            level_energy_q30 = (uint32_t)effective_noise * (uint32_t)effective_noise;
        }
        const int32_t noise_delta = (int32_t)instance->noise_level_q15
            - (int32_t)instance->noise_level_current_q15;
        brick6_stack_exact_ramp_t noise_ramp;
        brick6_stack_exact_ramp_init(&noise_ramp, noise_delta, frames);
        for (uint8_t i = 0U; i < frames; ++i)
        {
            const int16_t sample_q15 = brick6_stack_runtime_render_noise_sample(instance);
            const uint16_t noise_here = (uint16_t)(
                (int32_t)instance->noise_level_current_q15
                + (int32_t)brick6_stack_exact_ramp_next(&noise_ramp));
            const uint16_t effective_here = (uint16_t)(
                ((uint32_t)noise_here * (uint32_t)instance->voice.velocity_q15) >> 15);
            acc[i] += ((int32_t)sample_q15 * (int32_t)effective_here) >> 15;
        }
        instance->noise_level_current_q15 = instance->noise_level_q15;
    }

    const uint16_t output_gain_q15 = brick6_stack_runtime_energy_gain_q15(level_energy_q30);
    const uint8_t diag_stack = audio_track_diag_is_selected_logical_track(instance_id);
    if (diag_stack != 0U)
    {
        uint32_t soft_clip_activations = 0U;
        for (uint8_t i = 0U; i < frames; ++i)
        {
            const int32_t post_gain = (acc[i] * (int32_t)output_gain_q15) >> 15;
            uint8_t activated = 0U;
            instance->pending_mono[i] = brick6_stack_soft_clip_q15(post_gain, &activated);
            soft_clip_activations += activated;
        }
        audio_track_diag_report_stack_soft_clips(instance_id, soft_clip_activations);
    }
    else
    {
        for (uint8_t i = 0U; i < frames; ++i)
        {
            const int32_t post_gain = (acc[i] * (int32_t)output_gain_q15) >> 15;
            instance->pending_mono[i] = brick6_stack_soft_clip_q15(post_gain, NULL);
        }
    }
    instance->pending_offset = 0U;
    instance->pending_count = frames;
}

void brick6_stack_runtime_init(void)
{
    g_stack_command_head = 0U;
    g_stack_command_tail = 0U;
    memset((void *)g_stack_note_cancel_pending, 0, sizeof(g_stack_note_cancel_pending));
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
    brick6_stack_runtime_touch_config(instance);
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
    brick6_stack_runtime_flush_pending(instance);
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
    brick6_stack_runtime_touch_config(instance);
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
    brick6_stack_runtime_touch_config(instance);
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
    brick6_stack_runtime_touch_config(instance);
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
    brick6_stack_runtime_touch_config(instance);
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
    if (instance->phase_reset != 0U)
    {
        brick6_stack_runtime_generate_osc_detune_offsets(instance);
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
    brick6_stack_runtime_flush_pending(instance);
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
    brick6_stack_runtime_flush_pending(instance);
}

uint8_t brick6_stack_runtime_submit_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_NOTE_ON,
                                               instance_id,
                                               note,
                                               velocity,
                                               0,
                                               0U);
}

uint8_t brick6_stack_runtime_submit_note_off(uint8_t instance_id, uint8_t note)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_NOTE_OFF,
                                               instance_id,
                                               note,
                                               0U,
                                               0,
                                               0U);
}

uint8_t brick6_stack_runtime_submit_all_notes_off(uint8_t instance_id)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_ALL_NOTES_OFF,
                                               instance_id,
                                               0U,
                                               0U,
                                               0,
                                               0U);
}

uint8_t brick6_stack_runtime_submit_reset_instance(uint8_t instance_id)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_RESET,
                                               instance_id,
                                               0U,
                                               0U,
                                               0,
                                               0U);
}

void brick6_stack_runtime_cancel_note_state(uint8_t instance_id)
{
    if (instance_id >= BRICK6_STACK_MAX_INSTANCES)
    {
        return;
    }

    const uint32_t primask = brick6_stack_enter_critical();
    uint8_t read = g_stack_command_tail;
    uint8_t write = read;
    const uint8_t head = g_stack_command_head;
    while (read != head)
    {
        const brick6_stack_runtime_command_t command = g_stack_command_queue[read];
        read = (uint8_t)((read + 1U) % STACK_COMMAND_QUEUE_CAP);

        const uint8_t is_note_command =
            ((command.type == (uint8_t)STACK_COMMAND_NOTE_ON)
                || (command.type == (uint8_t)STACK_COMMAND_NOTE_OFF)
                || (command.type == (uint8_t)STACK_COMMAND_ALL_NOTES_OFF)
                || (command.type == (uint8_t)STACK_COMMAND_RESET)) ? 1U : 0U;
        if ((command.instance_id == instance_id) && (is_note_command != 0U))
        {
            continue;
        }

        g_stack_command_queue[write] = command;
        write = (uint8_t)((write + 1U) % STACK_COMMAND_QUEUE_CAP);
    }
    g_stack_command_head = write;
    g_stack_note_cancel_pending[instance_id] = 1U;
    brick6_stack_exit_critical(primask);
}

uint8_t brick6_stack_runtime_submit_slot_model(uint8_t instance_id, uint8_t slot, brick6_stack_model_t model)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_MODEL,
                                               instance_id,
                                               slot,
                                               (uint8_t)model,
                                               0,
                                               0U);
}

uint8_t brick6_stack_runtime_submit_slot_level(uint8_t instance_id, uint8_t slot, float level)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_LEVEL,
                                               instance_id,
                                               slot,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(level));
}

uint8_t brick6_stack_runtime_submit_slot_tune(uint8_t instance_id, uint8_t slot, float semitones)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_TUNE,
                                               instance_id,
                                               slot,
                                               0U,
                                               brick6_stack_tune_to_cents(semitones),
                                               0U);
}

uint8_t brick6_stack_runtime_submit_slot_timbre(uint8_t instance_id, uint8_t slot, float timbre)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_TIMBRE,
                                               instance_id,
                                               slot,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(timbre));
}

uint8_t brick6_stack_runtime_submit_slot_color(uint8_t instance_id, uint8_t slot, float color)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_COLOR,
                                               instance_id,
                                               slot,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(color));
}

uint8_t brick6_stack_runtime_submit_noise_level(uint8_t instance_id, float level)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_NOISE_LEVEL,
                                               instance_id,
                                               0U,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(level));
}

uint8_t brick6_stack_runtime_submit_osc_detune(uint8_t instance_id, float detune)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_OSC_DETUNE,
                                               instance_id,
                                               0U,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(detune));
}

uint8_t brick6_stack_runtime_submit_phase_reset(uint8_t instance_id, uint8_t enabled)
{
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_PHASE_RESET,
                                               instance_id,
                                               0U,
                                               (enabled != 0U) ? 1U : 0U,
                                               0,
                                               0U);
}

void brick6_stack_runtime_process_commands_from_audio(void)
{
    for (uint8_t instance_id = 0U; instance_id < BRICK6_STACK_MAX_INSTANCES; ++instance_id)
    {
        uint8_t cancel_pending = 0U;
        const uint32_t primask = brick6_stack_enter_critical();
        cancel_pending = g_stack_note_cancel_pending[instance_id];
        g_stack_note_cancel_pending[instance_id] = 0U;
        brick6_stack_exit_critical(primask);
        if (cancel_pending != 0U)
        {
            brick6_stack_runtime_all_notes_off(instance_id);
        }
    }

    for (;;)
    {
        const uint32_t primask = brick6_stack_enter_critical();
        if (g_stack_command_tail == g_stack_command_head)
        {
            brick6_stack_exit_critical(primask);
            break;
        }

        const uint8_t tail = g_stack_command_tail;
        const brick6_stack_runtime_command_t command = g_stack_command_queue[tail];
        g_stack_command_tail = (uint8_t)((tail + 1U) % STACK_COMMAND_QUEUE_CAP);
        brick6_stack_exit_critical(primask);

        switch ((brick6_stack_runtime_command_type_t)command.type)
        {
            case STACK_COMMAND_NOTE_ON:
                brick6_stack_runtime_note_on(command.instance_id, command.note, command.velocity);
                break;
            case STACK_COMMAND_NOTE_OFF:
                brick6_stack_runtime_note_off(command.instance_id, command.note);
                break;
            case STACK_COMMAND_ALL_NOTES_OFF:
                brick6_stack_runtime_all_notes_off(command.instance_id);
                break;
            case STACK_COMMAND_RESET:
                brick6_stack_runtime_reset_instance(command.instance_id);
                break;
            case STACK_COMMAND_SET_SLOT_MODEL:
                brick6_stack_runtime_set_slot_model(command.instance_id,
                                                    command.note,
                                                    (brick6_stack_model_t)command.velocity);
                break;
            case STACK_COMMAND_SET_SLOT_LEVEL:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if ((instance != NULL) && (command.note < BRICK6_STACK_SLOT_COUNT))
                {
                    instance->slots[command.note].level_q15 = command.value_u16;
                    instance->slots[command.note].level = (uint8_t)((command.value_u16 * 127U) / 32767U);
                }
                break;
            }
            case STACK_COMMAND_SET_SLOT_TUNE:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if ((instance != NULL) && (command.note < BRICK6_STACK_SLOT_COUNT))
                {
                    instance->slots[command.note].tune_cents = command.value_i16;
                    brick6_stack_runtime_update_slot_pitch(instance, command.note);
                }
                break;
            }
            case STACK_COMMAND_SET_SLOT_TIMBRE:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if ((instance != NULL) && (command.note < BRICK6_STACK_SLOT_COUNT))
                {
                    instance->slots[command.note].timbre_q15 = command.value_u16;
                    instance->slots[command.note].timbre = (uint8_t)((command.value_u16 * 127U) / 32767U);
                }
                break;
            }
            case STACK_COMMAND_SET_SLOT_COLOR:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if ((instance != NULL) && (command.note < BRICK6_STACK_SLOT_COUNT))
                {
                    instance->slots[command.note].color_q15 = command.value_u16;
                    instance->slots[command.note].color = (uint8_t)((command.value_u16 * 127U) / 32767U);
                }
                break;
            }
            case STACK_COMMAND_SET_NOISE_LEVEL:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if (instance != NULL)
                {
                    instance->noise_level_q15 = command.value_u16;
                }
                break;
            }
            case STACK_COMMAND_SET_OSC_DETUNE:
            {
                brick6_stack_runtime_set_osc_detune(command.instance_id,
                                                    (float)command.value_u16 * (1.0f / 32767.0f));
                break;
            }
            case STACK_COMMAND_SET_PHASE_RESET:
                brick6_stack_runtime_set_phase_reset(command.instance_id, command.velocity);
                break;
            default:
                break;
        }
    }
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
    if (dst->synced_config_version == src->config_version) return;
    dst->noise_level_q15 = src->noise_level_q15;
    dst->osc_detune_q15 = src->osc_detune_q15;
    dst->phase_reset = src->phase_reset;
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        stack_osc_slot_t *const out = &dst->slots[slot];
        const stack_osc_slot_t *const in = &src->slots[slot];
        out->model = in->model;
        out->family = in->family;
        out->kernel_id = in->kernel_id;
        out->renderer_id = in->renderer_id;
        out->level_q15 = in->level_q15;
        out->tune_cents = in->tune_cents;
        out->timbre_q15 = in->timbre_q15;
        out->color_q15 = in->color_q15;
    }
    dst->synced_config_version = src->config_version;
}

uint8_t brick6_stack_runtime_render_instance(uint8_t instance_id,
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
        brick6_stack_runtime_flush_pending(instance);
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

    uint32_t rendered = 0U;
    while (rendered < frames)
    {
        if (instance->pending_count == 0U)
        {
            const uint32_t remaining = frames - rendered;
            const uint8_t chunk = (remaining > BRICK6_STACK_RENDER_BLOCK_SIZE)
                ? BRICK6_STACK_RENDER_BLOCK_SIZE
                : (uint8_t)remaining;
            brick6_stack_runtime_generate_pending(instance_id,
                                                  instance,
                                                  chunk,
                                                  source_active);
        }

        while ((rendered < frames) && (instance->pending_count > 0U))
        {
            const int16_t sample = instance->pending_mono[instance->pending_offset];
            out_mono[rendered] = (float)sample * (1.0f / 32768.0f);
            rendered++;
            instance->pending_offset++;
            instance->pending_count--;
        }
    }
    return 1U;
}

const brick6_stack_runtime_voice_t *brick6_stack_runtime_get_voice(uint8_t instance_id)
{
    const brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}
