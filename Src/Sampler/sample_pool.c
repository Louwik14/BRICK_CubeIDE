/**
 * @file sample_pool.c
 * @brief Module applicatif sample_pool.
 *
 * RÃ´le du module:
 * - ImplÃ©menter les traitements liÃ©s Ã  sample_pool.
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
 *
 * Notes:
 * - Documentation ajoutÃ©e sans modification de la logique d'exÃ©cution.
 */

#include "Sampler/sample_pool.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Sampler/sample_cache.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_parser.h"
#include "Storage/sd_access_gate.h"
#include "Storage/waveform_cache.h"

#include "ff.h"

#define SAMPLE_POOL_HAS_FATFS 1

#ifndef SAMPLE_POOL_HAS_FATFS
#define SAMPLE_POOL_HAS_FATFS 0
#endif

SDRAM_SAMPLES static sample_desc_t g_sample_pool[SAMPLE_POOL_SIZE];

static CTRL_STATE int16_t g_sample_slot_by_sample[SAMPLE_POOL_SIZE];
static CTRL_STATE sample_pool_load_error_t g_sample_pool_last_load_error = SAMPLE_POOL_LOAD_OK;
static CTRL_STATE uint8_t g_sample_pool_last_sd_error_code;
STORAGE_STATE_SDRAM static sample_pool_diag_t g_sample_pool_diag;

static void sample_pool_set_error(sample_pool_load_error_t error, FRESULT fr)
{
    g_sample_pool_last_load_error = error;
    g_sample_pool_last_sd_error_code = (uint8_t)fr;
}

static void sample_pool_diag_record_open_fail(const char *path, FRESULT fr)
{
    g_sample_pool_diag.load_open_fail_count++;
    g_sample_pool_diag.gate_owner = sd_access_gate_current_owner();
    g_sample_pool_diag.gate_last_owner = sd_access_gate_last_owner();
    g_sample_pool_diag.fatfs_result = fr;
    if (path != 0)
    {
        const size_t path_len = strlen(path);
        const size_t copy_len = (path_len < sizeof(g_sample_pool_diag.path))
                                    ? path_len
                                    : (sizeof(g_sample_pool_diag.path) - 1U);
        memcpy(g_sample_pool_diag.path, path, copy_len);
        g_sample_pool_diag.path[copy_len] = '\0';
    }
    else
    {
        g_sample_pool_diag.path[0] = '\0';
    }
}

static void sample_pool_release_gate_on_error(void)
{
    g_sample_pool_diag.gate_release_on_error_count++;
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
}

static void sample_pool_set_read_error_from_fresult(FRESULT fr)
{
    switch (fr)
    {
        case FR_DISK_ERR:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, fr);
            break;

        case FR_INT_ERR:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_INT_ERR, fr);
            break;

        case FR_NOT_READY:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_NOT_READY, fr);
            break;

        case FR_INVALID_OBJECT:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_INVALID_OBJECT, fr);
            break;

        case FR_TIMEOUT:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_TIMEOUT, fr);
            break;

        case FR_NOT_ENOUGH_CORE:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_NOT_ENOUGH_CORE, fr);
            break;

        default:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, fr);
            break;
    }
}

static void sample_pool_set_error_from_cache(uint8_t cache_error, FRESULT cache_fr)
{
    switch (cache_error)
    {
        case 1U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, cache_fr);
            break;

        case 2U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_GATE_REFUSED, cache_fr);
            break;

        case 3U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_MOUNT_FAIL, cache_fr);
            break;

        case 4U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_OPEN_FAIL, cache_fr);
            break;

        case 5U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_PARSE_FAIL, cache_fr);
            break;

        case 6U:
        case 7U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT, cache_fr);
            break;

        case 8U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_MEMORY_LIMIT, cache_fr);
            break;

        case 9U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_SEEK_FAIL, cache_fr);
            break;

        case 13U:
        case 14U:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_SHORT_READ, cache_fr);
            break;

        case 12U:
            sample_pool_set_read_error_from_fresult(cache_fr);
            break;

        case 10U:
        case 11U:
        default:
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_READ_FAIL, cache_fr);
            break;
    }
}

static uint8_t sample_pool_wav_container_supported(const wav_info_t *info)
{
    return ((info != 0)
            && ((info->audio_format == 1U) || (info->audio_format == 65534U))
            && ((info->channels == 1U) || (info->channels == 2U))
            && ((info->bits_per_sample == 16U)
                || (info->bits_per_sample == 24U)
                || (info->bits_per_sample == 32U))
            && (info->block_align != 0U)) ? 1U : 0U;
}

/**
 * @brief Point d'entrÃ©e sample_pool_pcm24_to_float.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_pcm24_to_float.
 *
 * @param p ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/**
 * @brief Point d'entrÃ©e sample_pool_release_slot.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_release_slot.
 *
 * @param sample_id ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void sample_pool_release_slot(uint16_t sample_id)
{
    if(sample_id >= SAMPLE_POOL_SIZE)
        return;

    g_sample_slot_by_sample[sample_id] = -1;
}

#if SAMPLE_POOL_HAS_FATFS
STORAGE_STATE_SDRAM static FATFS g_sample_pool_fs;
static uint8_t g_sample_pool_fs_mounted;
#endif

/**
 * @brief Point d'entrÃ©e sample_pool_clear_entry.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_clear_entry.
 *
 * @param desc ParamÃ¨tre d'entrÃ©e de l'API.
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
    sample_cache_clear(id);
    sample_pool_clear_entry(&g_sample_pool[id]);
}

/**
 * @brief Point d'entrÃ©e sample_pool_trim_path_copy.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_trim_path_copy.
 *
 * @param dst ParamÃ¨tre d'entrÃ©e de l'API.
 * @param dst_size ParamÃ¨tre d'entrÃ©e de l'API.
 * @param src ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
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
 * @brief Point d'entrÃ©e sample_pool_init.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_init.
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

#if SAMPLE_POOL_HAS_FATFS
    g_sample_pool_fs_mounted = 0U;
#endif
    sample_cache_init();
    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);
}

void sample_pool_capture_project_snapshot(sample_pool_project_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    for (uint16_t id = 0U; id < SAMPLE_POOL_SIZE; ++id)
    {
        memset(out_snapshot->slots[id].path, 0, sizeof(out_snapshot->slots[id].path));
        if (g_sample_pool[id].path[0] == '\0')
        {
            continue;
        }

        const size_t path_len = strlen(g_sample_pool[id].path);
        memcpy(out_snapshot->slots[id].path, g_sample_pool[id].path, path_len);
        out_snapshot->slots[id].path[path_len] = '\0';
    }
}

void sample_pool_restore_project_snapshot(const sample_pool_project_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    sample_pool_init();

    for (uint16_t id = 0U; id < SAMPLE_POOL_SIZE; ++id)
    {
        const char *const path = snapshot->slots[id].path;

        if ((path == NULL) || (path[0] == '\0'))
        {
            continue;
        }

        (void)sample_pool_load(id, path);
    }

    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);
}

/**
 * @brief Point d'entrÃ©e sample_pool_load.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  sample_pool_load.
 *
 * @param id ParamÃ¨tre d'entrÃ©e de l'API.
 * @param path ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool sample_pool_load(uint16_t id, const char *path)
{
    char trimmed_path[SAMPLE_POOL_PATH_MAX];
    size_t path_len;

    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);

    if(id >= SAMPLE_POOL_SIZE)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_ID, FR_INVALID_PARAMETER);
        return false;
    }

    if((path == NULL) || (path[0] == '\0'))
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, FR_INVALID_NAME);
        return false;
    }

    const size_t raw_path_len = strlen(path);
    if(raw_path_len >= SAMPLE_POOL_PATH_MAX)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_PATH_TOO_LONG, FR_INVALID_NAME);
        return false;
    }

    path_len = sample_pool_trim_path_copy(trimmed_path, sizeof(trimmed_path), path);
    if(path_len == 0U)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, FR_INVALID_NAME);
        return false;
    }

#if SAMPLE_POOL_HAS_FATFS
    uint8_t sd_gate_held = 0U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_SD_GATE_REFUSED, FR_TIMEOUT);
        return false;
    }
    sd_gate_held = 1U;

    if(g_sample_pool_fs_mounted == 0U)
    {
        sd_access_trace_begin("sample_pool_f_mount");
        const FRESULT mount_fr = f_mount(&g_sample_pool_fs, "0:", 1U);
        sd_access_trace_end("sample_pool_f_mount", (int)mount_fr, 0U);
        if(mount_fr != FR_OK)
        {            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_MOUNT_FAIL, mount_fr);
            sample_pool_release_gate_on_error();
            return false;
        }

        g_sample_pool_fs_mounted = 1U;
    }

    FIL fp;
    sd_access_trace_begin("sample_pool_f_open");
    const FRESULT open_fr = f_open(&fp, trimmed_path, FA_READ);
    sd_access_trace_end("sample_pool_f_open", (int)open_fr, 0U);
    if(open_fr != FR_OK)
    {        if (open_fr == FR_NO_FILE)
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_FILE_NOT_FOUND, open_fr);
        }
        else
        {
            sample_pool_set_error(SAMPLE_POOL_LOAD_SD_OPEN_FAIL, open_fr);
        }
        sample_pool_diag_record_open_fail(trimmed_path, open_fr);
        sample_pool_release_gate_on_error();
        return false;
    }

    wav_info_t info;
    memset(&info, 0, sizeof(info));

    if(!wav_parser_parse_info(&fp, &info))
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_PARSE_FAIL, FR_INVALID_OBJECT);
        (void)f_close(&fp);
        sample_pool_release_gate_on_error();
        return false;
    }

    if (sample_pool_wav_container_supported(&info) == 0U)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT, FR_INVALID_PARAMETER);
        (void)f_close(&fp);
        sample_pool_release_gate_on_error();
        return false;
    }

    if (info.sample_rate != 48000U)
    {        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_48K_REQUIRED, FR_INVALID_PARAMETER);
        (void)f_close(&fp);
        sample_pool_release_gate_on_error();
        return false;
    }

    const uint32_t bytes_per_frame = (uint32_t)info.block_align;
    const uint32_t data_size_aligned = info.data_size - (info.data_size % bytes_per_frame);

    sample_desc_t next_desc;
    memset(&next_desc, 0, sizeof(next_desc));
    (void)memcpy(next_desc.path, trimmed_path, path_len + 1U);
    next_desc.data_offset = info.data_offset;
    next_desc.length_frames = data_size_aligned / bytes_per_frame;
    next_desc.bytes_per_frame = bytes_per_frame;
    next_desc.data_start_frame = 0U;
    next_desc.sample_rate = info.sample_rate;
    next_desc.channels = info.channels;
    next_desc.bits_per_sample = info.bits_per_sample;
    next_desc.data = NULL;

    (void)f_close(&fp);
    if (sd_gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    }

    if (next_desc.length_frames == 0U)
    {
        sample_pool_set_error(SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT, FR_INVALID_PARAMETER);
        return false;
    }

    sample_desc_t *desc = &g_sample_pool[id];
    (void)waveform_cache_request_for_wav_known_duration(next_desc.path,
                                                        WAVEFORM_CACHE_REASON_EDITOR_VISIBLE,
                                                        next_desc.length_frames,
                                                        next_desc.sample_rate);
    if (sample_cache_prepare(id, next_desc.path) == 0U)
    {
        const uint8_t cache_error = sample_cache_get_last_error(id);
        const FRESULT cache_fr = (FRESULT)sample_cache_get_last_fresult(id);
        if ((cache_error == 0U) && (cache_fr != FR_OK))
        {
            if (cache_fr == FR_TIMEOUT)
            {
                sample_pool_set_error(SAMPLE_POOL_LOAD_SD_GATE_REFUSED, cache_fr);
            }
            else if (cache_fr == FR_DISK_ERR)
            {
                sample_pool_set_error(SAMPLE_POOL_LOAD_SD_MOUNT_FAIL, cache_fr);
            }
            else if (cache_fr == FR_INVALID_NAME)
            {
                sample_pool_set_error(SAMPLE_POOL_LOAD_INVALID_PATH, cache_fr);
            }
            else
            {
                sample_pool_set_error(SAMPLE_POOL_LOAD_SD_OPEN_FAIL, cache_fr);
                sample_pool_diag_record_open_fail(next_desc.path, cache_fr);
            }
        }
        else
        {
            sample_pool_set_error_from_cache(cache_error, cache_fr);
        }
        if (!((cache_error == 0U) && (cache_fr != FR_OK)))
        {
            sample_pool_clear_entry(desc);
            g_sample_slot_by_sample[id] = -1;
        }
        return false;
    }

    *desc = next_desc;

    uint32_t cached_frames = 0U;
    desc->data = (float *)sample_cache_get_legacy_data(id, &cached_frames);
    if (cached_frames != 0U)
    {
        desc->length_frames = cached_frames;
        desc->bytes_per_frame = sizeof(float) * 2U;
        desc->channels = 2U;
        desc->bits_per_sample = 32U;
    }

    desc->valid = 1U;
    g_sample_slot_by_sample[id] = (int16_t)id;
    sample_pool_set_error(SAMPLE_POOL_LOAD_OK, FR_OK);

    return true;
#else
    (void)path;
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
    return ((desc != NULL) && (desc->valid != 0U) && (desc->length_frames != 0U)
            && (sample_cache_is_ready(id) != 0U));
}

sample_pool_slot_state_t sample_pool_get_state(uint16_t id)
{
    const sample_desc_t *const desc = sample_pool_get(id);
    if (desc == NULL)
    {
        return SAMPLE_POOL_SLOT_EMPTY;
    }

    if ((desc->valid != 0U) && (desc->length_frames != 0U))
    {
        const sample_cache_slot_readiness_t readiness = sample_cache_get_slot_readiness(id);
        if (readiness == SAMPLE_CACHE_SLOT_PLAYABLE)
        {
            return SAMPLE_POOL_SLOT_LOADED;
        }

        if ((readiness == SAMPLE_CACHE_SLOT_PREPARING)
            || (readiness == SAMPLE_CACHE_SLOT_START_PENDING)
            || (readiness == SAMPLE_CACHE_SLOT_NEEDS_REPREPARE))
        {
            return SAMPLE_POOL_SLOT_PREPARING;
        }

        if (readiness == SAMPLE_CACHE_SLOT_ERROR)
        {
            return SAMPLE_POOL_SLOT_ERROR;
        }
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

const sample_pool_diag_t *sample_pool_get_diag(void)
{
    return &g_sample_pool_diag;
}
