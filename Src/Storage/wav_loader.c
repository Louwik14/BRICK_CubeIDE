/**
 * @file wav_loader.c
 * @brief Module applicatif wav_loader.
 *
 * RÃ´le du module:
 * - ImplÃ©menter les traitements liÃ©s Ã  wav_loader.
 * - Fournir les services internes utilisÃ©s par le firmware utilisateur.
 *
 * Architecture:
 * - AppelÃ© par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dÃ©pendances matÃ©rielles et/ou modules utilisateur associÃ©s.
 *
 * Contraintes temps rÃ©el:
 * - IRQ: selon les API appelÃ©es.
 * - Hard realtime: selon le chemin d'exÃ©cution.
 * - malloc: Ã©viter en chemin critique.
 */

#include "wav_loader.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/multi_record_writer.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_audio_stream.h"
#include "wav_parser.h"

#define WAV_BUFFER_FRAMES (48000U)
#define WAV_BUFFER_SAMPLES (WAV_BUFFER_FRAMES * 2U)

static AUDIO_COLD_SDRAM float g_wav_pcm[WAV_BUFFER_SAMPLES];
UI_SDRAM static wav_loader_catalog_entry_t g_wav_catalog[WAV_LOADER_CATALOG_MAX];
static uint8_t g_wav_catalog_count;
static uint8_t g_wav_catalog_ready;

/**
 * @brief Point d'entrÃ©e pcm24_to_float.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  pcm24_to_float.
 *
 * @param p ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
const float *wav_loader_get_interleaved_buffer(void)
{
    return g_wav_pcm;
}

/**
 * @brief Point d'entrÃ©e wav_loader_get_capacity_frames.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  wav_loader_get_capacity_frames.
 *
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
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
 * @brief Point d'entrÃ©e wav_ext_is_wav.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  wav_ext_is_wav.
 *
 * @param name ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
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

static void wav_loader_catalog_add(const char *display_name, const char *path)
{
    if ((display_name == 0) || (display_name[0] == '\0')
        || (path == 0) || (path[0] == '\0')
        || (g_wav_catalog_count >= WAV_LOADER_CATALOG_MAX))
    {
        return;
    }

    wav_loader_catalog_entry_t *entry = &g_wav_catalog[g_wav_catalog_count];
    const int name_len = snprintf(entry->name, sizeof(entry->name), "%s", display_name);
    const int path_len = snprintf(entry->path, sizeof(entry->path), "%s", path);
    if ((name_len < 0) || (path_len < 0)
        || ((uint32_t)name_len >= sizeof(entry->name))
        || ((uint32_t)path_len >= sizeof(entry->path)))
    {        memset(entry, 0, sizeof(*entry));
        return;
    }
    entry->state = WAV_LOADER_CATALOG_READY;
    g_wav_catalog_count++;
}

uint8_t wav_loader_catalog_notify_file_created(const char *path)
{
    if ((path == 0) || (path[0] == '\0') || (!wav_ext_is_wav(path)))
    {
        return 0U;
    }

    if ((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        return 0U;
    }

    if (g_wav_catalog_ready == 0U)
    {
        return 1U;
    }

    for (uint8_t i = 0U; i < g_wav_catalog_count; ++i)
    {
        if (strcmp(g_wav_catalog[i].path, path) == 0)
        {
            return 1U;
        }
    }

    if (g_wav_catalog_count >= WAV_LOADER_CATALOG_MAX)
    {
        return 0U;
    }

    const char *display_name = path;
    const char *const loops_prefix = "0:/PROJECT/LOOPS/";
    const size_t loops_prefix_len = strlen(loops_prefix);
    const uint8_t previous_count = g_wav_catalog_count;
    if (strncmp(path, loops_prefix, loops_prefix_len) == 0)
    {
        char loop_name[32];
        const int written = snprintf(loop_name,
                                     sizeof(loop_name),
                                     "LOOPS/%s",
                                     path + loops_prefix_len);
        if ((written < 0) || ((uint32_t)written >= sizeof(loop_name)))
        {
            return 0U;
        }
        wav_loader_catalog_add(loop_name, path);
    }
    else
    {
        const char *slash = strrchr(path, '/');
        if (slash != 0)
        {
            display_name = slash + 1;
        }
        wav_loader_catalog_add(display_name, path);
    }

    return ((g_wav_catalog_count > previous_count)
            && (strcmp(g_wav_catalog[g_wav_catalog_count - 1U].path, path) == 0))
        ? 1U
        : 0U;
}

static void wav_loader_catalog_scan_dir(const char *dir_path, const char *display_prefix)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK)
    {
        return;
    }

    while (g_wav_catalog_count < WAV_LOADER_CATALOG_MAX)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK)
        {
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

        char display_name[32];
        char path[64];
        const int name_len = ((display_prefix != 0) && (display_prefix[0] != '\0'))
            ? snprintf(display_name, sizeof(display_name), "%s/%s", display_prefix, fno.fname)
            : snprintf(display_name, sizeof(display_name), "%s", fno.fname);
        const size_t dir_len = strlen(dir_path);
        const char *const separator = ((dir_len != 0U) && (dir_path[dir_len - 1U] == '/')) ? "" : "/";
        const int path_len = snprintf(path, sizeof(path), "%s%s%s", dir_path, separator, fno.fname);
        if ((name_len < 0) || (path_len < 0)
            || ((uint32_t)name_len >= sizeof(display_name))
            || ((uint32_t)path_len >= sizeof(path)))
        {
            continue;
        }

        wav_loader_catalog_add(display_name, path);
    }

    (void)f_closedir(&dir);
}

void wav_loader_catalog_refresh(void)
{
#if WAV_LOADER_HAS_FATFS
    FRESULT fr;

    if ((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        return;
    }

    wav_loader_catalog_clear();

    if (g_wav_fs_mounted == 0U)
    {
        fr = f_mount(&g_wav_fs, "0:", 1U);
        if (fr != FR_OK)
        {            return;
        }
        g_wav_fs_mounted = 1U;
    }

    wav_loader_catalog_scan_dir("0:/", "");
    wav_loader_catalog_scan_dir("0:/PROJECT/LOOPS", "LOOPS");
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
 * @brief Point d'entrÃ©e wav_loader_find_first_wav.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  wav_loader_find_first_wav.
 *
 * @param out_path ParamÃ¨tre d'entrÃ©e de l'API.
 * @param max_len ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool wav_loader_find_first_wav(char *out_path, uint32_t max_len)
{
    if((out_path == 0) || (max_len < 8U))
    {        return false;
    }

    wav_loader_catalog_refresh();
    if (g_wav_catalog_count == 0U)
    {        return false;
    }

    if ((snprintf(out_path, max_len, "%s", g_wav_catalog[0].path) < 0)
        || (strlen(out_path) >= max_len))
    {
        return false;
    }
    return true;
}

/**
 * @brief Point d'entrÃ©e wav_loader_load_to_sdram.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  wav_loader_load_to_sdram.
 *
 * @param path ParamÃ¨tre d'entrÃ©e de l'API.
 * @param info ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool wav_loader_load_to_sdram(const char *path, wav_info_t *info)
{
    uint32_t i;
    wav_info_t local_info;
    wav_info_t *const out_info = (info != 0) ? info : &local_info;

    if ((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
    {
        return false;
    }

    for(i = 0U; i < WAV_BUFFER_SAMPLES; i++)
        g_wav_pcm[i] = 0.0f;

    memset(out_info, 0, sizeof(*out_info));

#if WAV_LOADER_HAS_FATFS
    FIL fp;
    FRESULT fr;
    uint32_t frames_loaded = 0U;

    if(path == 0)
    {        return false;
    }

    if(g_wav_fs_mounted == 0U)
    {
        fr = f_mount(&g_wav_fs, "0:", 1U);
        if(fr != FR_OK)
        {            return false;
        }
        g_wav_fs_mounted = 1U;
    }

    fr = f_open(&fp, path, FA_READ);
    if(fr != FR_OK)
    {        return false;
    }

    if(!wav_parser_parse_info(&fp, out_info))
    {
        (void)f_close(&fp);        return false;
    }
    if(!((out_info->audio_format == 1U) || (out_info->audio_format == 65534U)))
    {
        (void)f_close(&fp);        return false;
    }

    if(!((out_info->channels == 1U) || (out_info->channels == 2U)))
    {
        (void)f_close(&fp);        return false;
    }

    if(!((out_info->bits_per_sample == 16U) || (out_info->bits_per_sample == 24U) || (out_info->bits_per_sample == 32U)))
    {
        (void)f_close(&fp);        return false;
    }

    if((out_info->block_align == 0U) || (out_info->sample_rate == 0U))
    {
        (void)f_close(&fp);        return false;
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
            (void)f_close(&fp);            return false;
        }

        wav_audio_stream_t stream;
        wav_audio_stream_init(&stream, &fp, out_info, target_rate);
        if (wav_audio_stream_start(&stream, out_info->data_offset) == 0U)
        {
            (void)f_close(&fp);            return false;
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
            (void)f_close(&fp);            return false;
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
        return true;
    }
#else
    (void)path;    return false;
#endif
}
