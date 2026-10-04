#ifndef BRICK6_DX7_SYSEX_H
#define BRICK6_DX7_SYSEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DX7_OPERATOR_COUNT 6U
#define DX7_VOICE_NAME_BYTES 10U
#define DX7_BANK_VOICE_COUNT 32U

typedef struct
{
    uint8_t rates[4];
    uint8_t levels[4];
    uint8_t breakpoint;
    uint8_t left_depth;
    uint8_t right_depth;
    uint8_t left_curve;
    uint8_t right_curve;
    uint8_t rate_scaling;
    uint8_t amplitude_mod_sensitivity;
    uint8_t velocity_sensitivity;
    uint8_t output_level;
    uint8_t oscillator_mode;
    uint8_t coarse;
    uint8_t fine;
    int8_t detune;
} dx7_operator_t;

typedef struct
{
    /* Logical order OP1..OP6, independent of the OP6..OP1 dump order. */
    dx7_operator_t operators[DX7_OPERATOR_COUNT];
    uint8_t pitch_rates[4];
    uint8_t pitch_levels[4];
    uint8_t algorithm;
    uint8_t feedback;
    uint8_t oscillator_key_sync;
    uint8_t lfo_speed;
    uint8_t lfo_delay;
    uint8_t pitch_mod_depth;
    uint8_t amplitude_mod_depth;
    uint8_t lfo_key_sync;
    uint8_t lfo_waveform;
    uint8_t pitch_mod_sensitivity;
    uint8_t transpose;
    char name[DX7_VOICE_NAME_BYTES];
} dx7_voice_t;

typedef enum
{
    DX7_SYSEX_OK = 0,
    DX7_SYSEX_INVALID_ARGUMENT,
    DX7_SYSEX_TRUNCATED,
    DX7_SYSEX_BAD_FRAMING,
    DX7_SYSEX_BAD_MANUFACTURER,
    DX7_SYSEX_BAD_DEVICE,
    DX7_SYSEX_UNSUPPORTED_FORMAT,
    DX7_SYSEX_BAD_LENGTH,
    DX7_SYSEX_BAD_DATA,
    DX7_SYSEX_BAD_CHECKSUM,
    DX7_SYSEX_CAPACITY
} dx7_sysex_result_t;

dx7_sysex_result_t dx7_sysex_parse(const uint8_t *bytes, size_t byte_count,
                                   dx7_voice_t *voices, size_t voice_capacity,
                                   size_t *out_voice_count);

#ifdef __cplusplus
}
#endif

#endif
