#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Import/dx7_import.h"
#include "Import/dx7_sysex.h"
#include "Param/engine_model_catalog.h"
#include "Param/param_ids.h"
#include "Storage/persistent_control_codec.h"
#include "Storage/persistent_key_catalog.h"

typedef struct { uint8_t bytes[4096]; uint32_t pos; uint32_t size; } memory_io_t;

static uint8_t mem_write(void *context, const uint8_t *data, uint32_t length)
{
    memory_io_t *io = context;
    if (length > sizeof(io->bytes) - io->pos) return 0U;
    memcpy(io->bytes + io->pos, data, length); io->pos += length;
    if (io->pos > io->size) io->size = io->pos;
    return 1U;
}

static uint8_t mem_read(void *context, uint8_t *data, uint32_t length)
{
    memory_io_t *io = context;
    if (length > io->size - io->pos) return 0U;
    memcpy(data, io->bytes + io->pos, length); io->pos += length; return 1U;
}

static uint8_t mem_reset(void *context) { ((memory_io_t *)context)->pos = 0U; return 1U; }
static uint8_t mem_size(void *context, uint32_t *size)
{ *size = ((memory_io_t *)context)->size; return 1U; }

static uint8_t yamaha_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0U;
    for (size_t i = 0U; i < length; ++i) sum += data[i];
    return (uint8_t)(-sum) & 0x7fU;
}

static void make_single(uint8_t message[163])
{
    memset(message, 0, 163U);
    message[0] = 0xf0U; message[1] = 0x43U; message[2] = 3U;
    message[3] = 0U; message[4] = 1U; message[5] = 0x1bU;
    uint8_t *d = message + 6U;
    for (uint8_t dump_op = 0U; dump_op < 6U; ++dump_op)
    {
        uint8_t *op = d + dump_op * 21U;
        op[0] = (uint8_t)(90U + dump_op); op[1] = 80U; op[2] = 70U; op[3] = 60U;
        op[4] = 99U; op[5] = 80U; op[6] = 40U; op[7] = 0U;
        op[8] = 39U; op[9] = 12U; op[10] = 34U; op[11] = dump_op & 3U;
        op[12] = (dump_op + 1U) & 3U; op[13] = dump_op & 7U;
        op[14] = dump_op & 3U; op[15] = (dump_op + 1U) & 7U;
        op[16] = (uint8_t)(99U - dump_op); op[17] = dump_op & 1U;
        op[18] = (uint8_t)(dump_op + 1U); op[19] = (uint8_t)(dump_op * 10U);
        op[20] = (uint8_t)(dump_op + 4U); /* detune -3..+2 */
    }
    d[126] = 91U; d[127] = 72U; d[128] = 53U; d[129] = 34U;
    d[130] = 11U; d[131] = 32U; d[132] = 75U; d[133] = 50U;
    d[134] = 17U; d[135] = 6U; d[136] = 1U; d[137] = 65U;
    d[138] = 44U; d[139] = 73U; d[140] = 81U; d[141] = 1U;
    d[142] = 4U; d[143] = 6U; d[144] = 31U;
    memcpy(d + 145U, "TEST FM-01", 10U);
    message[161] = yamaha_checksum(d, 155U); message[162] = 0xf7U;
}

static void pack_voice(const uint8_t single[163], uint8_t packed[128])
{
    const uint8_t *s = single + 6U;
    memset(packed, 0, 128U);
    for (uint8_t op = 0U; op < 6U; ++op)
    {
        const uint8_t *sop = s + op * 21U; uint8_t *d = packed + op * 17U;
        memcpy(d, sop, 11U); d[11] = sop[11] | (uint8_t)(sop[12] << 2U);
        d[12] = sop[13] | (uint8_t)(sop[20] << 3U);
        d[13] = sop[14] | (uint8_t)(sop[15] << 2U); d[14] = sop[16];
        d[15] = sop[17] | (uint8_t)(sop[18] << 1U); d[16] = sop[19];
    }
    memcpy(packed + 102U, s + 126U, 8U); packed[110] = s[134];
    packed[111] = s[135] | (uint8_t)(s[136] << 3U);
    memcpy(packed + 112U, s + 137U, 4U);
    packed[116] = s[141] | (uint8_t)(s[142] << 1U) | (uint8_t)(s[143] << 4U);
    packed[117] = s[144]; memcpy(packed + 118U, s + 145U, 10U);
}

static void make_bank(const uint8_t single[163], uint8_t bank[4104])
{
    memset(bank, 0, 4104U); bank[0] = 0xf0U; bank[1] = 0x43U;
    bank[3] = 9U; bank[4] = 0x20U;
    for (uint8_t i = 0U; i < 32U; ++i) pack_voice(single, bank + 6U + i * 128U);
    bank[4102] = yamaha_checksum(bank + 6U, 4096U); bank[4103] = 0xf7U;
}

int main(void)
{
    uint8_t single[163]; make_single(single);
    dx7_voice_t voices[32]; size_t count = 0U;
    assert(dx7_sysex_parse(single, sizeof(single), voices, 32U, &count) == DX7_SYSEX_OK);
    assert(count == 1U && voices[0].operators[0].coarse == 6U);
    assert(voices[0].operators[5].coarse == 1U); /* explicit OP6..OP1 reversal */
    assert(voices[0].operators[0].detune == 2 && voices[0].operators[5].detune == -3);
    assert(voices[0].algorithm == 17U && voices[0].feedback == 6U);
    assert(voices[0].transpose == 31U && voices[0].pitch_rates[2] == 53U);
    assert(voices[0].lfo_speed == 65U && voices[0].lfo_waveform == 4U);
    assert(voices[0].pitch_mod_depth == 73U && voices[0].amplitude_mod_depth == 81U);

    uint8_t damaged[163]; memcpy(damaged, single, sizeof(damaged)); damaged[20] ^= 1U;
    assert(dx7_sysex_parse(damaged, sizeof(damaged), voices, 32U, &count)
        == DX7_SYSEX_BAD_CHECKSUM);
    assert(dx7_sysex_parse(single, sizeof(single) - 1U, voices, 32U, &count)
        == DX7_SYSEX_TRUNCATED);
    memcpy(damaged, single, sizeof(damaged)); damaged[6U + 18U] = 32U;
    damaged[161] = yamaha_checksum(damaged + 6U, 155U);
    assert(dx7_sysex_parse(damaged, sizeof(damaged), voices, 32U, &count)
        == DX7_SYSEX_BAD_DATA);

    uint8_t bank[4104]; make_bank(single, bank);
    assert(dx7_sysex_parse(bank, sizeof(bank), voices, 32U, &count) == DX7_SYSEX_OK);
    assert(count == 32U && voices[31].operators[0].fine == 50U);
    assert(dx7_sysex_parse(bank, sizeof(bank) - 10U, voices, 32U, &count)
        == DX7_SYSEX_TRUNCATED);
    bank[6U + 11U] |= 0x10U; bank[4102] = yamaha_checksum(bank + 6U, 4096U);
    assert(dx7_sysex_parse(bank, sizeof(bank), voices, 32U, &count) == DX7_SYSEX_BAD_DATA);

    assert(dx7_sysex_parse(single, sizeof(single), voices, 32U, &count) == DX7_SYSEX_OK);
    persist_control_patch_t patch;
    assert(dx7_import_voice(&voices[0], 0U, &patch) == DX7_IMPORT_OK);
    assert(patch.fm.base.operators[0].mode == 1U && patch.fm.base.operators[0].fine == 50U);
    assert(patch.fm.base.operators[0].detune == 2 && patch.fm.base.operators[5].detune == -3);
    assert(patch.fm.base.transpose_cents == 3100U && patch.fm.base.pitch_levels[2] == 75U);
    assert(patch.modulation.routes[0].enabled == 1U); /* PMD/PMS -> transpose */
    assert(patch.modulation.routes[1].enabled == 1U); /* AMD/AMS -> OP level */
    assert(patch.modulation.lfos[0].rate < 0.0f);      /* free Hz domain */
    assert(dx7_import_lfo_frequency_hz(99U) > dx7_import_lfo_frequency_hz(50U));
    assert(dx7_import_pitch_depth_semitones(99U, 7U) == 12.0f);

    dx7_voice_t zero_mod = voices[0];
    zero_mod.pitch_mod_depth = 0U; zero_mod.amplitude_mod_depth = 0U;
    assert(dx7_import_voice(&zero_mod, 0U, &patch) == DX7_IMPORT_OK);
    for (uint8_t i = 0U; i < 8U; ++i) assert(patch.modulation.routes[i].enabled == 0U);

    assert(dx7_import_voice(&voices[0], 0U, &patch) == DX7_IMPORT_OK);
    memory_io_t io = {{0}, 0U, 0U}; uint32_t encoded = 0U;
    const persist_codec_sink_t sink = {mem_write, &io};
    assert(persist_codec_encode_patch(&patch, &sink, &encoded) == PERSIST_CODEC_OK);
    const persist_codec_source_t source = {mem_read, mem_reset, mem_size, &io};
    persist_codec_patch_staging_t decoded;
    assert(persist_codec_decode_patch(&source, &decoded) == PERSIST_CODEC_OK);
    assert(memcmp(&patch.fm, &decoded.patch.fm, sizeof(patch.fm)) == 0);
    assert(memcmp(&patch.modulation, &decoded.patch.modulation,
        sizeof(patch.modulation)) == 0);
    return 0;
}
