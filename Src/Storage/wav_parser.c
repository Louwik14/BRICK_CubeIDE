/**
 * @file wav_parser.c
 * @brief Module applicatif wav_parser.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à wav_parser.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 */

#include "wav_parser.h"
#include <string.h>

#include "Platform/memory_layout.h"

#define WAV_PARSER_CRC_IO_BYTES (4096U)
STORAGE_STATE_SDRAM static uint8_t g_wav_parser_crc_io[WAV_PARSER_CRC_IO_BYTES];

#define WAV_FMT_PCM        1U
#define WAV_FMT_IEEE_FLOAT 3U
#define WAV_FMT_EXTENSIBLE 65534U
#define WAV_CANONICAL_DATA_OFFSET 512U

static uint16_t le16(const uint8_t *p);
static uint32_t le32(const uint8_t *p);

static wav_sample_encoding_t wav_encoding_from_fmt(uint16_t audio_format,
                                                   const uint8_t *fmt,
                                                   uint32_t chunk_size,
                                                   uint16_t *out_valid_bits)
{
    static const uint8_t pcm_guid_le[16] = {
        0x01U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U,
        0x10U, 0x00U,
        0x80U, 0x00U,
        0x00U, 0xAAU, 0x00U, 0x38U, 0x9BU, 0x71U
    };
    static const uint8_t float_guid_le[16] = {
        0x03U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U,
        0x10U, 0x00U,
        0x80U, 0x00U,
        0x00U, 0xAAU, 0x00U, 0x38U, 0x9BU, 0x71U
    };

    if (out_valid_bits != 0) *out_valid_bits = 0U;
    if(audio_format == WAV_FMT_PCM) return WAV_SAMPLE_ENCODING_PCM_INTEGER;
    if(audio_format == WAV_FMT_IEEE_FLOAT) return WAV_SAMPLE_ENCODING_IEEE_FLOAT;

    if(audio_format != WAV_FMT_EXTENSIBLE) return WAV_SAMPLE_ENCODING_INVALID;

    if((fmt == 0) || (chunk_size < 40U) || (le16(&fmt[16]) < 22U)
        || ((uint32_t)le16(&fmt[16]) > (chunk_size - 18U)))
        return WAV_SAMPLE_ENCODING_INVALID;

    if (out_valid_bits != 0) *out_valid_bits = le16(&fmt[18]);
    if (memcmp(&fmt[24], pcm_guid_le, sizeof(pcm_guid_le)) == 0)
        return WAV_SAMPLE_ENCODING_PCM_INTEGER;
    if (memcmp(&fmt[24], float_guid_le, sizeof(float_guid_le)) == 0)
        return WAV_SAMPLE_ENCODING_IEEE_FLOAT;
    return WAV_SAMPLE_ENCODING_INVALID;
}

uint8_t wav_parser_format_supported(const wav_info_t *info)
{
    if ((info == 0) || (info->channels < 1U) || (info->channels > 2U)
        || (info->sample_rate == 0U) || (info->data_size == 0U)
        || (info->block_align == 0U) || ((info->bits_per_sample & 7U) != 0U)
        || ((info->data_size % info->block_align) != 0U))
        return 0U;
    const uint16_t expected_align = (uint16_t)(info->channels
        * (info->bits_per_sample / 8U));
    const uint64_t expected_byte_rate =
        (uint64_t)info->sample_rate * (uint64_t)expected_align;
    if ((expected_align == 0U) || (info->block_align != expected_align)
        || (expected_byte_rate > UINT32_MAX)
        || (info->byte_rate != (uint32_t)expected_byte_rate))
        return 0U;
    if (info->encoding == WAV_SAMPLE_ENCODING_PCM_INTEGER)
    {
        if ((info->valid_bits_per_sample == 0U)
            || (info->valid_bits_per_sample > info->bits_per_sample)) return 0U;
        return ((info->bits_per_sample == 16U) || (info->bits_per_sample == 24U)
                || (info->bits_per_sample == 32U)) ? 1U : 0U;
    }
    if (info->encoding == WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        return ((info->bits_per_sample == 32U)
                && (info->valid_bits_per_sample == 32U)) ? 1U : 0U;
    return 0U;
}

uint8_t wav_parser_is_canonical_brick_float(const wav_info_t *info)
{
    if ((wav_parser_format_supported(info) == 0U)
        || (info->audio_format != WAV_FMT_IEEE_FLOAT)
        || (info->encoding != WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        || (info->fmt_chunk_size != 16U)
        || (info->channels != 2U) || (info->sample_rate != 48000U)
        || (info->bits_per_sample != 32U) || (info->valid_bits_per_sample != 32U)
        || (info->block_align != 8U) || (info->byte_rate != 384000U)
        || (info->data_offset != WAV_CANONICAL_DATA_OFFSET)
        || (info->has_fact == 0U)
        || (info->fact_sample_length != (info->data_size / info->block_align)))
        return 0U;
    return 1U;
}

/**
 * @brief Point d'entrée le16.
 *
 * Rôle:
 * - Exécuter le traitement associé à le16.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/**
 * @brief Point d'entrée le32.
 *
 * Rôle:
 * - Exécuter le traitement associé à le32.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool wav_find_chunks(FIL *fp,
                            wav_info_t *info,
                            uint16_t *audio_format,
                            uint16_t *channels,
                            uint32_t *sample_rate,
                            uint16_t *bits_per_sample,
                            uint16_t *block_align,
                            uint32_t *byte_rate,
                            uint32_t *data_offset,
                            uint32_t *data_size)
{
    uint8_t riff[12];
    UINT br;
    wav_sample_encoding_t encoding = WAV_SAMPLE_ENCODING_INVALID;
    uint16_t valid_bits = 0U;
    uint32_t fmt_chunk_size = 0U;
    uint32_t fact_sample_length = 0U;
    uint8_t has_fact = 0U;

    if((f_read(fp, riff, 12, &br) != FR_OK) || br != 12)
        return false;

    if(memcmp(&riff[0], "RIFF", 4) != 0 || memcmp(&riff[8], "WAVE", 4) != 0)
        return false;
    const FSIZE_t file_size = f_size(fp);
    const uint64_t riff_end = (uint64_t)le32(&riff[4]) + 8ULL;
    if ((riff_end < 12ULL) || (riff_end > (uint64_t)file_size)) return false;
    const FSIZE_t parse_limit = (FSIZE_t)riff_end;

    *audio_format = 0;
    *channels = 0;
    *sample_rate = 0;
    *bits_per_sample = 0;
    *block_align = 0;
    *byte_rate = 0;
    *data_offset = 0;
    *data_size = 0;

    while(((uint64_t)f_tell(fp) + 8ULL) <= (uint64_t)parse_limit)
    {
        uint8_t hdr[8];
        uint32_t chunk_size;

        if((f_read(fp, hdr, 8, &br) != FR_OK) || br != 8)
            return false;

        chunk_size = le32(&hdr[4]);
        const FSIZE_t payload_offset = f_tell(fp);
        if ((payload_offset > parse_limit)
            || ((uint64_t)chunk_size > ((uint64_t)parse_limit - payload_offset)))
            return false;

        if(memcmp(&hdr[0], "fmt ", 4) == 0)
        {
            uint8_t fmt[40] = {0};
            uint32_t to_read = chunk_size > sizeof(fmt) ? sizeof(fmt) : chunk_size;

            if((f_read(fp, fmt, to_read, &br) != FR_OK) || br != to_read)
                return false;

            if(to_read < 16)
                return false;

            *audio_format   = le16(&fmt[0]);
            *channels       = le16(&fmt[2]);
            *sample_rate    = le32(&fmt[4]);
            *byte_rate      = le32(&fmt[8]);
            *block_align    = le16(&fmt[12]);
            *bits_per_sample= le16(&fmt[14]);
            fmt_chunk_size = chunk_size;

            if(chunk_size > to_read)
            {
                if(f_lseek(fp, f_tell(fp) + (chunk_size - to_read)) != FR_OK)
                    return false;
            }

            encoding = wav_encoding_from_fmt(*audio_format, fmt, chunk_size,
                                             &valid_bits);
            if(encoding == WAV_SAMPLE_ENCODING_INVALID)
                return false;
            if (valid_bits == 0U) valid_bits = *bits_per_sample;
        }
        else if(memcmp(&hdr[0], "fact", 4) == 0)
        {
            uint8_t fact[4];
            if (chunk_size < sizeof(fact)
                || (f_read(fp, fact, sizeof(fact), &br) != FR_OK)
                || (br != sizeof(fact))) return false;
            fact_sample_length = le32(fact);
            has_fact = 1U;
            if (chunk_size > sizeof(fact)
                && (f_lseek(fp, f_tell(fp) + chunk_size - sizeof(fact)) != FR_OK))
                return false;
        }
        else if(memcmp(&hdr[0], "data", 4) == 0)
        {
            *data_offset = f_tell(fp);
            *data_size   = chunk_size;

            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }
        else
        {
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }

        if(chunk_size & 1)
        {
            if (f_tell(fp) >= parse_limit) return false;
            if(f_lseek(fp, f_tell(fp) + 1) != FR_OK)
                return false;
        }

        if(*audio_format && *data_size)
            break;
    }

    if(info)
    {
        info->audio_format   = *audio_format;
        info->encoding        = encoding;
        info->sample_rate     = *sample_rate;
        info->byte_rate       = *byte_rate;
        info->channels        = *channels;
        info->block_align     = *block_align;
        info->bits_per_sample = *bits_per_sample;
        info->valid_bits_per_sample = valid_bits;
        info->fmt_chunk_size  = fmt_chunk_size;
        info->data_offset     = *data_offset;
        info->data_size       = *data_size;
        info->fact_sample_length = fact_sample_length;
        info->has_fact        = has_fact;
    }

    if (encoding == WAV_SAMPLE_ENCODING_INVALID) return false;
    wav_info_t parsed = {
        .audio_format = *audio_format,
        .encoding = encoding,
        .sample_rate = *sample_rate,
        .byte_rate = *byte_rate,
        .channels = *channels,
        .block_align = *block_align,
        .bits_per_sample = *bits_per_sample,
        .valid_bits_per_sample = valid_bits,
        .fmt_chunk_size = fmt_chunk_size,
        .data_offset = *data_offset,
        .data_size = *data_size,
        .fact_sample_length = fact_sample_length,
        .has_fact = has_fact,
    };
    return wav_parser_format_supported(&parsed) != 0U;
}

/**
 * @brief Point d'entrée wav_parser_parse_info.
 *
 * Rôle:
 * - Exécuter le traitement associé à wav_parser_parse_info.
 *
 * @param fp Paramètre d'entrée de l'API.
 * @param info Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool wav_parser_parse_info(FIL *fp, wav_info_t *info)
{
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t byte_rate;
    uint32_t data_offset;
    uint32_t data_size;

    if(!fp)
        return false;

    if(f_lseek(fp, 0) != FR_OK)
        return false;

    return wav_find_chunks(fp,
                           info,
                           &audio_format,
                           &channels,
                           &sample_rate,
                           &bits_per_sample,
                           &block_align,
                           &byte_rate,
                           &data_offset,
                           &data_size);
}

uint8_t wav_parser_crc32_file(FIL *fp, uint32_t *out_crc32)
{
    if ((fp == NULL) || (out_crc32 == NULL) || (f_lseek(fp, 0U) != FR_OK))
        return 0U;

    uint32_t crc = 0xFFFFFFFFUL;
    for (;;)
    {
        UINT read = 0U;
        if ((f_read(fp, g_wav_parser_crc_io, sizeof(g_wav_parser_crc_io), &read)
             != FR_OK))
            return 0U;
        if (read == 0U) break;
        for (UINT i = 0U; i < read; ++i)
        {
            crc ^= g_wav_parser_crc_io[i];
            for (uint8_t bit = 0U; bit < 8U; ++bit)
            {
                const uint32_t mask = 0U - (crc & 1U);
                crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
            }
        }
    }
    *out_crc32 = ~crc;
    return (f_lseek(fp, 0U) == FR_OK) ? 1U : 0U;
}
