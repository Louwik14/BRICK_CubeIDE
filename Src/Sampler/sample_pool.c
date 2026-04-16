/**
 * @file sample_pool.c
 * @brief Module applicatif sample_pool.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à sample_pool.
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
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "Sampler/sample_pool.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/wav_parser.h"
#include "Storage/sd_access_gate.h"

#include "ff.h"

#define SAMPLE_POOL_HAS_FATFS 1

#ifndef SAMPLE_POOL_HAS_FATFS
#define SAMPLE_POOL_HAS_FATFS 0
#endif

#define SAMPLE_POOL_DEBUG 1

#if SAMPLE_POOL_DEBUG
#define SAMPLE_POOL_LOG(...) printf(__VA_ARGS__)
#else
#define SAMPLE_POOL_LOG(...)
#endif

SDRAM_SAMPLES static sample_desc_t g_sample_pool[SAMPLE_POOL_SIZE];

#define SAMPLE_POOL_RESIDENT_SLOTS (32U)
#define SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE (65536U)
SDRAM_SAMPLES static float g_sample_pool_data[SAMPLE_POOL_RESIDENT_SLOTS][SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE * 2U];

static CTRL_STATE int16_t g_sample_slot_by_sample[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_sample_slot_in_use[SAMPLE_POOL_RESIDENT_SLOTS];
static CTRL_STATE sample_pool_load_error_t g_sample_pool_last_load_error = SAMPLE_POOL_LOAD_OK;
static CTRL_STATE uint8_t g_sample_pool_last_sd_error_code;

static void sample_pool_set_error(sample_pool_load_error_t error, FRESULT fr)
{
    g_sample_pool_last_load_error = error;
    g_sample_pool_last_sd_error_code = (uint8_t)fr;
}

/**
 * @brief Point d'entrée sample_pool_pcm24_to_float.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_pcm24_to_float.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float sample_pool_pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

/**
 * @brief Point d'entrée sample_pool_pcm32_to_float.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_pcm32_to_float.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float sample_pool_pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

/**
 * @brief Point d'entrée sample_pool_release_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_release_slot.
 *
 * @param sample_id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void sample_pool_release_slot(uint16_t sample_id)
{
    if(sample_id >= SAMPLE_POOL_SIZE)
        return;

    const int16_t slot = g_sample_slot_by_sample[sample_id];
    if((slot >= 0) && ((uint32_t)slot < SAMPLE_POOL_RESIDENT_SLOTS))
        g_sample_slot_in_use[(uint32_t)slot] = 0U;

    g_sample_slot_by_sample[sample_id] = -1;
}

/**
 * @brief Point d'entrée sample_pool_alloc_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_alloc_slot.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int16_t sample_pool_alloc_slot(void)
{
    for(uint32_t i = 0U; i < SAMPLE_POOL_RESIDENT_SLOTS; i++)
    {
        if(g_sample_slot_in_use[i] == 0U)
        {
            g_sample_slot_in_use[i] = 1U;
            return (int16_t)i;
        }
    }

    return -1;
}

#if SAMPLE_POOL_HAS_FATFS
/**
 * @brief Point d'entrée sample_pool_load_full_data.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_load_full_data.
 *
 * @param fp Paramètre d'entrée de l'API.
 * @param slot Paramètre d'entrée de l'API.
 * @param info Paramètre d'entrée de l'API.
 * @param data_size_aligned Paramètre d'entrée de l'API.
 * @param desc Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool sample_pool_load_full_data(FIL *fp,
                                       uint16_t slot,
                                       const wav_info_t *info,
                                       uint32_t data_size_aligned,
                                       sample_desc_t *desc)
{
    uint8_t io_buf[512U * 8U];
    const uint32_t bytes_per_frame = info->block_align;
    const uint32_t total_frames = data_size_aligned / bytes_per_frame;
    uint32_t loaded_frames = 0U;

    if(total_frames == 0U)
        return false;

    if(total_frames > SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] sample too large slot=%u frames=%lu max=%u\n",
                        (unsigned)slot,
                        (unsigned long)total_frames,
                        (unsigned)SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE);
        sample_pool_set_error(SAMPLE_POOL_LOAD_MEMORY_LIMIT, FR_OK);
        return false;
    }

    if(f_lseek(fp, info->data_offset) != FR_OK)
    {
        sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, FR_DISK_ERR);
        return false;
    }

    while(loaded_frames < total_frames)
    {
        const uint32_t frames_left = total_frames - loaded_frames;
        const uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        const uint32_t chunk_bytes = chunk_frames * bytes_per_frame;

        UINT br = 0U;
        const FRESULT fr = f_read(fp, io_buf, chunk_bytes, &br);
        if((fr != FR_OK) || (br == 0U))
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, fr);
            break;
        }

        const uint32_t ready_frames = br / bytes_per_frame;
        for(uint32_t i = 0U; i < ready_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bytes_per_frame];
            const uint32_t out = (loaded_frames + i) * 2U;

            if(info->bits_per_sample == 24U)
            {
                g_sample_pool_data[slot][out] = sample_pool_pcm24_to_float(&frame[0]);
                g_sample_pool_data[slot][out + 1U] = sample_pool_pcm24_to_float(&frame[3]);
            }
            else
            {
                g_sample_pool_data[slot][out] = sample_pool_pcm32_to_float(&frame[0]);
                g_sample_pool_data[slot][out + 1U] = sample_pool_pcm32_to_float(&frame[4]);
            }
        }

        loaded_frames += ready_frames;

        if(ready_frames < chunk_frames)
            break;
    }

    if(loaded_frames != total_frames)
    {
        if (g_sample_pool_last_load_error == SAMPLE_POOL_LOAD_OK)
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, FR_DISK_ERR);
        }
        return false;
    }

    desc->data = &g_sample_pool_data[slot][0];
    return true;
}
#endif

#if SAMPLE_POOL_HAS_FATFS
static FATFS g_sample_pool_fs;
static uint8_t g_sample_pool_fs_mounted;
#endif

/**
 * @brief Point d'entrée sample_pool_clear_entry.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_clear_entry.
 *
 * @param desc Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void sample_pool_clear_entry(sample_desc_t *desc)
{
    if(desc == NULL)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->data = NULL;
    desc->valid = 0U;
}

void sample_pool_clear(uint16_t id)
{
    if(id >= SAMPLE_POOL_SIZE)
        return;

    sample_pool_release_slot(id);
    sample_pool_clear_entry(&g_sample_pool[id]);
}

/**
 * @brief Point d'entrée sample_pool_trim_path_copy.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_trim_path_copy.
 *
 * @param dst Paramètre d'entrée de l'API.
 * @param dst_size Paramètre d'entrée de l'API.
 * @param src Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static size_t sample_pool_trim_path_copy(char *dst, size_t dst_size, const char *src)
{
    size_t start = 0U;
    size_t end;

    if((dst == NULL) || (dst_size == 0U) || (src == NULL))
        return 0U;

    end = strlen(src);

    while((start < end) && (isspace((unsigned char)src[start]) != 0))
        start++;

    while((end > start) && (isspace((unsigned char)src[end - 1U]) != 0))
        end--;

    const size_t trimmed_len = end - start;
    if(trimmed_len >= dst_size)
        return 0U;

    memcpy(dst, &src[start], trimmed_len);
    dst[trimmed_len] = '\0';

    return trimmed_len;
}

/**
 * @brief Point d'entrée sample_pool_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void sample_pool_init(void)
{
    for(uint32_t i = 0U; i < SAMPLE_POOL_SIZE; i++)
    {
        sample_pool_clear_entry(&g_sample_pool[i]);
        g_sample_slot_by_sample[i] = -1;
    }

    memset(g_sample_slot_in_use, 0, sizeof(g_sample_slot_in_use));

#if SAMPLE_POOL_HAS_FATFS
    g_sample_pool_fs_mounted = 0U;
#endif
    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);
}

/**
 * @brief Point d'entrée sample_pool_load.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_pool_load.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param path Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool sample_pool_load(uint16_t id, const char *path)
{
    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);

    if(id >= SAMPLE_POOL_SIZE)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] invalid id=%u\n", (unsigned)id);
        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_ID, FR_INVALID_PARAMETER);
        return false;
    }

    sample_desc_t *desc = &g_sample_pool[id];
    sample_pool_release_slot(id);
    sample_pool_clear_entry(desc);

    if((path == NULL) || (path[0] == '\0'))
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u invalid path\n", (unsigned)id);
        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, FR_INVALID_NAME);
        return false;
    }

    const size_t raw_path_len = strlen(path);
    if(raw_path_len >= SAMPLE_POOL_PATH_MAX)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u path too long (%lu >= %u)\n",
                        (unsigned)id,
                        (unsigned long)raw_path_len,
                        (unsigned)SAMPLE_POOL_PATH_MAX);
        sample_pool_set_error(SAMPLE_POOL_LOAD_PATH_TOO_LONG, FR_INVALID_NAME);
        return false;
    }

    const size_t path_len = sample_pool_trim_path_copy(desc->path,
                                                       sizeof(desc->path),
                                                       path);
    if(path_len == 0U)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u path invalid/empty after trim\n",
                        (unsigned)id);
        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, FR_INVALID_NAME);
        return false;
    }

    const int16_t slot = sample_pool_alloc_slot();
    if(slot < 0)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] no free resident slots for id=%u\n", (unsigned)id);
        sample_pool_set_error(SAMPLE_POOL_LOAD_NO_FREE_SLOT, FR_NOT_ENOUGH_CORE);
        return false;
    }

#if SAMPLE_POOL_HAS_FATFS
    uint8_t sd_gate_held = 0U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] sd gate refused for id=%u path=%s\n", (unsigned)id, desc->path);
        sample_pool_set_error(SAMPLE_POOL_LOAD_SD_GATE_REFUSED, FR_TIMEOUT);
        g_sample_slot_in_use[(uint32_t)slot] = 0U;
        return false;
    }
    sd_gate_held = 1U;

    if(g_sample_pool_fs_mounted == 0U)
    {
        sd_access_trace_begin("sample_pool_f_mount");
        const FRESULT mount_fr = f_mount(&g_sample_pool_fs, "0:", 1U);
        sd_access_trace_end("sample_pool_f_mount", (int)mount_fr, 0U);
        if(mount_fr != FR_OK)
        {
            SAMPLE_POOL_LOG("[SAMPLE_POOL] f_mount failed id=%u path=%s fr=%d\n",
                            (unsigned)id,
                            desc->path,
                            (int)mount_fr);
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_MOUNT_FAIL, mount_fr);
            sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
            g_sample_slot_in_use[(uint32_t)slot] = 0U;
            return false;
        }

        g_sample_pool_fs_mounted = 1U;
    }

    FIL fp;
    sd_access_trace_begin("sample_pool_f_open");
    const FRESULT open_fr = f_open(&fp, desc->path, FA_READ);
    sd_access_trace_end("sample_pool_f_open", (int)open_fr, 0U);
    if(open_fr != FR_OK)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] f_open failed id=%u path=%s fr=%d\n",
                        (unsigned)id,
                        desc->path,
                        (int)open_fr);
        if (open_fr == FR_NO_FILE)
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_FILE_NOT_FOUND, open_fr);
        }
        else
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_OPEN_FAIL, open_fr);
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_sample_slot_in_use[(uint32_t)slot] = 0U;
        return false;
    }

    wav_info_t info;
    memset(&info, 0, sizeof(info));

    if(!wav_parser_parse_info(&fp, &info))
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] wav parse failed id=%u path=%s\n",
                        (unsigned)id,
                        desc->path);
        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_PARSE_FAIL, FR_INVALID_OBJECT);
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_sample_slot_in_use[(uint32_t)slot] = 0U;
        return false;
    }

    if((info.audio_format != 1U) ||
       (info.sample_rate != 48000U) ||
       (info.channels != 2U) ||
       ((info.bits_per_sample != 24U) && (info.bits_per_sample != 32U)) ||
       (info.block_align == 0U) ||
       (info.byte_rate != (info.sample_rate * info.block_align)))
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] wav unsupported id=%u path=%s fmt=%u sr=%lu ch=%u bits=%u align=%u br=%lu\n",
                        (unsigned)id,
                        desc->path,
                        (unsigned)info.audio_format,
                        (unsigned long)info.sample_rate,
                        (unsigned)info.channels,
                        (unsigned)info.bits_per_sample,
                        (unsigned)info.block_align,
                        (unsigned long)info.byte_rate);
        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT, FR_INVALID_PARAMETER);
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_sample_slot_in_use[(uint32_t)slot] = 0U;
        return false;
    }

    const uint32_t bytes_per_frame = (uint32_t)info.block_align;
    const uint32_t data_size_aligned = info.data_size - (info.data_size % bytes_per_frame);

    desc->data_offset = info.data_offset;
    desc->length_frames = data_size_aligned / bytes_per_frame;
    desc->bytes_per_frame = bytes_per_frame;
    desc->sample_rate = info.sample_rate;
    desc->channels = info.channels;
    desc->bits_per_sample = info.bits_per_sample;

    if(!sample_pool_load_full_data(&fp, (uint16_t)slot, &info, data_size_aligned, desc))
    {
        if (g_sample_pool_last_load_error == SAMPLE_POOL_LOAD_OK)
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, FR_DISK_ERR);
        }
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        g_sample_slot_in_use[(uint32_t)slot] = 0U;
        return false;
    }

    desc->valid = 1U;
    g_sample_slot_by_sample[id] = slot;
    (void)f_close(&fp);
    if (sd_gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    }

    SAMPLE_POOL_LOG("[SAMPLE_POOL] loaded id=%u slot=%d path=%s frames=%lu\n",
                    (unsigned)id,
                    (int)slot,
                    desc->path,
                    (unsigned long)desc->length_frames);
    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);

    return true;
#else
    (void)path;
    g_sample_slot_in_use[(uint32_t)slot] = 0U;
    SAMPLE_POOL_LOG("[SAMPLE_POOL] FatFs unavailable in this build\n");
    return false;
#endif
}

const sample_desc_t *sample_pool_get(uint16_t id)
{
    if(id >= SAMPLE_POOL_SIZE)
        return NULL;

    return &g_sample_pool[id];
}

bool sample_pool_is_loaded(uint16_t id)
{
    const sample_desc_t *const desc = sample_pool_get(id);
    return ((desc != NULL) && (desc->valid != 0U) && (desc->data != NULL) && (desc->length_frames != 0U));
}

sample_pool_slot_state_t sample_pool_get_state(uint16_t id)
{
    const sample_desc_t *const desc = sample_pool_get(id);
    if (desc == NULL)
    {
        return SAMPLE_POOL_SLOT_EMPTY;
    }

    if ((desc->valid != 0U) && (desc->data != NULL) && (desc->length_frames != 0U))
    {
        return SAMPLE_POOL_SLOT_LOADED;
    }

    return (desc->path[0] != '\0') ? SAMPLE_POOL_SLOT_MISSING : SAMPLE_POOL_SLOT_EMPTY;
}

sample_pool_load_error_t sample_pool_get_last_load_error(void)
{
    return g_sample_pool_last_load_error;
}

uint8_t sample_pool_get_last_sd_error_code(void)
{
    return g_sample_pool_last_sd_error_code;
}
