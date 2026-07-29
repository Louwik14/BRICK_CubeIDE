/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams.
 *
 * This file is a scalar Cortex-M7 port of the basic-wave paths in:
 *   src/deluge/dsp/oscillators/oscillator.cpp
 *   src/deluge/dsp/oscillators/basic_waves.cpp
 *   src/deluge/processing/vector_rendering_function.h
 * Upstream commit: 0d9cbf0440f0555e2544cc1eb019b31675637008.
 * The intended fractional PWM path was cross-checked against its pre-Argon
 * form at commit 530ca42171b3e5499d918309126ef6979e05b13e.
 *
 * This program is free software under GPL-3.0. See
 * LICENSES/DelugeFirmware-GPL-3.0.txt.
 */
#include "Audio/deluge_oscillator.h"

#include <limits.h>
#include <stddef.h>

#include "Audio/deluge_tables.h"

namespace
{
static const int16_t *const kSawTables[20] = {
    NULL, NULL, NULL, NULL, NULL, NULL, sawWave215, sawWave153, sawWave109, sawWave76,
    sawWave53, sawWave39, sawWave27, sawWave19, sawWave13, sawWave9, sawWave7, sawWave5,
    sawWave3, sawWave1
};

static const int16_t *const kSquareTables[20] = {
    NULL, NULL, NULL, NULL, NULL, NULL, squareWave215, squareWave153, squareWave109, squareWave76,
    squareWave53, squareWave39, squareWave27, squareWave19, squareWave13, squareWave9, squareWave7,
    squareWave5, squareWave3, squareWave1
};

static const int16_t *const kAnalogSquareTables[20] = {
    analogSquare_1722, analogSquare_1217, analogSquare_861, analogSquare_609, analogSquare_431,
    analogSquare_305, analogSquare_215, analogSquare_153, analogSquare_109, analogSquare_76,
    analogSquare_53, analogSquare_39, analogSquare_27, analogSquare_19, analogSquare_13,
    analogSquare_9, analogSquare_7, analogSquare_5, analogSquare_3, analogSquare_1
};

/* Deluge source note: bands 0..7 are mystery synth A; bands 8..19 are mystery synth B. */
static const int16_t *const kAnalogSawTables[20] = {
    mysterySynthASaw_1722, mysterySynthASaw_1217, mysterySynthASaw_861, mysterySynthASaw_609,
    mysterySynthASaw_431, mysterySynthASaw_305, mysterySynthASaw_215, mysterySynthASaw_153,
    mysterySynthBSaw_109, mysterySynthBSaw_76, mysterySynthBSaw_53, mysterySynthBSaw_39,
    mysterySynthBSaw_27, mysterySynthBSaw_19, mysterySynthBSaw_13, mysterySynthBSaw_9,
    mysterySynthBSaw_7, mysterySynthBSaw_5, mysterySynthBSaw_3, mysterySynthBSaw_1
};

static inline int32_t sat_i32(int64_t value)
{
    if (value > INT32_MAX)
    {
        return INT32_MAX;
    }
    if (value < INT32_MIN)
    {
        return INT32_MIN;
    }
    return (int32_t)value;
}

static inline int32_t bits_to_i32(uint32_t bits)
{
    if (bits <= UINT32_C(0x7FFFFFFF))
    {
        return (int32_t)bits;
    }
    return -1 - (int32_t)(UINT32_MAX - bits);
}

static inline int16_t bits_to_i16(uint16_t bits)
{
    if (bits <= UINT16_C(0x7FFF))
    {
        return (int16_t)bits;
    }
    return (int16_t)(-1 - (int32_t)(UINT16_MAX - bits));
}

static inline int32_t asr_i32(int32_t value, uint32_t shift)
{
    const uint32_t bits = (uint32_t)value;
    if (shift >= 32U)
    {
        return (value < 0) ? -1 : 0;
    }
    if ((shift == 0U) || (value >= 0))
    {
        return bits_to_i32(bits >> shift);
    }
    return bits_to_i32((bits >> shift) | (UINT32_MAX << (32U - shift)));
}

static inline int32_t mul_s32_hi(int32_t a, int32_t b)
{
    const int64_t product = (int64_t)a * (int64_t)b;
    return bits_to_i32((uint32_t)((uint64_t)product >> 32));
}

static inline int32_t mul_s32_hi_rounded(int32_t a, int32_t b)
{
    const uint64_t rounded = (uint64_t)((int64_t)a * (int64_t)b) + (UINT64_C(1) << 31);
    return bits_to_i32((uint32_t)(rounded >> 32));
}

static inline int32_t q31_mul_round(int32_t a, int32_t b)
{
    const int64_t rounded = ((int64_t)a * (int64_t)b) + (INT64_C(1) << 30);
    const int64_t divisor = INT64_C(1) << 31;
    const int64_t result = (rounded >= 0)
        ? (rounded / divisor)
        : -(((-rounded) + divisor - 1) / divisor);
    return sat_i32(result);
}

static inline int32_t qdmull_s16(int16_t a, int16_t b)
{
    return sat_i32((int64_t)a * (int64_t)b * 2);
}

static inline int32_t qdmlal_s16(int32_t accumulator, int16_t a, int16_t b)
{
    return sat_i32((int64_t)accumulator + ((int64_t)a * (int64_t)b * 2));
}

static inline int32_t triangle_small(uint32_t phase)
{
    if (phase >= UINT32_C(0x80000000))
    {
        phase = 0U - phase;
    }
    return bits_to_i32(phase - UINT32_C(0x40000000));
}

static inline int32_t square_small(uint32_t phase, uint32_t phase_width)
{
    return (phase >= phase_width) ? INT32_C(-1073741824) : INT32_C(1073741823);
}

static int32_t table_sample_q31(const int16_t *table, int32_t magnitude, uint32_t phase)
{
    const uint32_t index = phase >> (32 - magnitude);
    const uint16_t shifted = (uint16_t)(phase >> (32 - 16 - magnitude));
    const int16_t strength = (int16_t)(shifted >> 1);
    const int16_t value1 = table[index];
    const int16_t value2 = table[index + 1U];
    const int16_t difference = bits_to_i16((uint16_t)((int32_t)value2 - (int32_t)value1));
    return sat_i32(((int64_t)value1 * INT64_C(65536))
                   + ((int64_t)difference * (int64_t)strength * 2));
}

static int32_t pulse_table_sample_q31(const int16_t *table,
                                      int32_t magnitude,
                                      uint32_t phase,
                                      uint32_t phase_to_add)
{
    const uint32_t phase_later = phase + phase_to_add;
    const uint32_t index_a = phase >> (32 - magnitude);
    const uint32_t index_b = phase_later >> (32 - magnitude);
    const int16_t fraction_a = (int16_t)
        ((uint16_t)(phase >> (32 - 16 - magnitude)) & UINT16_C(0x7FFF));
    const int16_t fraction_b = (int16_t)
        ((uint16_t)(phase_later >> (32 - 16 - magnitude)) & UINT16_C(0x7FFF));

    const int16_t strength_a1 = bits_to_i16((uint16_t)fraction_a | UINT16_C(0x8000));
    const int16_t strength_a2 = bits_to_i16(
        (uint16_t)(UINT16_C(0x8000) - (uint16_t)strength_a1));
    int32_t output_a = qdmull_s16(strength_a2, table[index_a + 1U]);
    output_a = qdmlal_s16(output_a, strength_a1, table[index_a]);

    const int16_t strength_b2 = fraction_b;
    const int16_t strength_b1 = (int16_t)(INT16_MAX - strength_b2);
    int32_t output_b = qdmull_s16(strength_b2, table[index_b + 1U]);
    output_b = qdmlal_s16(output_b, strength_b1, table[index_b]);

    return bits_to_i32((uint32_t)q31_mul_round(output_a, output_b) << 1);
}

static void get_table_number(uint32_t increment, int32_t *number, int32_t *magnitude)
{
    static const uint32_t kThresholds[19] = {
        1247086U, 1764571U, 2494173U, 3526245U, 4982560U, 7040929U, 9988296U,
        14035840U, 19701684U, 28256363U, 40518559U, 55063683U, 79536431U,
        113025455U, 165191049U, 238609294U, 306783378U, 429496729U, 715827882U
    };
    static const uint8_t kMagnitudes[20] = {
        13U, 12U, 12U, 11U, 11U, 11U, 11U, 11U, 11U, 11U,
        11U, 11U, 11U, 11U, 10U, 10U, 10U, 10U, 9U, 9U
    };

    int32_t table_number = 19;
    for (int32_t i = 0; i < 19; ++i)
    {
        if (increment <= kThresholds[i])
        {
            table_number = i;
            break;
        }
    }
    *number = table_number;
    *magnitude = (int32_t)kMagnitudes[table_number];
}

typedef struct
{
    deluge_osc_type_t type;
    const int16_t *table;
    int32_t magnitude;
    uint32_t pulse_width;
} render_shape_t;

static int32_t render_shape_sample(const render_shape_t *shape, uint32_t phase)
{
    if ((shape->type == DELUGE_OSC_TRIANGLE) && (shape->table == NULL))
    {
        return bits_to_i32((uint32_t)triangle_small(phase) << 1);
    }
    if ((shape->type == DELUGE_OSC_SAW) && (shape->table == NULL))
    {
        return asr_i32(bits_to_i32(phase), 1U);
    }
    return table_sample_q31(shape->table, shape->magnitude, phase);
}

static void render_crude_sync_scalar(deluge_osc_type_t type,
                                     int32_t *buffer,
                                     uint32_t sample_count,
                                     uint32_t phase,
                                     uint32_t phase_increment,
                                     uint32_t resetter_phase,
                                     uint32_t resetter_phase_increment,
                                     int32_t resetter_divide_by_phase_increment,
                                     uint32_t retrigger_phase)
{
    for (uint32_t i = 0U; i < sample_count; ++i)
    {
        phase += phase_increment;
        resetter_phase += resetter_phase_increment;
        if (resetter_phase < resetter_phase_increment)
        {
            const int32_t reset_fraction =
                mul_s32_hi(bits_to_i32(resetter_phase), bits_to_i32(phase_increment));
            const int32_t reset_offset =
                mul_s32_hi(reset_fraction, resetter_divide_by_phase_increment);
            phase = ((uint32_t)reset_offset << 17) + 1U + retrigger_phase;
        }

        buffer[i] = (type == DELUGE_OSC_TRIANGLE)
            ? bits_to_i32((uint32_t)triangle_small(phase) << 1)
            : asr_i32(bits_to_i32(phase), 1U);
    }
}

static void render_sync_scalar(const render_shape_t *shape,
                               int32_t *buffer,
                               int32_t sample_count,
                               uint32_t phase,
                               uint32_t phase_increment,
                               uint32_t resetter_phase,
                               uint32_t resetter_phase_increment,
                               int32_t resetter_divide_by_phase_increment,
                               uint32_t retrigger_phase)
{
    bool rendered_from_start = false;
    int32_t crossover_sample_before_sync = 0;
    int32_t fade_between_syncs = 0;
    uint32_t samples_including_crossover = 1U;
    int32_t samples_remaining = sample_count;
    int32_t *buffer_start = buffer;

    while (samples_remaining > 0)
    {
        const uint32_t distance = 0U - resetter_phase - (resetter_phase_increment >> 1);
        samples_including_crossover += (distance - 1U) / resetter_phase_increment;
        const bool begin_next_sync = ((uint32_t)samples_remaining >= samples_including_crossover);
        const int32_t render_count = begin_next_sync
            ? (int32_t)samples_including_crossover
            : samples_remaining;

        uint32_t phase_temp = phase;
        for (int32_t i = 0; i < render_count; ++i)
        {
            phase_temp += phase_increment;
            buffer_start[i] = render_shape_sample(shape, phase_temp);
        }

        if (rendered_from_start)
        {
            const int32_t sample_half = asr_i32(buffer_start[0], 1U);
            const int32_t crossover_half = asr_i32(crossover_sample_before_sync, 1U);
            const int32_t average = sample_half + crossover_half;
            const int32_t half_difference = sample_half - crossover_half;
            const int32_t sine_value =
                table_sample_q31(sineWaveSmall, 8, (uint32_t)asr_i32(fade_between_syncs, 1U));
            buffer_start[0] = bits_to_i32(
                (uint32_t)average
                + ((uint32_t)mul_s32_hi(half_difference, sine_value) << 1));
        }

        if (!begin_next_sync)
        {
            return;
        }

        buffer_start += samples_including_crossover - 1U;
        crossover_sample_before_sync = buffer_start[0];
        samples_remaining -= (int32_t)samples_including_crossover - 1;
        resetter_phase += resetter_phase_increment
            * (samples_including_crossover - (rendered_from_start ? 1U : 0U));
        fade_between_syncs = bits_to_i32(
            (uint32_t)mul_s32_hi(bits_to_i32(resetter_phase),
                                 resetter_divide_by_phase_increment) << 17);
        phase = (uint32_t)mul_s32_hi(
            fade_between_syncs, bits_to_i32(phase_increment)) + retrigger_phase;
        phase -= phase_increment;
        rendered_from_start = true;
        samples_including_crossover = 2U;
    }
}
} // namespace

void deluge_oscillator_render(deluge_osc_type_t type,
                              int32_t *output,
                              uint32_t sample_count,
                              uint32_t phase_increment,
                              uint32_t pulse_width,
                              uint32_t *start_phase)
{
    if ((output == NULL) || (start_phase == NULL) || (sample_count == 0U) || (phase_increment == 0U))
    {
        return;
    }

    uint32_t phase = *start_phase;
    *start_phase += phase_increment * sample_count;
    uint32_t retrigger_phase = 0U;
    uint32_t resetter_phase = 0U;
    uint32_t resetter_increment = 0U;
    bool pulse_wave = false;
    int32_t table_number = 0;
    int32_t table_magnitude = 0;

    if (type == DELUGE_OSC_SINE)
    {
        retrigger_phase += UINT32_C(0xC0000000);
    }
    else if (type != DELUGE_OSC_TRIANGLE)
    {
        uint32_t increment_for_table = phase_increment;
        if (type == DELUGE_OSC_SQUARE)
        {
            pulse_wave = (pulse_width != 0U);
            pulse_width += UINT32_C(0x80000000);
            if (pulse_wave)
            {
                increment_for_table = (uint32_t)((double)phase_increment * 0.6);
            }
        }
        get_table_number(increment_for_table, &table_number, &table_magnitude);
        if (type == DELUGE_OSC_SAW)
        {
            retrigger_phase += UINT32_C(0x80000000);
        }
    }

    if ((type != DELUGE_OSC_SQUARE) && (pulse_width != 0U))
    {
        pulse_wave = true;
        const uint32_t width_abs = (bits_to_i32(pulse_width) >= 0)
            ? pulse_width
            : (0U - pulse_width);
        resetter_phase = phase;
        resetter_increment = phase_increment;

        if (type == DELUGE_OSC_ANALOG_SQUARE)
        {
            int64_t phase_to_divide = (int64_t)((uint64_t)resetter_phase << 30);
            if (resetter_phase >= (0U - (resetter_increment >> 1)))
            {
                phase_to_divide -= (INT64_C(1) << 62);
            }
            const uint32_t denominator = (width_abs + UINT32_C(0x80000000)) >> 1;
            phase = (uint32_t)(phase_to_divide / (int32_t)denominator);
            phase_increment =
                (uint32_t)(((uint64_t)phase_increment << 31) / (width_abs + UINT32_C(0x80000000)));
        }
        else
        {
            if (type == DELUGE_OSC_SAW)
            {
                resetter_phase += UINT32_C(0x80000000);
            }
            else if (type == DELUGE_OSC_SINE)
            {
                resetter_phase -= UINT32_C(0xC0000000);
            }

            int32_t resetter_to_multiply =
                asr_i32(bits_to_i32(resetter_phase), 1U);
            if (resetter_phase >= (0U - (resetter_increment >> 1)))
            {
                resetter_to_multiply = bits_to_i32(
                    (uint32_t)resetter_to_multiply - UINT32_C(0x80000000));
            }
            const int32_t width_factor = bits_to_i32(
                (width_abs >> 1) + UINT32_C(0x40000000));
            phase = (uint32_t)mul_s32_hi_rounded(width_factor, resetter_to_multiply) << 3;
            phase_increment = (uint32_t)mul_s32_hi_rounded(
                width_factor, bits_to_i32(phase_increment >> 1)) << 3;
        }
        phase += retrigger_phase;

        const uint16_t rounded_increment = (uint16_t)((resetter_increment + 65535U) >> 16);
        const int32_t divide = bits_to_i32(
            UINT32_C(0x80000000) / rounded_increment);
        render_shape_t shape = { type, NULL, table_magnitude, pulse_width };

        if (type == DELUGE_OSC_SINE)
        {
            shape.table = sineWaveSmall;
            shape.magnitude = 8;
        }
        else if (type == DELUGE_OSC_TRIANGLE)
        {
            if (phase_increment >= 69273666U)
            {
                if (phase_increment <= 102261126U) { shape.table = triangleWaveAntiAliasing21; shape.magnitude = 7; }
                else if (phase_increment <= 143165576U) { shape.table = triangleWaveAntiAliasing15; shape.magnitude = 7; }
                else if (phase_increment <= 238609294U) { shape.table = triangleWaveAntiAliasing9; shape.magnitude = 7; }
                else if (phase_increment <= 429496729U) { shape.table = triangleWaveAntiAliasing5; shape.magnitude = 7; }
                else if (phase_increment <= 715827882U) { shape.table = triangleWaveAntiAliasing3; shape.magnitude = 6; }
                else { shape.table = triangleWaveAntiAliasing1; shape.magnitude = 6; }
            }
        }
        else if (type == DELUGE_OSC_SAW)
        {
            shape.table = (table_number < 6) ? NULL : kSawTables[table_number];
        }
        else if (type == DELUGE_OSC_ANALOG_SAW)
        {
            shape.table = kAnalogSawTables[table_number];
        }
        else
        {
            shape.table = kAnalogSquareTables[table_number];
        }

        if (((type == DELUGE_OSC_TRIANGLE) || (type == DELUGE_OSC_SAW))
                && (shape.table == NULL))
        {
            render_crude_sync_scalar(type,
                                     output,
                                     sample_count,
                                     phase,
                                     phase_increment,
                                     resetter_phase,
                                     resetter_increment,
                                     divide,
                                     retrigger_phase);
            return;
        }

        render_sync_scalar(&shape,
                           output,
                           (int32_t)sample_count,
                           phase,
                           phase_increment,
                           resetter_phase,
                           resetter_increment,
                           divide,
                           retrigger_phase);
        (void)pulse_wave;
        return;
    }

    if (type == DELUGE_OSC_TRIANGLE)
    {
        if (phase_increment < 69273666U)
        {
            for (uint32_t i = 0U; i < sample_count; ++i)
            {
                phase += phase_increment;
                output[i] = bits_to_i32((uint32_t)triangle_small(phase) << 1);
            }
            return;
        }

        const int16_t *table = NULL;
        if (phase_increment <= 102261126U) { table = triangleWaveAntiAliasing21; table_magnitude = 7; }
        else if (phase_increment <= 143165576U) { table = triangleWaveAntiAliasing15; table_magnitude = 7; }
        else if (phase_increment <= 238609294U) { table = triangleWaveAntiAliasing9; table_magnitude = 7; }
        else if (phase_increment <= 429496729U) { table = triangleWaveAntiAliasing5; table_magnitude = 7; }
        else if (phase_increment <= 715827882U) { table = triangleWaveAntiAliasing3; table_magnitude = 6; }
        else { table = triangleWaveAntiAliasing1; table_magnitude = 6; }
        for (uint32_t i = 0U; i < sample_count; ++i)
        {
            phase += phase_increment;
            output[i] = table_sample_q31(table, table_magnitude, phase);
        }
        return;
    }

    if (type == DELUGE_OSC_SINE)
    {
        for (uint32_t i = 0U; i < sample_count; ++i)
        {
            phase += phase_increment;
            output[i] = table_sample_q31(sineWaveSmall, 8, phase);
        }
        return;
    }

    if ((type == DELUGE_OSC_SAW) && (table_number < 6))
    {
        for (uint32_t i = 0U; i < sample_count; ++i)
        {
            phase += phase_increment;
            output[i] = asr_i32(bits_to_i32(phase), 1U);
        }
        return;
    }

    if ((type == DELUGE_OSC_SQUARE) && (table_number < 6))
    {
        for (uint32_t i = 0U; i < sample_count; ++i)
        {
            phase += phase_increment;
            output[i] = square_small(phase, pulse_width);
        }
        return;
    }

    if ((type == DELUGE_OSC_SQUARE) && pulse_wave)
    {
        const int16_t *table = kSquareTables[table_number];
        const uint32_t phase_to_add = 0U - (pulse_width >> 1);
        phase >>= 1;
        phase_increment >>= 1;
        for (uint32_t i = 0U; i < sample_count; ++i)
        {
            phase += phase_increment;
            output[i] = pulse_table_sample_q31(table, table_magnitude, phase, phase_to_add);
        }
        return;
    }

    const int16_t *table = NULL;
    if (type == DELUGE_OSC_SAW) { table = kSawTables[table_number]; }
    else if (type == DELUGE_OSC_SQUARE) { table = kSquareTables[table_number]; }
    else if (type == DELUGE_OSC_ANALOG_SAW) { table = kAnalogSawTables[table_number]; }
    else { table = kAnalogSquareTables[table_number]; }

    for (uint32_t i = 0U; i < sample_count; ++i)
    {
        phase += phase_increment;
        output[i] = table_sample_q31(table, table_magnitude, phase);
    }
}
