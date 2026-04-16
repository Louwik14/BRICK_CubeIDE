/**
 * @file wav_loader.c
 * @brief Module applicatif wav_loader.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à wav_loader.
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

#include "wav_loader.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_audio_stream.h"
#include "wav_parser.h"

#define WAV_BUFFER_FRAMES (48000U)
#define WAV_BUFFER_SAMPLES (WAV_BUFFER_FRAMES * 2U)

static AUDIO_COLD_SDRAM float g_wav_pcm[WAV_BUFFER_SAMPLES];
static wav_loader_catalog_entry_t g_wav_catalog[WAV_LOADER_CATALOG_MAX];
static uint8_t g_wav_catalog_count;
static uint8_t g_wav_catalog_ready;

/**
 * @brief Point d'entrée pcm24_to_float.
 *
 * Rôle:
 * - Exécuter le traitement associé à pcm24_to_float.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
const float *wav_loader_get_interleaved_buffer(void)
{
    return g_wav_pcm;
}

/**
 * @brief Point d'entrée wav_loader_get_capacity_frames.
 *
 * Rôle:
 * - Exécuter le traitement associé à wav_loader_get_capacity_frames.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t wav_loader_get_capacity_frames(void)
{
    return WAV_BUFFER_FRAMES;
}


#if WAV_LOADER_HAS_FATFS
static FATFS g_wav_fs;
static uint8_t g_wav_fs_mounted;

/**
 * @brief Point d'entrée wav_ext_is_wav.
 *
 * Rôle:
 * - Exécuter le traitement associé à wav_ext_is_wav.
 *
 * @param name Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int wav_ext_is_wav(const char *name)
{
    size_t len;

    if(name == 0)
        return 0;

    len = strlen(name);
    if(len < 4U)
        return 0;

    return (name[len - 4U] == '.') &&
           ((name[len - 3U] == 'w') || (name[len - 3U] == 'W')) &&
           ((name[len - 2U] == 'a') || (name[len - 2U] == 'A')) &&
           ((name[len - 1U] == 'v') || (name[len - 1U] == 'V'));
}

static void wav_loader_catalog_clear(void)
{
    memset(g_wav_catalog, 0, sizeof(g_wav_catalog));
    g_wav_catalog_count = 0U;
    g_wav_catalog_ready = 0U;
}

static void wav_loader_catalog_add(const char *name)
{
    if ((name == 0) || (name[0] == '\0') || (g_wav_catalog_count >= WAV_LOADER_CATALOG_MAX))
    {
        return;
    }

    wav_loader_catalog_entry_t *entry = &g_wav_catalog[g_wav_catalog_count];
    const int name_len = snprintf(entry->name, sizeof(entry->name), "%s", name);
    const int path_len = snprintf(entry->path, sizeof(entry->path), "0:/%s", name);
    if ((name_len < 0) || (path_len < 0)
        || ((uint32_t)name_len >= sizeof(entry->name))
        || ((uint32_t)path_len >= sizeof(entry->path)))
    {
        printf("[WAV] catalog skip long entry: %s\r\n", name);
        memset(entry, 0, sizeof(*entry));
        return;
    }
    entry->state = WAV_LOADER_CATALOG_READY;
    g_wav_catalog_count++;
}

void wav_loader_catalog_refresh(void)
{
#if WAV_LOADER_HAS_FATFS
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    wav_loader_catalog_clear();

    if (g_wav_fs_mounted == 0U)
    {
        fr = f_mount(&g_wav_fs, "0:", 1U);
        if (fr != FR_OK)
        {
            printf("[WAV] catalog f_mount failed: %d\r\n", (int)fr);
            return;
        }
        g_wav_fs_mounted = 1U;
    }

    fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK)
    {
        printf("[WAV] catalog f_opendir failed: %d\r\n", (int)fr);
        return;
    }

    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK)
        {
            printf("[WAV] catalog f_readdir failed: %d\r\n", (int)fr);
            break;
        }

        if (fno.fname[0] == '\0')
        {
            break;
        }

        if (((fno.fattrib & AM_DIR) != 0U) || (!wav_ext_is_wav(fno.fname)))
        {
            continue;
        }

        wav_loader_catalog_add(fno.fname);
    }

    (void)f_closedir(&dir);
    g_wav_catalog_ready = 1U;
#else
    wav_loader_catalog_clear();
#endif
}

uint8_t wav_loader_catalog_count(void)
{
    if (g_wav_catalog_ready == 0U)
    {
        wav_loader_catalog_refresh();
    }

    return g_wav_catalog_count;
}

const wav_loader_catalog_entry_t *wav_loader_catalog_get(uint8_t index)
{
    if ((g_wav_catalog_ready == 0U) || (index >= g_wav_catalog_count))
    {
        return 0;
    }

    return &g_wav_catalog[index];
}

#endif

/**
 * @brief Point d'entrée wav_loader_find_first_wav.
 *
 * Rôle:
 * - Exécuter le traitement associé à wav_loader_find_first_wav.
 *
 * @param out_path Paramètre d'entrée de l'API.
 * @param max_len Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool wav_loader_find_first_wav(char *out_path, uint32_t max_len)
{
    if((out_path == 0) || (max_len < 8U))
    {
        printf("[WAV] invalid output buffer\r\n");
        return false;
    }

    wav_loader_catalog_refresh();
    if (g_wav_catalog_count == 0U)
    {
        printf("[WAV] no WAV file in root\r\n");
        return false;
    }

    if ((snprintf(out_path, max_len, "%s", g_wav_catalog[0].path) < 0) ||
        (strlen(out_path) >= max_len))
    {
        printf("[WAV] path too long: %s\r\n", g_wav_catalog[0].path);
        return false;
    }

    printf("[WAV] found file: %s\r\n", out_path);
    return true;
}

/**
 * @brief Point d'entrée wav_loader_load_to_sdram.
 *
 * Rôle:
 * - Exécuter le traitement associé à wav_loader_load_to_sdram.
 *
 * @param path Paramètre d'entrée de l'API.
 * @param info Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool wav_loader_load_to_sdram(const char *path, wav_info_t *info)
{
    uint32_t i;
    wav_info_t local_info;
    wav_info_t *const out_info = (info != 0) ? info : &local_info;

    for(i = 0U; i < WAV_BUFFER_SAMPLES; i++)
        g_wav_pcm[i] = 0.0f;

    memset(out_info, 0, sizeof(*out_info));

#if WAV_LOADER_HAS_FATFS
    FIL fp;
    FRESULT fr;
    uint32_t frames_loaded = 0U;

    if(path == 0)
    {
        printf("[WAV] invalid path\r\n");
        return false;
    }

    if(g_wav_fs_mounted == 0U)
    {
        fr = f_mount(&g_wav_fs, "0:", 1U);
        if(fr != FR_OK)
        {
            printf("[WAV] f_mount failed: %d\r\n", (int)fr);
            return false;
        }
        g_wav_fs_mounted = 1U;
    }

    fr = f_open(&fp, path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[WAV] f_open failed: %s (%d)\r\n", path, (int)fr);
        return false;
    }

    if(!wav_parser_parse_info(&fp, out_info))
    {
        (void)f_close(&fp);
        printf("[WAV] invalid RIFF/WAVE or missing chunks\r\n");
        return false;
    }

    printf("[WAV] fmt=%u ch=%u sr=%lu bits=%u data=%lu bytes off=%lu\r\n",
           (unsigned)out_info->audio_format,
           (unsigned)out_info->channels,
           (unsigned long)out_info->sample_rate,
           (unsigned)out_info->bits_per_sample,
           (unsigned long)out_info->data_size,
           (unsigned long)out_info->data_offset);

    if(!((out_info->audio_format == 1U) || (out_info->audio_format == 65534U)))
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] format unsupported\r\n");
        return false;
    }

    if(!((out_info->channels == 1U) || (out_info->channels == 2U)))
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] unsupported channel count\r\n");
        return false;
    }

    if(!((out_info->bits_per_sample == 16U) || (out_info->bits_per_sample == 24U) || (out_info->bits_per_sample == 32U)))
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] format unsupported\r\n");
        return false;
    }

    if((out_info->block_align == 0U) || (out_info->sample_rate == 0U))
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] invalid timing metadata\r\n");
        return false;
    }

    {
        const uint32_t source_frames = out_info->data_size / out_info->block_align;
        const uint32_t target_rate = 48000U;
        const uint32_t frames_to_load = (source_frames == 0U)
            ? 0U
            : (uint32_t)(((uint64_t)source_frames * (uint64_t)target_rate
                          + (uint64_t)out_info->sample_rate - 1U)
                         / (uint64_t)out_info->sample_rate);

        if (frames_to_load == 0U)
        {
            (void)f_close(&fp);
            printf("[WAV ERROR] empty source\r\n");
            return false;
        }

        wav_audio_stream_t stream;
        wav_audio_stream_init(&stream, &fp, out_info, target_rate);
        if (wav_audio_stream_start(&stream, out_info->data_offset) == 0U)
        {
            (void)f_close(&fp);
            printf("[WAV] f_lseek data failed\r\n");
            return false;
        }

        while ((frames_loaded < frames_to_load) && (frames_loaded < WAV_BUFFER_FRAMES))
        {
            float left = 0.0f;
            float right = 0.0f;
            if (wav_audio_stream_next_frame(&stream, &left, &right) == 0U)
            {
                break;
            }

            g_wav_pcm[frames_loaded * 2U + 0U] = left;
            g_wav_pcm[frames_loaded * 2U + 1U] = right;
            frames_loaded++;
        }

        if ((frames_loaded == 0U) || (stream.io_error != 0U))
        {
            (void)f_close(&fp);
            printf("[WAV] stream decode failed\r\n");
            return false;
        }

        if (info != 0)
        {
            info->audio_format = 1U;
            info->sample_rate = target_rate;
            info->byte_rate = target_rate * 2U * sizeof(float);
            info->channels = 2U;
            info->block_align = 2U * sizeof(float);
            info->bits_per_sample = 32U;
            info->data_offset = 0U;
            info->data_size = frames_loaded * (2U * sizeof(float));
        }

        (void)f_close(&fp);

        printf("[WAV] loaded frames=%lu (capacity=%lu)\r\n",
               (unsigned long)frames_loaded,
               (unsigned long)WAV_BUFFER_FRAMES);

        return true;
    }
#else
    (void)path;
    printf("[WAV] FatFs unavailable in this build\r\n");
    return false;
#endif
}
