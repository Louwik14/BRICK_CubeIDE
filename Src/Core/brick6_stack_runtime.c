/**
 * @file brick6_stack_runtime.c
 * @brief Minimal independent Stack runtime, without MacroOscillator ownership.
 */

#include "Core/brick6_stack_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Core/brick6_stack_braids_resources.h"
#include "Core/brick6_stack_waveform.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define STACK_DEFAULT_RNG_SEED 0x6D2B79F5UL
#define STACK_DEFAULT_NOTE 60U
#define STACK_BASE_C4_INC 23428135UL
#define STACK_COMMAND_QUEUE_CAP 256U
#define STACK_OSC_DETUNE_MAX_SHIFT 8U
#define STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15 10752
#define STACK_BRAIDS_SWARM_OSC_GAIN_Q15 4096
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
    STACK_RENDERER_SOFT = 0,
    STACK_RENDERER_SHAPE,
    STACK_RENDERER_SINE_FOLD,
    STACK_RENDERER_TRI_FOLD,
    STACK_RENDERER_WAVETABLE,
    STACK_RENDERER_SUB,
    STACK_RENDERER_FM,
    STACK_RENDERER_FEEDBACK_FM,
    STACK_RENDERER_RING,
    STACK_RENDERER_TRIPLE_SAW,
    STACK_RENDERER_TRIPLE_SQUARE,
    STACK_RENDERER_SWARM,
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
    STACK_COMMAND_SET_SLOT_PARAM3,
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
    uint8_t kernel_state_size;
} brick6_stack_model_desc_t;

typedef struct
{
    brick6_stack_runtime_voice_t voice;
    stack_osc_slot_t slots[BRICK6_STACK_SLOT_COUNT];
    uint16_t noise_level_q15;
    uint16_t osc_detune_q15;
    uint32_t rng;
    int16_t osc_detune_offset_q15[BRICK6_STACK_SLOT_COUNT];
    uint8_t pending_offset;
    uint8_t pending_count;
    uint8_t phase_reset;
    uint8_t reserved0;
    int16_t pending_mono[BRICK6_STACK_RENDER_BLOCK_SIZE];
} brick6_stack_runtime_instance_t;

_Static_assert(sizeof(stack_osc_slot_t) <= 192U, "stack_osc_slot_t RAM budget exceeded");
_Static_assert(sizeof(brick6_stack_runtime_instance_t) <= 768U, "brick6_stack_runtime_instance_t RAM budget exceeded");

AUDIO_HOT static brick6_stack_runtime_instance_t g_stack_runtime[BRICK6_STACK_MAX_INSTANCES];
AUDIO_HOT static volatile uint8_t g_stack_command_head;
AUDIO_HOT static volatile uint8_t g_stack_command_tail;
AUDIO_HOT static brick6_stack_runtime_command_t g_stack_command_queue[STACK_COMMAND_QUEUE_CAP];

static const brick6_stack_model_desc_t k_stack_model_catalog[BRICK6_STACK_MODEL_COUNT] = {
    [BRICK6_STACK_MODEL_SOFT] = {
        "SOFT", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_PHASE_FOLD, STACK_RENDERER_SOFT, 12U
    },
    [BRICK6_STACK_MODEL_SHAPE] = {
        "SHAPE", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_PHASE_BASIC, STACK_RENDERER_SHAPE, 8U
    },
    [BRICK6_STACK_MODEL_WAVETABLE] = {
        "WAVETABLE", BRICK6_STACK_FAMILY_TABLE, BRICK6_STACK_KERNEL_WAVETABLE, STACK_RENDERER_WAVETABLE, 16U
    },
    [BRICK6_STACK_MODEL_SUB] = {
        "SUB", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_SUB, STACK_RENDERER_SUB, 20U
    },
    [BRICK6_STACK_MODEL_FM] = {
        "FM", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_FM, STACK_RENDERER_FM, 28U
    },
    [BRICK6_STACK_MODEL_FEEDBACK_FM] = {
        "FEEDBACK FM", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_FEEDBACK_FM, STACK_RENDERER_FEEDBACK_FM, 32U
    },
    [BRICK6_STACK_MODEL_RING] = {
        "RING", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_RING, STACK_RENDERER_RING, 32U
    },
    [BRICK6_STACK_MODEL_TRIPLE_SAW] = {
        "TRIPLE SAW", BRICK6_STACK_FAMILY_ENSEMBLE, BRICK6_STACK_KERNEL_TRIPLE_ANALOG, STACK_RENDERER_TRIPLE_SAW, 28U
    },
    [BRICK6_STACK_MODEL_TRIPLE_SQUARE] = {
        "TRIPLE SQUARE", BRICK6_STACK_FAMILY_ENSEMBLE, BRICK6_STACK_KERNEL_TRIPLE_ANALOG, STACK_RENDERER_TRIPLE_SQUARE, 28U
    },
    [BRICK6_STACK_MODEL_SWARM] = {
        "SWARM", BRICK6_STACK_FAMILY_ENSEMBLE, BRICK6_STACK_KERNEL_SWARM, STACK_RENDERER_SWARM, 40U
    },
    [BRICK6_STACK_MODEL_SINE_FOLD] = {
        "SINE FOLD", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_PHASE_FOLD, STACK_RENDERER_SINE_FOLD, 16U
    },
    [BRICK6_STACK_MODEL_TRI_FOLD] = {
        "TRI FOLD", BRICK6_STACK_FAMILY_PHASE, BRICK6_STACK_KERNEL_PHASE_FOLD, STACK_RENDERER_TRI_FOLD, 16U
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

static int16_t brick6_stack_soft_clip_q15(int32_t value)
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
    if (instance_id >= BRICK6_STACK_MAX_INSTANCES)
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

static const brick6_stack_model_desc_t *brick6_stack_runtime_model_desc(brick6_stack_model_t model)
{
    if ((uint8_t)model >= (uint8_t)BRICK6_STACK_MODEL_COUNT)
    {
        return &k_stack_model_catalog[BRICK6_STACK_MODEL_SHAPE];
    }
    return &k_stack_model_catalog[(uint8_t)model];
}

static brick6_stack_runtime_instance_t *brick6_stack_runtime_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_STACK_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_stack_runtime[instance_id];
}

static const brick6_stack_runtime_instance_t *brick6_stack_runtime_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_STACK_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_stack_runtime[instance_id];
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
    slot->kernel_state_size = desc->kernel_state_size;
    slot->level = enabled;
    slot->level_q15 = (enabled != 0U) ? 32767U : 0U;
    slot->timbre = 127U;
    slot->color = 127U;
    slot->param3 = 127U;
    slot->timbre_q15 = 16384U;
    slot->color_q15 = 16384U;
    slot->param3_q15 = 16384U;
    slot->phase_inc = brick6_stack_note_to_phase_inc(STACK_DEFAULT_NOTE);
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
    instance->osc_detune_q15 = 0U;
    instance->phase_reset = 0U;
    instance->rng = STACK_DEFAULT_RNG_SEED;
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        instance->osc_detune_offset_q15[slot] = 0;
    }
    brick6_stack_runtime_update_all_pitches(instance);
}

static int16_t brick6_stack_phase_saw(uint32_t phase)
{
    return brick6_stack_waveform_saw(phase);
}

static int16_t brick6_stack_phase_sine(uint32_t phase)
{
    return brick6_stack_waveform_sine(phase);
}

static int16_t brick6_stack_phase_pwm(uint32_t phase, uint16_t width_q15)
{
    return brick6_stack_waveform_pwm(phase, width_q15);
}

static int16_t brick6_stack_mix_q15(int16_t a, int16_t b, uint16_t balance_q15)
{
    if (balance_q15 == 0U)
    {
        return a;
    }
    if (balance_q15 >= 32767U)
    {
        return b;
    }
    const int32_t inv = 32767 - (int32_t)balance_q15;
    const int32_t mixed = (((int32_t)a * inv) + ((int32_t)b * (int32_t)balance_q15)) >> 15;
    return brick6_stack_sat16(mixed);
}

static int16_t brick6_stack_mix_q16(int16_t a, int16_t b, uint16_t balance_q16)
{
    const int32_t mixed = (int32_t)a + ((((int32_t)b - (int32_t)a) * (int32_t)balance_q16) >> 16);
    return brick6_stack_sat16(mixed);
}

static int16_t brick6_stack_mul_q16(int16_t a, int16_t b)
{
    return (int16_t)(((int32_t)a * (int32_t)b) >> 16);
}

static int32_t brick6_stack_apply_gain_q15(int16_t sample, int32_t gain_q15)
{
    return ((int32_t)sample * gain_q15) >> 15;
}

static uint32_t brick6_stack_pm_offset_fm(uint16_t modulator, uint16_t amount_q15)
{
    return (uint32_t)((uint32_t)((int32_t)(int16_t)modulator * (int32_t)amount_q15) << 2);
}

static uint32_t brick6_stack_pm_offset_feedback_fm(uint16_t modulator, uint16_t amount_q15)
{
    return (uint32_t)((uint32_t)((int32_t)(int16_t)modulator * (int32_t)amount_q15) << 1);
}

static uint32_t brick6_stack_scale_inc(uint32_t inc, uint32_t ratio_q15)
{
    uint64_t scaled = ((uint64_t)inc * (uint64_t)ratio_q15) >> 15;
    if (scaled == 0U)
    {
        scaled = 1U;
    }
    if (scaled > 0x7FFFFFFFULL)
    {
        scaled = 0x7FFFFFFFULL;
    }
    return (uint32_t)scaled;
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

static uint32_t brick6_stack_ratio_from_color(uint16_t color_q15)
{
    static const uint32_t k_ratios_q15[] = {
        16384U, 24576U, 32768U, 49152U, 65535U, 98304U, 131070U, 196605U
    };
    const uint32_t scaled = (uint32_t)color_q15 * (uint32_t)(sizeof(k_ratios_q15) / sizeof(k_ratios_q15[0]));
    const uint8_t index = (uint8_t)(scaled >> 15);
    return k_ratios_q15[index < 8U ? index : 7U];
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

static int16_t brick6_stack_wave_soft(stack_osc_slot_t *slot)
{
    return brick6_stack_waveform_soft(slot->phase, slot->timbre_q15, slot->color_q15);
}

static int16_t brick6_stack_wave_shape(stack_osc_slot_t *slot)
{
    return brick6_stack_waveform_shape(slot->phase, slot->timbre_q15, slot->color_q15);
}

static int16_t brick6_stack_wave_sine_fold(stack_osc_slot_t *slot)
{
    return brick6_stack_waveform_sine_fold(slot->phase, slot->timbre_q15, slot->color_q15, slot->param3_q15);
}

static int16_t brick6_stack_wave_tri_fold(stack_osc_slot_t *slot)
{
    return brick6_stack_waveform_tri_fold(slot->phase, slot->timbre_q15, slot->color_q15, slot->param3_q15);
}

static void brick6_stack_runtime_render_soft(stack_osc_slot_t *slot,
                                             int32_t *acc,
                                             uint8_t frames,
                                             uint16_t effective_level)
{
    brick6_stack_render_waveform(slot, acc, frames, effective_level, brick6_stack_wave_soft);
}

static void brick6_stack_runtime_render_shape(stack_osc_slot_t *slot,
                                                 int32_t *acc,
                                                 uint8_t frames,
                                                 uint16_t effective_level)
{
    brick6_stack_render_waveform(slot, acc, frames, effective_level, brick6_stack_wave_shape);
}

static void brick6_stack_runtime_render_sine_fold(stack_osc_slot_t *slot,
                                                  int32_t *acc,
                                                  uint8_t frames,
                                                  uint16_t effective_level)
{
    brick6_stack_render_waveform(slot, acc, frames, effective_level, brick6_stack_wave_sine_fold);
}

static void brick6_stack_runtime_render_tri_fold(stack_osc_slot_t *slot,
                                                 int32_t *acc,
                                                 uint8_t frames,
                                                 uint16_t effective_level)
{
    brick6_stack_render_waveform(slot, acc, frames, effective_level, brick6_stack_wave_tri_fold);
}

static void brick6_stack_runtime_render_wavetable(stack_osc_slot_t *slot,
                                                  int32_t *acc,
                                                  uint8_t frames,
                                                  uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint8_t bank = (uint8_t)(((uint32_t)slot->color_q15
            * BRICK6_STACK_BRAIDS_WAVETABLE_BANK_COUNT) >> 15);
    const uint8_t bank_base = (uint8_t)(((bank < BRICK6_STACK_BRAIDS_WAVETABLE_BANK_COUNT)
            ? bank
            : (BRICK6_STACK_BRAIDS_WAVETABLE_BANK_COUNT - 1U))
            * BRICK6_STACK_BRAIDS_WAVETABLE_BANK_SIZE);
    const uint32_t position = (uint32_t)slot->timbre_q15 * (BRICK6_STACK_BRAIDS_WAVETABLE_BANK_SIZE - 1U) * 2U;
    const uint8_t offset = (uint8_t)(position >> 16);
    const uint16_t xfade_q16 = (uint16_t)(position & 0xFFFFU);
    const uint8_t wave_a = (uint8_t)(bank_base + offset);
    const uint8_t wave_b = (uint8_t)(bank_base
            + ((offset + 1U) < BRICK6_STACK_BRAIDS_WAVETABLE_BANK_SIZE ? (offset + 1U) : offset));
    const uint32_t half_inc = slot->phase_inc >> 1;

    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += (half_inc != 0U) ? half_inc : 1U;
        const int16_t a = brick6_stack_braids_wavetable_sample(wave_a, slot->phase);
        const int16_t b = brick6_stack_braids_wavetable_sample(wave_b, slot->phase);
        int32_t sample_q15 = (int32_t)brick6_stack_mix_q16(a, b, xfade_q16) >> 1;
        slot->phase += (half_inc != 0U) ? half_inc : 1U;
        const int16_t c = brick6_stack_braids_wavetable_sample(wave_a, slot->phase);
        const int16_t d = brick6_stack_braids_wavetable_sample(wave_b, slot->phase);
        sample_q15 += (int32_t)brick6_stack_mix_q16(c, d, xfade_q16) >> 1;
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_sub(stack_osc_slot_t *slot,
                                            int32_t *acc,
                                            uint8_t frames,
                                            uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t sub_inc = (slot->color_q15 < 16384U) ? (slot->phase_inc >> 1) : (slot->phase_inc >> 2);
    const uint16_t sub_mix = (uint16_t)(slot->color_q15 < 16384U ? ((16383U - slot->color_q15) << 1)
                                                                : ((slot->color_q15 - 16384U) << 1));
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += (sub_inc != 0U) ? sub_inc : 1U;
        const int16_t principal = brick6_stack_mix_q15(brick6_stack_phase_saw(slot->phase),
                                                       brick6_stack_phase_pwm(slot->phase, slot->timbre_q15),
                                                       slot->timbre_q15);
        const int16_t sub = brick6_stack_phase_pwm(slot->phase2, 16384U);
        const int16_t sample_q15 = brick6_stack_mix_q15(principal, sub, sub_mix);
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_fm(stack_osc_slot_t *slot,
                                           int32_t *acc,
                                           uint8_t frames,
                                           uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t mod_inc = brick6_stack_scale_inc(slot->phase_inc, brick6_stack_ratio_from_color(slot->color_q15));
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += mod_inc;
        const int16_t mod = brick6_stack_phase_sine(slot->phase2);
        const uint32_t offset = brick6_stack_pm_offset_fm((uint16_t)mod, slot->timbre_q15);
        const int16_t sample_q15 = brick6_stack_phase_sine(slot->phase + (uint32_t)offset);
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_feedback_fm(stack_osc_slot_t *slot,
                                                    int32_t *acc,
                                                    uint8_t frames,
                                                    uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t mod_inc = brick6_stack_scale_inc(slot->phase_inc, brick6_stack_ratio_from_color(slot->color_q15));
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += mod_inc;
        const uint32_t feedback_pm = (uint32_t)((uint32_t)(int32_t)slot->feedback_q15 << 14);
        const int16_t mod = brick6_stack_phase_sine(slot->phase2 + feedback_pm);
        const uint32_t offset = brick6_stack_pm_offset_feedback_fm((uint16_t)mod, slot->timbre_q15);
        const int16_t sample_q15 = brick6_stack_phase_sine(slot->phase + (uint32_t)offset);
        slot->feedback_q15 = sample_q15;
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_ring(stack_osc_slot_t *slot,
                                             int32_t *acc,
                                             uint8_t frames,
                                             uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t mod1_inc = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, 1);
    const uint32_t mod2_inc = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, -1);
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += mod1_inc;
        slot->phase3 += mod2_inc;
        const int16_t a = brick6_stack_phase_sine(slot->phase);
        const int16_t b = brick6_stack_phase_sine(slot->phase2);
        const int16_t c = brick6_stack_phase_sine(slot->phase3);
        const int16_t sample_q15 = brick6_stack_mul_q16(brick6_stack_mul_q16(a, b), c);
        acc[i] += ((int32_t)sample_q15 * (int32_t)effective_level) >> 15;
    }
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
        const int32_t mixed = brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase),
                                                          STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase2),
                                              STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase3),
                                              STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15);
        acc[i] += (mixed * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_triple_square(stack_osc_slot_t *slot,
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
        const int32_t mixed = brick6_stack_apply_gain_q15(brick6_stack_phase_pwm(slot->phase, 16384U),
                                                          STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_pwm(slot->phase2, 16384U),
                                              STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_pwm(slot->phase3, 16384U),
                                              STACK_BRAIDS_TRIPLE_OSC_GAIN_Q15);
        acc[i] += (mixed * (int32_t)effective_level) >> 15;
    }
}

static void brick6_stack_runtime_render_swarm(stack_osc_slot_t *slot,
                                              int32_t *acc,
                                              uint8_t frames,
                                              uint16_t effective_level)
{
    if ((slot == NULL) || (acc == NULL))
    {
        return;
    }

    const uint32_t inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
    const uint32_t inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, 1);
    const uint16_t damp_q15 = (uint16_t)(2048U + (((uint32_t)slot->color_q15 * 14336U) >> 15));
    for (uint8_t i = 0U; i < frames; ++i)
    {
        slot->phase += slot->phase_inc;
        slot->phase2 += inc2;
        slot->phase3 += inc3;
        const int32_t mixed = brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase),
                                                          STACK_BRAIDS_SWARM_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase2),
                                              STACK_BRAIDS_SWARM_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase3),
                                              STACK_BRAIDS_SWARM_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase + 0x33333333UL),
                                              STACK_BRAIDS_SWARM_OSC_GAIN_Q15)
                + brick6_stack_apply_gain_q15(brick6_stack_phase_saw(slot->phase3 + 0x99999999UL),
                                              STACK_BRAIDS_SWARM_OSC_GAIN_Q15);
        slot->swarm_lp_q15 = brick6_stack_sat16((int32_t)slot->swarm_lp_q15
                + (((mixed - (int32_t)slot->swarm_lp_q15) * (int32_t)damp_q15) >> 15));
        acc[i] += ((int32_t)slot->swarm_lp_q15 * (int32_t)effective_level) >> 15;
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
        case STACK_RENDERER_SOFT:
        case STACK_RENDERER_SHAPE:
        case STACK_RENDERER_SINE_FOLD:
        case STACK_RENDERER_TRI_FOLD:
            slot->phase += slot->phase_inc * (uint32_t)frames;
            break;

        case STACK_RENDERER_WAVETABLE:
        {
            const uint32_t half_inc = slot->phase_inc >> 1;
            const uint32_t step = ((half_inc != 0U) ? half_inc : 1U) * 2U;
            slot->phase += step * (uint32_t)frames;
            break;
        }

        case STACK_RENDERER_SUB:
        {
            const uint32_t sub_inc = (slot->color_q15 < 16384U) ? (slot->phase_inc >> 1) : (slot->phase_inc >> 2);
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += ((sub_inc != 0U) ? sub_inc : 1U) * (uint32_t)frames;
            break;
        }

        case STACK_RENDERER_FM:
        case STACK_RENDERER_FEEDBACK_FM:
        {
            const uint32_t mod_inc = brick6_stack_scale_inc(slot->phase_inc, brick6_stack_ratio_from_color(slot->color_q15));
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += mod_inc * (uint32_t)frames;
            break;
        }

        case STACK_RENDERER_RING:
        {
            const uint32_t mod1_inc = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, 1);
            const uint32_t mod2_inc = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, -1);
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += mod1_inc * (uint32_t)frames;
            slot->phase3 += mod2_inc * (uint32_t)frames;
            break;
        }

        case STACK_RENDERER_TRIPLE_SAW:
        case STACK_RENDERER_TRIPLE_SQUARE:
        {
            const uint32_t inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
            const uint32_t inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->color_q15, 1);
            slot->phase += slot->phase_inc * (uint32_t)frames;
            slot->phase2 += inc2 * (uint32_t)frames;
            slot->phase3 += inc3 * (uint32_t)frames;
            break;
        }

        case STACK_RENDERER_SWARM:
        {
            const uint32_t inc2 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, -1);
            const uint32_t inc3 = brick6_stack_detune_inc(slot->phase_inc, slot->timbre_q15, 1);
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
    brick6_stack_runtime_render_soft,
    brick6_stack_runtime_render_shape,
    brick6_stack_runtime_render_sine_fold,
    brick6_stack_runtime_render_tri_fold,
    brick6_stack_runtime_render_wavetable,
    brick6_stack_runtime_render_sub,
    brick6_stack_runtime_render_fm,
    brick6_stack_runtime_render_feedback_fm,
    brick6_stack_runtime_render_ring,
    brick6_stack_runtime_render_triple_saw,
    brick6_stack_runtime_render_triple_square,
    brick6_stack_runtime_render_swarm,
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

static uint8_t brick6_stack_runtime_slot_is_clean_soft_sine(const stack_osc_slot_t *slot)
{
    return ((slot != NULL)
            && (slot->renderer_id == (uint8_t)STACK_RENDERER_SOFT)
            && (slot->timbre_q15 == 0U)
            && (slot->color_q15 == 0U))
        ? 1U
        : 0U;
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

static void brick6_stack_runtime_generate_pending(brick6_stack_runtime_instance_t *instance, uint8_t frames)
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
    uint8_t active_signal_count = 0U;
    uint8_t clean_soft_sine_signal = 0U;
    if (instance->voice.gate != 0U)
    {
        for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
        {
            stack_osc_slot_t *const osc = &instance->slots[slot];
            if (osc->level_q15 == 0U)
            {
                continue;
            }

            const uint16_t effective_level = (uint16_t)(((uint32_t)osc->level_q15
                    * (uint32_t)instance->voice.velocity_q15) >> 15);
            level_energy_q30 += (uint32_t)effective_level * (uint32_t)effective_level;
            active_signal_count++;
            clean_soft_sine_signal = brick6_stack_runtime_slot_is_clean_soft_sine(osc);
            brick6_stack_runtime_render_slot_chunk(osc,
                                                   acc,
                                                   frames,
                                                   effective_level);
        }
    }
    else
    {
        brick6_stack_runtime_advance_free_running(instance, frames);
    }

    if ((instance->voice.gate != 0U) && (instance->noise_level_q15 != 0U))
    {
        const uint16_t effective_noise = (uint16_t)(((uint32_t)instance->noise_level_q15
                * (uint32_t)instance->voice.velocity_q15) >> 15);
        if (level_energy_q30 == 0U)
        {
            level_energy_q30 = (uint32_t)effective_noise * (uint32_t)effective_noise;
        }
        active_signal_count++;
        clean_soft_sine_signal = 0U;
        for (uint8_t i = 0U; i < frames; ++i)
        {
            const int16_t sample_q15 = brick6_stack_runtime_render_noise_sample(instance);
            acc[i] += ((int32_t)sample_q15 * (int32_t)effective_noise) >> 15;
        }
    }

    const uint16_t output_gain_q15 = brick6_stack_runtime_energy_gain_q15(level_energy_q30);
    const uint8_t bypass_soft_clip = ((active_signal_count == 1U) && (clean_soft_sine_signal != 0U)) ? 1U : 0U;
    for (uint8_t i = 0U; i < frames; ++i)
    {
        const int32_t post_gain = (acc[i] * (int32_t)output_gain_q15) >> 15;
        instance->pending_mono[i] = (bypass_soft_clip != 0U)
            ? brick6_stack_sat16(post_gain)
            : brick6_stack_soft_clip_q15(post_gain);
    }
    instance->pending_offset = 0U;
    instance->pending_count = frames;
}

void brick6_stack_runtime_init(void)
{
    g_stack_command_head = 0U;
    g_stack_command_tail = 0U;
    for (uint8_t instance = 0U; instance < BRICK6_STACK_MAX_INSTANCES; ++instance)
    {
        brick6_stack_runtime_init_instance(&g_stack_runtime[instance]);
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
    instance->slots[slot].level_q15 = brick6_stack_float_to_q15(level);
    instance->slots[slot].level = (uint8_t)((instance->slots[slot].level_q15 * 127U) / 32767U);
}

void brick6_stack_runtime_set_slot_model(uint8_t instance_id, uint8_t slot, brick6_stack_model_t model)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }

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
    osc->kernel_state_size = desc->kernel_state_size;
    osc->phase = 0U;
    osc->phase2 = 0x55555555UL;
    osc->phase3 = 0xAAAAAAAAUL;
    osc->feedback_q15 = 0;
    osc->swarm_lp_q15 = 0;
    brick6_stack_runtime_update_slot_pitch(instance, slot);
    brick6_stack_runtime_flush_pending(instance);
}

void brick6_stack_runtime_set_slot_tune(uint8_t instance_id, uint8_t slot, float semitones)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    instance->slots[slot].tune_cents = brick6_stack_tune_to_cents(semitones);
    brick6_stack_runtime_update_slot_pitch(instance, slot);
}

void brick6_stack_runtime_set_slot_timbre(uint8_t instance_id, uint8_t slot, float timbre)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    instance->slots[slot].timbre_q15 = brick6_stack_float_to_q15(timbre);
    instance->slots[slot].timbre = (uint8_t)((instance->slots[slot].timbre_q15 * 127U) / 32767U);
}

void brick6_stack_runtime_set_slot_color(uint8_t instance_id, uint8_t slot, float color)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    instance->slots[slot].color_q15 = brick6_stack_float_to_q15(color);
    instance->slots[slot].color = (uint8_t)((instance->slots[slot].color_q15 * 127U) / 32767U);
}

void brick6_stack_runtime_set_slot_param3(uint8_t instance_id, uint8_t slot, float param3)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if ((instance == NULL) || (slot >= BRICK6_STACK_SLOT_COUNT))
    {
        return;
    }
    instance->slots[slot].param3_q15 = brick6_stack_float_to_q15(param3);
    instance->slots[slot].param3 = (uint8_t)((instance->slots[slot].param3_q15 * 127U) / 32767U);
}

void brick6_stack_runtime_set_noise_level(uint8_t instance_id, float level)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->noise_level_q15 = brick6_stack_float_to_q15(level);
}

void brick6_stack_runtime_set_osc_detune(uint8_t instance_id, float detune)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->osc_detune_q15 = brick6_stack_float_to_q15(detune);
    if ((instance->osc_detune_q15 != 0U) && (brick6_stack_runtime_osc_detune_offsets_are_zero(instance) != 0U))
    {
        brick6_stack_runtime_generate_osc_detune_offsets(instance);
    }
    brick6_stack_runtime_update_all_pitches(instance);
}

void brick6_stack_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->phase_reset = (enabled != 0U) ? 1U : 0U;
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
    instance->voice.velocity_q15 = (uint16_t)(((uint32_t)velocity * 32767U) / 127U);
    if (instance->phase_reset != 0U)
    {
        brick6_stack_runtime_generate_osc_detune_offsets(instance);
        for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
        {
            instance->slots[slot].phase = (uint32_t)slot * 0x55555555UL;
            instance->slots[slot].phase2 = 0x55555555UL + ((uint32_t)slot * 0x11111111UL);
            instance->slots[slot].phase3 = 0xAAAAAAAAUL - ((uint32_t)slot * 0x11111111UL);
            instance->slots[slot].feedback_q15 = 0;
            instance->slots[slot].swarm_lp_q15 = 0;
        }
    }
    brick6_stack_runtime_update_all_pitches(instance);
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

uint8_t brick6_stack_runtime_submit_slot_param3(uint8_t instance_id, uint8_t slot, float param3)
{
    if (slot >= BRICK6_STACK_SLOT_COUNT)
    {
        return 0U;
    }
    return brick6_stack_runtime_submit_command((uint8_t)STACK_COMMAND_SET_SLOT_PARAM3,
                                               instance_id,
                                               slot,
                                               0U,
                                               0,
                                               brick6_stack_float_to_q15(param3));
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
            case STACK_COMMAND_SET_SLOT_PARAM3:
            {
                brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(command.instance_id);
                if ((instance != NULL) && (command.note < BRICK6_STACK_SLOT_COUNT))
                {
                    instance->slots[command.note].param3_q15 = command.value_u16;
                    instance->slots[command.note].param3 = (uint8_t)((command.value_u16 * 127U) / 32767U);
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

void brick6_stack_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance_mut(instance_id);
    if (out_mono == NULL)
    {
        return;
    }
    if (instance == NULL)
    {
        memset(out_mono, 0, frames * sizeof(float));
        return;
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
            brick6_stack_runtime_generate_pending(instance, chunk);
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
}

const brick6_stack_runtime_voice_t *brick6_stack_runtime_get_voice(uint8_t instance_id)
{
    const brick6_stack_runtime_instance_t *const instance = brick6_stack_runtime_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}
