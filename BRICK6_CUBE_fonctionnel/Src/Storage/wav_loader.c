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
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "wav_loader.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "wav_parser.h"

#define WAV_BUFFER_FRAMES (48000U)
#define WAV_BUFFER_SAMPLES (WAV_BUFFER_FRAMES * 2U)

static AUDIO_COLD_SDRAM float g_wav_pcm[WAV_BUFFER_SAMPLES];

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
static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

/**
 * @brief Point d'entrée pcm32_to_float.
 *
 * Rôle:
 * - Exécuter le traitement associé à pcm32_to_float.
 *
 * @param p Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

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
#if WAV_LOADER_HAS_FATFS
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    if((out_path == 0) || (max_len < 8U))
    {
        printf("[WAV] invalid output buffer\r\n");
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

    fr = f_opendir(&dir, "0:/");
    if(fr != FR_OK)
    {
        printf("[WAV] f_opendir failed: %d\r\n", (int)fr);
        return false;
    }

    while(1)
    {
        fr = f_readdir(&dir, &fno);
        if(fr != FR_OK)
        {
            printf("[WAV] f_readdir failed: %d\r\n", (int)fr);
            (void)f_closedir(&dir);
            return false;
        }

        if(fno.fname[0] == '\0')
            break;

        if((fno.fattrib & AM_DIR) != 0U)
            continue;

        if(!wav_ext_is_wav(fno.fname))
            continue;

        if((snprintf(out_path, max_len, "0:/%s", fno.fname) < 0) ||
           (strlen(out_path) >= max_len))
        {
            printf("[WAV] path too long: %s\r\n", fno.fname);
            (void)f_closedir(&dir);
            return false;
        }

        printf("[WAV] found file: %s\r\n", out_path);
        (void)f_closedir(&dir);
        return true;
    }

    (void)f_closedir(&dir);
    printf("[WAV] no WAV file in root\r\n");
    return false;
#else
    (void)out_path;
    (void)max_len;
    printf("[WAV] FatFs unavailable in this build\r\n");
    return false;
#endif
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

    for(i = 0U; i < WAV_BUFFER_SAMPLES; i++)
        g_wav_pcm[i] = 0.0f;

    if(info != 0)
        memset(info, 0, sizeof(*info));

#if WAV_LOADER_HAS_FATFS
    FIL fp;
    FRESULT fr;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t bytes_per_frame;
    uint32_t max_frames_from_file;
    uint32_t frames_to_load;
    uint32_t frames_loaded = 0U;
    uint8_t io_buf[4096];

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

    if(!wav_parser_parse_info(&fp, info))
    {
        (void)f_close(&fp);
        printf("[WAV] invalid RIFF/WAVE or missing chunks\r\n");
        return false;
    }

    audio_format = 1U;
    channels = info->channels;
    sample_rate = info->sample_rate;
    bits_per_sample = info->bits_per_sample;
    data_offset = info->data_offset;
    data_size = info->data_size;

    printf("[WAV] fmt=%u ch=%u sr=%lu bits=%u data=%lu bytes off=%lu\r\n",
           (unsigned)audio_format,
           (unsigned)channels,
           (unsigned long)sample_rate,
           (unsigned)bits_per_sample,
           (unsigned long)data_size,
           (unsigned long)data_offset);

    if(audio_format != 1U)
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] format unsupported\r\n");
        return false;
    }

    if(sample_rate != 48000U)
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] bad sample rate\r\n");
        return false;
    }

    if(channels != 2U)
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] not stereo\r\n");
        return false;
    }

    if(!((bits_per_sample == 24U) || (bits_per_sample == 32U)))
    {
        (void)f_close(&fp);
        printf("[WAV ERROR] format unsupported\r\n");
        return false;
    }

    bytes_per_frame = (uint32_t)channels * ((uint32_t)bits_per_sample / 8U);
    if(bytes_per_frame == 0U)
    {
        (void)f_close(&fp);
        return false;
    }

    max_frames_from_file = data_size / bytes_per_frame;
    frames_to_load = (max_frames_from_file < WAV_BUFFER_FRAMES) ? max_frames_from_file : WAV_BUFFER_FRAMES;

    if(f_lseek(&fp, data_offset) != FR_OK)
    {
        (void)f_close(&fp);
        printf("[WAV] f_lseek data failed\r\n");
        return false;
    }

    while(frames_loaded < frames_to_load)
    {
        UINT br = 0U;
        uint32_t frames_left = frames_to_load - frames_loaded;
        uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        uint32_t chunk_bytes = chunk_frames * bytes_per_frame;

        if((f_read(&fp, io_buf, chunk_bytes, &br) != FR_OK) || (br == 0U))
            break;

        chunk_frames = br / bytes_per_frame;
        for(i = 0U; i < chunk_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bytes_per_frame];
            float l;
            float r;

            if(bits_per_sample == 24U)
            {
                l = pcm24_to_float(&frame[0]);
                r = pcm24_to_float(&frame[3]);
            }
            else
            {
                l = pcm32_to_float(&frame[0]);
                r = pcm32_to_float(&frame[4]);
            }

            g_wav_pcm[(frames_loaded + i) * 2U + 0U] = l;
            g_wav_pcm[(frames_loaded + i) * 2U + 1U] = r;
        }

        frames_loaded += chunk_frames;

        if(chunk_frames == 0U)
            break;
    }

    (void)f_close(&fp);


    printf("[WAV] loaded frames=%lu (capacity=%lu)\r\n",
           (unsigned long)frames_loaded,
           (unsigned long)WAV_BUFFER_FRAMES);

    return (frames_loaded > 0U);
#else
    (void)path;
    printf("[WAV] FatFs unavailable in this build\r\n");
    return false;
#endif
}
