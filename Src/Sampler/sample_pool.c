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
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_audio_stream.h"
#include "Storage/wav_parser.h"
#include "Storage/sd_access_gate.h"
#include "Audio/live_recorder_config.h"

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
/*
 * Reuse the recorder buffer shrink directly in the resident sample arena.
 * The pool keeps its slot topology and gains the recorder's freed frame budget.
 */
#define SAMPLE_POOL_TOTAL_FRAMES ((SAMPLE_POOL_RESIDENT_SLOTS * 65536U) + LIVE_RECORDER_MAX_FRAMES)
SDRAM_SAMPLES static float g_sample_pool_data[SAMPLE_POOL_TOTAL_FRAMES * 2U];

static CTRL_STATE int16_t g_sample_slot_by_sample[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_sample_slot_in_use[SAMPLE_POOL_RESIDENT_SLOTS];
static CTRL_STATE uint32_t g_sample_region_start[SAMPLE_POOL_SIZE];
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
    g_sample_region_start[sample_id] = 0U;
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

static uint32_t sample_pool_region_end(uint32_t sample_id)
{
    return g_sample_region_start[sample_id] + g_sample_pool[sample_id].length_frames;
}

static int16_t sample_pool_alloc_region(uint32_t frames, uint32_t *start_frame)
{
    uint32_t cursor = 0U;

    if((frames == 0U) || (frames > SAMPLE_POOL_TOTAL_FRAMES) || (start_frame == NULL))
        return -1;

    while(cursor + frames <= SAMPLE_POOL_TOTAL_FRAMES)
    {
        uint32_t next_start = SAMPLE_POOL_TOTAL_FRAMES;
        int16_t next_sample = -1;

        for(uint32_t i = 0U; i < SAMPLE_POOL_SIZE; i++)
        {
            const sample_desc_t *const desc = &g_sample_pool[i];
            if((g_sample_slot_by_sample[i] < 0) || (desc->valid == 0U) || (desc->data == NULL) || (desc->length_frames == 0U))
                continue;

            const uint32_t region_start = g_sample_region_start[i];
            if((region_start >= cursor) && (region_start < next_start))
            {
                next_start = region_start;
                next_sample = (int16_t)i;
            }
        }

        if(next_sample < 0)
        {
            *start_frame = cursor;
            return 0;
        }

        if(cursor + frames <= next_start)
        {
            *start_frame = cursor;
            return 0;
        }

        cursor = sample_pool_region_end((uint32_t)next_sample);
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
                                       uint16_t sample_id,
                                       const wav_info_t *info,
                                       uint32_t data_size_aligned,
                                       sample_desc_t *desc)
{
    wav_audio_stream_t stream;
    const uint32_t source_frames = data_size_aligned / info->block_align;
    const uint32_t target_rate = 48000U;
    const uint32_t total_frames = (uint32_t)(((uint64_t)source_frames * (uint64_t)target_rate
                                              + (uint64_t)info->sample_rate - 1U)
                                             / (uint64_t)info->sample_rate);
    uint32_t loaded_frames = 0U;
    uint32_t start_frame = 0U;

    if((source_frames == 0U) || (total_frames == 0U))
        return false;

    if(sample_pool_alloc_region(total_frames, &start_frame) != 0)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] pool full sample=%u slot=%u frames=%lu total=%u\n",
                        (unsigned)sample_id,
                        (unsigned)slot,
                        (unsigned long)total_frames,
                        (unsigned)SAMPLE_POOL_TOTAL_FRAMES);
        sample_pool_set_error(SAMPLE_POOL_LOAD_MEMORY_LIMIT, FR_OK);
        return false;
    }

    wav_audio_stream_init(&stream, fp, info, target_rate);
    if (wav_audio_stream_start(&stream, info->data_offset) == 0U)
    {
        sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, FR_DISK_ERR);
        return false;
    }

    while(loaded_frames < total_frames)
    {
        float left = 0.0f;
        float right = 0.0f;

        if (wav_audio_stream_next_frame(&stream, &left, &right) == 0U)
        {
            break;
        }

        g_sample_pool_data[(start_frame + loaded_frames) * 2U] = left;
        g_sample_pool_data[(start_frame + loaded_frames) * 2U + 1U] = right;
        loaded_frames++;
    }

    if ((loaded_frames == 0U) || (stream.io_error != 0U))
    {
        if (g_sample_pool_last_load_error == SAMPLE_POOL_LOAD_OK)
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, FR_DISK_ERR);
        }
        return false;
    }

    g_sample_region_start[sample_id] = start_frame;
    desc->data_start_frame = start_frame;
    desc->data = &g_sample_pool_data[start_frame * 2U];
    desc->length_frames = loaded_frames;
    desc->sample_rate = target_rate;
    desc->channels = 2U;
    desc->bits_per_sample = 32U;
    desc->bytes_per_frame = sizeof(float) * 2U;
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
    desc->data_start_frame = 0U;
}

void sample_pool_clear(uint16_t id)
{
    if(id >= SAMPLE_POOL_SIZE)
        return;

    sample_pool_release_slot(id);
    sample_pool_clear_entry(&g_sample_pool[id]);
    g_sample_region_start[id] = 0U;
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
        g_sample_region_start[i] = 0U;
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

    if(!((info.audio_format == 1U) || (info.audio_format == 65534U)) ||
       ((info.channels != 1U) && (info.channels != 2U)) ||
       ((info.bits_per_sample != 16U) && (info.bits_per_sample != 24U) && (info.bits_per_sample != 32U)) ||
       (info.block_align == 0U) ||
       (info.sample_rate == 0U))
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
    desc->length_frames = 0U;
    desc->bytes_per_frame = sizeof(float) * 2U;
    desc->data_start_frame = 0U;
    desc->sample_rate = 48000U;
    desc->channels = 2U;
    desc->bits_per_sample = 32U;

    if(!sample_pool_load_full_data(&fp, (uint16_t)slot, id, &info, data_size_aligned, desc))
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
