#include "Import/dx7_sysex.h"

#include <string.h>

#define DX7_SINGLE_DATA_BYTES 155U
#define DX7_PACKED_VOICE_BYTES 128U
#define DX7_BANK_DATA_BYTES 4096U

static uint8_t range(uint8_t value, uint8_t maximum)
{
    return (uint8_t)(value <= maximum);
}

static uint8_t checksum_valid(const uint8_t *data, size_t length, uint8_t checksum)
{
    uint32_t sum = 0U;
    for (size_t i = 0U; i < length; ++i) sum += data[i];
    return (uint8_t)((((uint8_t)(0U - sum)) & 0x7fU) == checksum);
}

static uint8_t operator_valid(const dx7_operator_t *op)
{
    for (uint8_t i = 0U; i < 4U; ++i)
        if (!range(op->rates[i], 99U) || !range(op->levels[i], 99U)) return 0U;
    return (uint8_t)(range(op->breakpoint, 99U)
        && range(op->left_depth, 99U) && range(op->right_depth, 99U)
        && range(op->left_curve, 3U) && range(op->right_curve, 3U)
        && range(op->rate_scaling, 7U)
        && range(op->amplitude_mod_sensitivity, 3U)
        && range(op->velocity_sensitivity, 7U)
        && range(op->output_level, 99U) && range(op->oscillator_mode, 1U)
        && range(op->coarse, 31U) && range(op->fine, 99U)
        && op->detune >= -7 && op->detune <= 7);
}

static uint8_t voice_valid(const dx7_voice_t *voice)
{
    for (uint8_t op = 0U; op < DX7_OPERATOR_COUNT; ++op)
        if (!operator_valid(&voice->operators[op])) return 0U;
    for (uint8_t i = 0U; i < 4U; ++i)
        if (!range(voice->pitch_rates[i], 99U)
                || !range(voice->pitch_levels[i], 99U)) return 0U;
    return (uint8_t)(range(voice->algorithm, 31U)
        && range(voice->feedback, 7U) && range(voice->oscillator_key_sync, 1U)
        && range(voice->lfo_speed, 99U) && range(voice->lfo_delay, 99U)
        && range(voice->pitch_mod_depth, 99U)
        && range(voice->amplitude_mod_depth, 99U)
        && range(voice->lfo_key_sync, 1U) && range(voice->lfo_waveform, 5U)
        && range(voice->pitch_mod_sensitivity, 7U)
        && range(voice->transpose, 48U));
}

static uint8_t decode_single_voice(const uint8_t *data, dx7_voice_t *voice)
{
    memset(voice, 0, sizeof(*voice));
    for (uint8_t dump_op = 0U; dump_op < DX7_OPERATOR_COUNT; ++dump_op)
    {
        const uint8_t *s = data + (size_t)dump_op * 21U;
        dx7_operator_t *d = &voice->operators[5U - dump_op];
        memcpy(d->rates, s, 4U); memcpy(d->levels, s + 4U, 4U);
        d->breakpoint = s[8]; d->left_depth = s[9]; d->right_depth = s[10];
        d->left_curve = s[11]; d->right_curve = s[12]; d->rate_scaling = s[13];
        d->amplitude_mod_sensitivity = s[14]; d->velocity_sensitivity = s[15];
        d->output_level = s[16]; d->oscillator_mode = s[17];
        d->coarse = s[18]; d->fine = s[19]; d->detune = (int8_t)s[20] - 7;
    }
    memcpy(voice->pitch_rates, data + 126U, 4U);
    memcpy(voice->pitch_levels, data + 130U, 4U);
    voice->algorithm = data[134]; voice->feedback = data[135];
    voice->oscillator_key_sync = data[136]; voice->lfo_speed = data[137];
    voice->lfo_delay = data[138]; voice->pitch_mod_depth = data[139];
    voice->amplitude_mod_depth = data[140]; voice->lfo_key_sync = data[141];
    voice->lfo_waveform = data[142]; voice->pitch_mod_sensitivity = data[143];
    voice->transpose = data[144]; memcpy(voice->name, data + 145U, 10U);
    return voice_valid(voice);
}

static uint8_t decode_packed_voice(const uint8_t *data, dx7_voice_t *voice)
{
    memset(voice, 0, sizeof(*voice));
    for (uint8_t dump_op = 0U; dump_op < DX7_OPERATOR_COUNT; ++dump_op)
    {
        const uint8_t *s = data + (size_t)dump_op * 17U;
        dx7_operator_t *d = &voice->operators[5U - dump_op];
        if ((s[11] & 0xf0U) != 0U || (s[12] & 0x80U) != 0U
                || (s[13] & 0xe0U) != 0U || (s[15] & 0xc0U) != 0U) return 0U;
        memcpy(d->rates, s, 4U); memcpy(d->levels, s + 4U, 4U);
        d->breakpoint = s[8]; d->left_depth = s[9]; d->right_depth = s[10];
        d->left_curve = s[11] & 3U; d->right_curve = (s[11] >> 2U) & 3U;
        d->rate_scaling = s[12] & 7U; d->detune = (int8_t)((s[12] >> 3U) & 15U) - 7;
        d->amplitude_mod_sensitivity = s[13] & 3U;
        d->velocity_sensitivity = (s[13] >> 2U) & 7U; d->output_level = s[14];
        d->oscillator_mode = s[15] & 1U; d->coarse = (s[15] >> 1U) & 31U;
        d->fine = s[16];
    }
    memcpy(voice->pitch_rates, data + 102U, 4U);
    memcpy(voice->pitch_levels, data + 106U, 4U);
    if ((data[111] & 0xf0U) != 0U || (data[116] & 0x80U) != 0U) return 0U;
    voice->algorithm = data[110]; voice->feedback = data[111] & 7U;
    voice->oscillator_key_sync = (data[111] >> 3U) & 1U;
    voice->lfo_speed = data[112]; voice->lfo_delay = data[113];
    voice->pitch_mod_depth = data[114]; voice->amplitude_mod_depth = data[115];
    voice->lfo_key_sync = data[116] & 1U;
    voice->lfo_waveform = (data[116] >> 1U) & 7U;
    voice->pitch_mod_sensitivity = (data[116] >> 4U) & 7U;
    voice->transpose = data[117]; memcpy(voice->name, data + 118U, 10U);
    return voice_valid(voice);
}

dx7_sysex_result_t dx7_sysex_parse(const uint8_t *bytes, size_t byte_count,
                                   dx7_voice_t *voices, size_t voice_capacity,
                                   size_t *out_voice_count)
{
    if (bytes == NULL || voices == NULL || out_voice_count == NULL)
        return DX7_SYSEX_INVALID_ARGUMENT;
    *out_voice_count = 0U;
    if (byte_count == 0U) return DX7_SYSEX_TRUNCATED;
    size_t pos = 0U;
    while (pos < byte_count)
    {
        if (byte_count - pos < 8U) return DX7_SYSEX_TRUNCATED;
        if (bytes[pos] != 0xf0U) return DX7_SYSEX_BAD_FRAMING;
        if (bytes[pos + 1U] != 0x43U) return DX7_SYSEX_BAD_MANUFACTURER;
        if ((bytes[pos + 2U] & 0xf0U) != 0U) return DX7_SYSEX_BAD_DEVICE;
        const uint8_t format = bytes[pos + 3U];
        const size_t data_length = ((size_t)bytes[pos + 4U] << 7U) | bytes[pos + 5U];
        const size_t expected = (format == 0x00U) ? DX7_SINGLE_DATA_BYTES
                              : (format == 0x09U) ? DX7_BANK_DATA_BYTES : 0U;
        if (expected == 0U) return DX7_SYSEX_UNSUPPORTED_FORMAT;
        if (data_length != expected) return DX7_SYSEX_BAD_LENGTH;
        const size_t message_length = data_length + 8U;
        if (message_length > byte_count - pos) return DX7_SYSEX_TRUNCATED;
        if (bytes[pos + message_length - 1U] != 0xf7U) return DX7_SYSEX_BAD_FRAMING;
        const uint8_t *data = bytes + pos + 6U;
        for (size_t i = 0U; i < data_length; ++i)
            if ((data[i] & 0x80U) != 0U) return DX7_SYSEX_BAD_DATA;
        if (!checksum_valid(data, data_length, data[data_length]))
            return DX7_SYSEX_BAD_CHECKSUM;
        const size_t message_voices = (format == 0x00U) ? 1U : DX7_BANK_VOICE_COUNT;
        if (message_voices > voice_capacity - *out_voice_count) return DX7_SYSEX_CAPACITY;
        for (size_t i = 0U; i < message_voices; ++i)
        {
            dx7_voice_t *voice = &voices[*out_voice_count + i];
            const uint8_t valid = (format == 0x00U)
                ? decode_single_voice(data, voice)
                : decode_packed_voice(data + i * DX7_PACKED_VOICE_BYTES, voice);
            if (!valid) return DX7_SYSEX_BAD_DATA;
        }
        *out_voice_count += message_voices;
        pos += message_length;
    }
    return (*out_voice_count != 0U) ? DX7_SYSEX_OK : DX7_SYSEX_BAD_LENGTH;
}
