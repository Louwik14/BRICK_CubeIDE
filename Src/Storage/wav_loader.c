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

#include "Platform/memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_access_gate.h"
#include "SD/sd_scheduler_runtime.h"
#include "wav_parser.h"

#define WAV_LOADER_CATALOG_MAGIC (0x314C3657UL) /* W6L1 */
#define WAV_LOADER_CATALOG_VERSION (1U)
#define WAV_LOADER_CATALOG_PATH "0:/BRICK/SAMPLE.CAT"
#define WAV_LOADER_SAMPLE_ROOT "0:/Samples"
#define WAV_LOADER_CATALOG_VIEW_CACHE_COUNT (2U)

static uint16_t g_wav_catalog_count;
static uint8_t g_wav_catalog_loaded;
static uint8_t g_wav_catalog_truncated;
static uint8_t g_wav_catalog_stale;
static uint8_t g_wav_catalog_last_sd_busy;
static uint8_t g_wav_catalog_last_io_error;
static uint8_t g_wav_catalog_path_truncated;
STORAGE_STATE_SDRAM static wav_loader_catalog_diag_t g_wav_catalog_diag;


#if WAV_LOADER_HAS_FATFS
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

static uint8_t wav_loader_path_is_hidden_system_cache(const char *path)
{
    static const char prefix[] = "0:/BRICK/.wavecache/";
    return (uint8_t)((path != 0)
            && (strncmp(path, prefix, sizeof(prefix) - 1U) == 0));
}

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_size;
    uint16_t capacity;
    uint16_t count;
    uint32_t checksum;
} wav_loader_catalog_file_header_t;

typedef struct
{
    uint8_t valid;
    uint8_t _pad;
    uint16_t parent_id;
    uint16_t page_start;
    uint16_t child_count;
    uint16_t loaded_count;
    uint32_t age;
    wav_loader_catalog_entry_t entries[WAV_LOADER_CATALOG_VIEW_MAX];
    uint16_t indices[WAV_LOADER_CATALOG_VIEW_MAX];
} wav_loader_catalog_view_t;

UI_SDRAM static wav_loader_catalog_view_t g_wav_catalog_views[WAV_LOADER_CATALOG_VIEW_CACHE_COUNT];
UI_SDRAM static wav_loader_catalog_view_t g_wav_catalog_scratch_view;
static wav_loader_catalog_entry_t g_wav_catalog_lookup_entry;
static uint16_t g_wav_catalog_lookup_index = WAV_LOADER_CATALOG_ROOT_PARENT;
static uint8_t g_wav_catalog_lookup_valid;
static uint32_t g_wav_catalog_view_age;

typedef enum
{
    WAV_CATALOG_VIEW_IDLE = 0,
    WAV_CATALOG_VIEW_MOUNT,
    WAV_CATALOG_VIEW_OPEN,
    WAV_CATALOG_VIEW_HEADER,
    WAV_CATALOG_VIEW_READ,
    WAV_CATALOG_VIEW_CLOSE
} wav_catalog_view_state_t;

typedef struct
{
    wav_catalog_view_state_t state;
    FIL file;
    wav_loader_catalog_file_header_t header;
    uint16_t parent_id;
    uint16_t page_start;
    uint16_t entry_index;
    uint16_t child_ordinal;
    uint32_t media_epoch;
    uint8_t file_open;
    uint8_t read_ok;
} wav_catalog_view_load_t;

STORAGE_STATE_SDRAM static wav_catalog_view_load_t g_wav_catalog_view_load;

static void wav_loader_catalog_checksum_update(uint32_t *hash, const void *data, uint32_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (uint32_t i = 0U; i < len; ++i)
    {
        *hash ^= bytes[i];
        *hash *= 16777619UL;
    }
}

static void wav_loader_catalog_views_clear(void)
{
    memset(g_wav_catalog_views, 0, sizeof(g_wav_catalog_views));
    memset(&g_wav_catalog_scratch_view, 0, sizeof(g_wav_catalog_scratch_view));
    memset(&g_wav_catalog_lookup_entry, 0, sizeof(g_wav_catalog_lookup_entry));
    g_wav_catalog_lookup_index = WAV_LOADER_CATALOG_ROOT_PARENT;
    g_wav_catalog_lookup_valid = 0U;
    g_wav_catalog_view_age = 0U;
    memset(&g_wav_catalog_view_load, 0, sizeof(g_wav_catalog_view_load));
}

static void wav_loader_catalog_diag_record_open_fail(FRESULT fr)
{
    g_wav_catalog_diag.catalog_open_fail_count++;
    g_wav_catalog_diag.gate_owner = sd_access_gate_current_owner();
    g_wav_catalog_diag.gate_last_owner = sd_access_gate_last_owner();
    g_wav_catalog_diag.fatfs_result = fr;
    (void)snprintf(g_wav_catalog_diag.path,
                   sizeof(g_wav_catalog_diag.path),
                   "%s",
                   WAV_LOADER_CATALOG_PATH);
}

static void wav_loader_catalog_release_gate_on_error(void)
{
    g_wav_catalog_diag.gate_release_on_error_count++;
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
}

static void wav_loader_catalog_clear(void)
{
    wav_loader_catalog_views_clear();
    g_wav_catalog_count = 0U;
    g_wav_catalog_loaded = 0U;
    g_wav_catalog_truncated = 0U;
    g_wav_catalog_stale = 1U;
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    g_wav_catalog_path_truncated = 0U;
}

static uint8_t wav_loader_catalog_fill_entry(wav_loader_catalog_entry_t *entry,
                                             const char *display_name,
                                             const char *path,
                                             uint16_t parent_id,
                                             wav_loader_catalog_entry_type_t type,
                                             uint32_t size,
                                             uint16_t date,
                                             uint16_t time)
{
    if ((display_name == 0) || (display_name[0] == '\0')
        || (path == 0) || (path[0] == '\0')
        || (entry == 0)
        || (wav_loader_path_is_hidden_system_cache(path) != 0U))
    {
        return 0U;
    }

    memset(entry, 0, sizeof(*entry));
    const int name_len = snprintf(entry->name, sizeof(entry->name), "%s", display_name);
    const int path_len = snprintf(entry->path, sizeof(entry->path), "%s", path);
    if ((name_len < 0) || (path_len < 0)
        || ((uint32_t)name_len >= sizeof(entry->name))
        || ((uint32_t)path_len >= sizeof(entry->path)))
    {
        memset(entry, 0, sizeof(*entry));
        return 0U;
    }
    entry->size = size;
    entry->parent_id = parent_id;
    entry->date = date;
    entry->time = time;
    entry->type = type;
    entry->state = WAV_LOADER_CATALOG_READY;
    return 1U;
}

typedef struct
{
    FIL *file;
    FRESULT fr;
    uint32_t checksum;
} wav_loader_catalog_build_t;

static uint8_t wav_loader_catalog_write_entry(wav_loader_catalog_build_t *build,
                                              const wav_loader_catalog_entry_t *entry,
                                              uint16_t *out_index)
{
    UINT written = 0U;
    if ((build == 0) || (build->file == 0) || (entry == 0)
        || (build->fr != FR_OK)
        || (g_wav_catalog_count >= WAV_LOADER_CATALOG_MAX))
    {
        g_wav_catalog_truncated = 1U;
        return 0U;
    }

    if (out_index != 0)
    {
        *out_index = g_wav_catalog_count;
    }

    build->fr = f_write(build->file, entry, sizeof(*entry), &written);
    if ((build->fr != FR_OK) || (written != sizeof(*entry)))
    {
        build->fr = FR_INT_ERR;
        g_wav_catalog_truncated = 1U;
        return 0U;
    }

    wav_loader_catalog_checksum_update(&build->checksum, entry, sizeof(*entry));
    g_wav_catalog_count++;
    return 1U;
}

uint8_t wav_loader_catalog_notify_file_created(const char *path)
{
    if ((path == 0) || (path[0] == '\0') || (!wav_ext_is_wav(path))
            || (wav_loader_path_is_hidden_system_cache(path) != 0U))
    {
        return 0U;
    }

    wav_loader_catalog_mark_stale();
    return 1U;
}

static uint8_t wav_loader_catalog_make_path(char *out,
                                            uint32_t out_size,
                                            const char *dir_path,
                                            const char *name)
{
    if ((out == 0) || (out_size == 0U) || (dir_path == 0) || (name == 0))
    {
        return 0U;
    }
    const size_t dir_len = strlen(dir_path);
    const char *const separator = ((dir_len != 0U) && (dir_path[dir_len - 1U] == '/')) ? "" : "/";
    const int path_len = snprintf(out, out_size, "%s%s%s", dir_path, separator, name);
    return (uint8_t)((path_len >= 0) && ((uint32_t)path_len < out_size));
}

static uint8_t wav_loader_catalog_write_dirent(wav_loader_catalog_build_t *build,
                                               const char *dir_path,
                                               const FILINFO *fno,
                                               uint16_t parent_id,
                                               uint16_t *out_index)
{
    if ((build == 0) || (fno == 0))
    {
        return 0U;
    }

    const uint8_t is_dir = ((fno->fattrib & AM_DIR) != 0U) ? 1U : 0U;
    char path[WAV_LOADER_CATALOG_PATH_MAX];
    if (wav_loader_catalog_make_path(path, sizeof(path), dir_path, fno->fname) == 0U)
    {
        g_wav_catalog_path_truncated = 1U;
        return 0U;
    }

    wav_loader_catalog_entry_t entry;
    if (wav_loader_catalog_fill_entry(&entry,
                                      fno->fname,
                                      path,
                                      parent_id,
                                      (is_dir != 0U) ? WAV_LOADER_CATALOG_ENTRY_DIR : WAV_LOADER_CATALOG_ENTRY_FILE,
                                      fno->fsize,
                                      fno->fdate,
                                      fno->ftime) == 0U)
    {
        g_wav_catalog_path_truncated = 1U;
        return 0U;
    }

    return wav_loader_catalog_write_entry(build, &entry, out_index);
}

static void wav_loader_catalog_scan_dir(wav_loader_catalog_build_t *build,
                                        const char *dir_path,
                                        uint16_t parent_id,
                                        uint8_t depth)
{
    DIR dir;
    FILINFO fno;
    if((build == 0) || (build->fr != FR_OK)
        || (depth > 8U) || (wav_loader_path_is_hidden_system_cache(dir_path) != 0U))
    {
        return;
    }
    FRESULT fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK)
    {
        return;
    }

    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if ((fno.fname[0] == '.') || ((fno.fattrib & AM_DIR) == 0U))
        {
            continue;
        }

        uint16_t entry_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        if (wav_loader_catalog_write_dirent(build, dir_path, &fno, parent_id, &entry_index) == 0U)
        {
            if (g_wav_catalog_truncated != 0U)
            {
                break;
            }
        }
        else if (g_wav_catalog_truncated == 0U)
        {
            char path[WAV_LOADER_CATALOG_PATH_MAX];
            const size_t dir_len = strlen(dir_path);
            const char *const separator = ((dir_len != 0U) && (dir_path[dir_len - 1U] == '/')) ? "" : "/";
            const int path_len = snprintf(path, sizeof(path), "%s%s%s", dir_path, separator, fno.fname);
            if ((path_len >= 0) && ((uint32_t)path_len < sizeof(path)))
            {
                wav_loader_catalog_scan_dir(build, path, entry_index, (uint8_t)(depth + 1U));
            }
            else
            {
                g_wav_catalog_path_truncated = 1U;
            }
        }
    }

    (void)f_closedir(&dir);

    if (build->fr != FR_OK)
    {
        return;
    }

    fr = f_opendir(&dir, dir_path);
    if (fr != FR_OK)
    {
        return;
    }

    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if ((fno.fname[0] == '.')
            || ((fno.fattrib & AM_DIR) != 0U)
            || (!wav_ext_is_wav(fno.fname)))
        {
            continue;
        }
        if (wav_loader_catalog_write_dirent(build, dir_path, &fno, parent_id, 0) == 0U)
        {
            if (g_wav_catalog_truncated != 0U)
            {
                break;
            }
        }
    }

    (void)f_closedir(&dir);
}

static uint8_t wav_loader_catalog_validate_stream(FIL *file, const wav_loader_catalog_file_header_t *header)
{
    if ((file == 0) || (header == 0))
    {
        return 0U;
    }

    uint32_t checksum = 2166136261UL;
    wav_loader_catalog_entry_t entry;
    for (uint16_t i = 0U; i < header->count; ++i)
    {
        UINT read = 0U;
        const FRESULT fr = f_read(file, &entry, sizeof(entry), &read);
        if ((fr != FR_OK) || (read != sizeof(entry)))
        {
            return 0U;
        }
        wav_loader_catalog_checksum_update(&checksum, &entry, sizeof(entry));
    }
    return (checksum == header->checksum) ? 1U : 0U;
}

static uint8_t wav_loader_catalog_read_header(FIL *file, wav_loader_catalog_file_header_t *header)
{
    UINT read = 0U;
    if ((file == 0) || (header == 0))
    {
        return 0U;
    }
    FRESULT fr = f_lseek(file, 0U);
    if (fr != FR_OK)
    {
        return 0U;
    }
    fr = f_read(file, header, sizeof(*header), &read);
    return (uint8_t)((fr == FR_OK)
        && (read == sizeof(*header))
        && (header->magic == WAV_LOADER_CATALOG_MAGIC)
        && (header->version == WAV_LOADER_CATALOG_VERSION)
        && (header->entry_size == sizeof(wav_loader_catalog_entry_t))
        && (header->count <= WAV_LOADER_CATALOG_MAX));
}

static uint8_t wav_loader_catalog_open_read(FIL *file, wav_loader_catalog_file_header_t *header)
{
    if ((file == 0) || (header == 0))
    {
        return 0U;
    }
    if (sd_access_gate_streaming_critical_active() != 0U)
    {
        g_wav_catalog_last_sd_busy = 1U;
        return 0U;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        g_wav_catalog_last_sd_busy = 1U;
        return 0U;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        g_wav_catalog_last_io_error = 1U;
        wav_loader_catalog_release_gate_on_error();
        return 0U;
    }
    const FRESULT open_fr = f_open(file, WAV_LOADER_CATALOG_PATH, FA_READ);
    if (open_fr != FR_OK)
    {
        g_wav_catalog_last_io_error = 1U;
        wav_loader_catalog_diag_record_open_fail(open_fr);
        wav_loader_catalog_release_gate_on_error();
        return 0U;
    }
    if (wav_loader_catalog_read_header(file, header) == 0U)
    {
        g_wav_catalog_last_io_error = 1U;
        (void)f_close(file);
        wav_loader_catalog_release_gate_on_error();
        return 0U;
    }
    return 1U;
}

static uint16_t wav_loader_catalog_page_start(uint16_t child_index)
{
    return (uint16_t)(child_index - (child_index % WAV_LOADER_CATALOG_VIEW_MAX));
}

static wav_loader_catalog_view_t *wav_loader_catalog_find_view(uint16_t parent_id, uint16_t page_start)
{
    for (uint8_t i = 0U; i < WAV_LOADER_CATALOG_VIEW_CACHE_COUNT; ++i)
    {
        if ((g_wav_catalog_views[i].valid != 0U)
            && (g_wav_catalog_views[i].parent_id == parent_id)
            && (g_wav_catalog_views[i].page_start == page_start))
        {
            g_wav_catalog_views[i].age = ++g_wav_catalog_view_age;
            return &g_wav_catalog_views[i];
        }
    }
    return 0;
}

static wav_loader_catalog_view_t *wav_loader_catalog_alloc_view(void)
{
    wav_loader_catalog_view_t *oldest = &g_wav_catalog_views[0];
    for (uint8_t i = 0U; i < WAV_LOADER_CATALOG_VIEW_CACHE_COUNT; ++i)
    {
        if (g_wav_catalog_views[i].valid == 0U)
        {
            return &g_wav_catalog_views[i];
        }
        if (g_wav_catalog_views[i].age < oldest->age)
        {
            oldest = &g_wav_catalog_views[i];
        }
    }
    return oldest;
}

static wav_loader_catalog_view_t *wav_loader_catalog_load_view(uint16_t parent_id, uint16_t child_index)
{
    const uint16_t page_start = wav_loader_catalog_page_start(child_index);
    wav_loader_catalog_view_t *view = wav_loader_catalog_find_view(parent_id, page_start);
    if (view != 0)
    {
        return view;
    }

    if ((g_wav_catalog_loaded == 0U) || (g_wav_catalog_stale != 0U))
    {
        return 0;
    }

    if (g_wav_catalog_view_load.state == WAV_CATALOG_VIEW_IDLE)
    {
        memset(&g_wav_catalog_view_load, 0, sizeof(g_wav_catalog_view_load));
        memset(&g_wav_catalog_scratch_view, 0, sizeof(g_wav_catalog_scratch_view));
        g_wav_catalog_view_load.parent_id = parent_id;
        g_wav_catalog_view_load.page_start = page_start;
        g_wav_catalog_view_load.media_epoch = sd_access_media_epoch();
        g_wav_catalog_view_load.read_ok = 1U;
        g_wav_catalog_view_load.state = WAV_CATALOG_VIEW_MOUNT;
        g_wav_catalog_scratch_view.parent_id = parent_id;
        g_wav_catalog_scratch_view.page_start = page_start;
    }
    return 0;
}

uint8_t wav_loader_catalog_view_busy(void)
{
    return (g_wav_catalog_view_load.state != WAV_CATALOG_VIEW_IDLE) ? 1U : 0U;
}

static sd_scheduler_background_admission_t wav_loader_catalog_view_begin(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = bytes,
        .media_epoch = g_wav_catalog_view_load.media_epoch,
        .kind = kind
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}

wav_loader_catalog_view_service_result_t wav_loader_catalog_view_service(void)
{
    wav_catalog_view_load_t *const load = &g_wav_catalog_view_load;
    if (load->state == WAV_CATALOG_VIEW_IDLE)
    {
        return WAV_LOADER_CATALOG_VIEW_PENDING;
    }

    const uint32_t entries_per_slice =
        SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES / (uint32_t)sizeof(wav_loader_catalog_entry_t);
    const sd_scheduler_background_kind_t kind =
        ((load->state == WAV_CATALOG_VIEW_HEADER) || (load->state == WAV_CATALOG_VIEW_READ))
            ? SD_SCHEDULER_BACKGROUND_DATA
            : SD_SCHEDULER_BACKGROUND_METADATA;
    uint32_t bytes = 0U;
    if (load->state == WAV_CATALOG_VIEW_HEADER)
    {
        bytes = sizeof(load->header);
    }
    else if (load->state == WAV_CATALOG_VIEW_READ)
    {
        const uint32_t remaining = (uint32_t)load->header.count - load->entry_index;
        const uint32_t count = (remaining < entries_per_slice) ? remaining : entries_per_slice;
        bytes = count * (uint32_t)sizeof(wav_loader_catalog_entry_t);
    }

    const sd_scheduler_background_admission_t admission =
        wav_loader_catalog_view_begin(kind, bytes);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return WAV_LOADER_CATALOG_VIEW_PENDING;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        memset(load, 0, sizeof(*load));
        g_wav_catalog_last_io_error = 1U;
        return WAV_LOADER_CATALOG_VIEW_ERROR;
    }

    wav_loader_catalog_view_service_result_t result = WAV_LOADER_CATALOG_VIEW_PENDING;
    switch (load->state)
    {
        case WAV_CATALOG_VIEW_MOUNT:
            if (sd_access_fs_mount_if_needed() != 0U)
            {
                load->state = WAV_CATALOG_VIEW_OPEN;
            }
            else
            {
                load->read_ok = 0U;
                load->state = WAV_CATALOG_VIEW_CLOSE;
            }
            break;

        case WAV_CATALOG_VIEW_OPEN:
            if (f_open(&load->file, WAV_LOADER_CATALOG_PATH, FA_READ) == FR_OK)
            {
                load->file_open = 1U;
                load->state = WAV_CATALOG_VIEW_HEADER;
            }
            else
            {
                load->read_ok = 0U;
                load->state = WAV_CATALOG_VIEW_CLOSE;
            }
            break;

        case WAV_CATALOG_VIEW_HEADER:
        {
            UINT read = 0U;
            const FRESULT fr = f_read(&load->file, &load->header, sizeof(load->header), &read);
            if ((fr == FR_OK) && (read == sizeof(load->header))
                && (load->header.magic == WAV_LOADER_CATALOG_MAGIC)
                && (load->header.version == WAV_LOADER_CATALOG_VERSION)
                && (load->header.entry_size == sizeof(wav_loader_catalog_entry_t))
                && (load->header.count <= WAV_LOADER_CATALOG_MAX))
            {
                load->state = (load->header.count == 0U)
                    ? WAV_CATALOG_VIEW_CLOSE : WAV_CATALOG_VIEW_READ;
            }
            else
            {
                load->read_ok = 0U;
                load->state = WAV_CATALOG_VIEW_CLOSE;
            }
            break;
        }

        case WAV_CATALOG_VIEW_READ:
        {
            const uint32_t remaining = (uint32_t)load->header.count - load->entry_index;
            const uint32_t count = (remaining < entries_per_slice) ? remaining : entries_per_slice;
            for (uint32_t n = 0U; n < count; ++n)
            {
                wav_loader_catalog_entry_t entry;
                UINT read = 0U;
                const FRESULT fr = f_read(&load->file, &entry, sizeof(entry), &read);
                if ((fr != FR_OK) || (read != sizeof(entry)))
                {
                    load->read_ok = 0U;
                    break;
                }
                if (entry.parent_id == load->parent_id)
                {
                    g_wav_catalog_scratch_view.child_count++;
                    if ((load->child_ordinal >= load->page_start)
                        && (g_wav_catalog_scratch_view.loaded_count < WAV_LOADER_CATALOG_VIEW_MAX))
                    {
                        const uint16_t dst = g_wav_catalog_scratch_view.loaded_count++;
                        g_wav_catalog_scratch_view.entries[dst] = entry;
                        g_wav_catalog_scratch_view.indices[dst] = load->entry_index;
                    }
                    load->child_ordinal++;
                }
                load->entry_index++;
            }
            if ((load->read_ok == 0U) || (load->entry_index >= load->header.count))
            {
                load->state = WAV_CATALOG_VIEW_CLOSE;
            }
            break;
        }

        case WAV_CATALOG_VIEW_CLOSE:
            if (load->file_open != 0U)
            {
                if (f_close(&load->file) != FR_OK)
                {
                    load->read_ok = 0U;
                }
                load->file_open = 0U;
            }
            if (load->read_ok != 0U)
            {
                wav_loader_catalog_view_t *const view = wav_loader_catalog_alloc_view();
                *view = g_wav_catalog_scratch_view;
                view->valid = 1U;
                view->age = ++g_wav_catalog_view_age;
                result = WAV_LOADER_CATALOG_VIEW_PUBLISHED;
            }
            else
            {
                g_wav_catalog_last_io_error = 1U;
                g_wav_catalog_diag.catalog_view_preserved_on_error_count++;
                result = WAV_LOADER_CATALOG_VIEW_ERROR;
            }
            memset(load, 0, sizeof(*load));
            break;

        default:
            memset(load, 0, sizeof(*load));
            break;
    }
    sd_scheduler_runtime_background_end();
    return result;
}

static uint8_t wav_loader_catalog_read_entry_by_index(uint16_t index, wav_loader_catalog_entry_t *out)
{
    if ((out == 0) || (g_wav_catalog_loaded == 0U) || (index >= g_wav_catalog_count))
    {
        return 0U;
    }
    for (uint8_t v = 0U; v < WAV_LOADER_CATALOG_VIEW_CACHE_COUNT; ++v)
    {
        wav_loader_catalog_view_t *const view = &g_wav_catalog_views[v];
        if (view->valid == 0U)
        {
            continue;
        }
        for (uint16_t i = 0U; i < view->loaded_count; ++i)
        {
            if (view->indices[i] == index)
            {
                *out = view->entries[i];
                view->age = ++g_wav_catalog_view_age;
                return 1U;
            }
        }
    }

    FIL file;
    wav_loader_catalog_file_header_t header;
    if (wav_loader_catalog_open_read(&file, &header) == 0U)
    {
        return 0U;
    }
    FRESULT fr = f_lseek(&file,
                         (FSIZE_t)sizeof(header)
                         + ((FSIZE_t)index * (FSIZE_t)sizeof(*out)));
    UINT read = 0U;
    if (fr == FR_OK)
    {
        fr = f_read(&file, out, sizeof(*out), &read);
    }
    if ((fr != FR_OK) || (read != sizeof(*out)))
    {
        g_wav_catalog_last_io_error = 1U;
        g_wav_catalog_diag.catalog_view_preserved_on_error_count++;
    }
    (void)f_close(&file);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    return (uint8_t)((fr == FR_OK) && (read == sizeof(*out)));
}

static uint8_t wav_loader_catalog_save(void)
{
    FIL file;
    UINT written = 0U;
    if (f_mkdir("0:/BRICK") != FR_OK)
    {
        /* Existing directory also reports an error on some FatFs builds. */
    }

    if (f_open(&file, WAV_LOADER_CATALOG_PATH, FA_CREATE_ALWAYS | FA_READ | FA_WRITE) != FR_OK)
    {
        return 0U;
    }

    wav_loader_catalog_file_header_t header;
    header.magic = WAV_LOADER_CATALOG_MAGIC;
    header.version = WAV_LOADER_CATALOG_VERSION;
    header.entry_size = (uint16_t)sizeof(wav_loader_catalog_entry_t);
    header.capacity = WAV_LOADER_CATALOG_MAX;
    header.count = 0U;
    header.checksum = 0U;

    FRESULT fr = f_write(&file, &header, sizeof(header), &written);
    wav_loader_catalog_build_t build;
    build.file = &file;
    build.fr = ((fr == FR_OK) && (written == sizeof(header))) ? FR_OK : FR_INT_ERR;
    build.checksum = 2166136261UL;
    if (build.fr == FR_OK)
    {
        wav_loader_catalog_scan_dir(&build, WAV_LOADER_SAMPLE_ROOT, WAV_LOADER_CATALOG_ROOT_PARENT, 0U);
        fr = build.fr;
    }
    if ((fr == FR_OK) && (f_lseek(&file, 0U) == FR_OK))
    {
        header.count = g_wav_catalog_count;
        header.checksum = build.checksum;
        written = 0U;
        fr = f_write(&file, &header, sizeof(header), &written);
        if (written != sizeof(header))
        {
            fr = FR_INT_ERR;
        }
    }
    if (fr == FR_OK)
    {
        fr = f_sync(&file);
    }
    (void)f_close(&file);
    return (fr == FR_OK) ? 1U : 0U;
}

void wav_loader_catalog_init_load(void)
{
#if WAV_LOADER_HAS_FATFS
    wav_loader_catalog_file_header_t header;

    wav_loader_catalog_clear();

    FIL file;
    if (wav_loader_catalog_open_read(&file, &header) == 0U)
    {
        return;
    }
    if (wav_loader_catalog_validate_stream(&file, &header) != 0U)
    {
        g_wav_catalog_count = header.count;
        g_wav_catalog_loaded = 1U;
        g_wav_catalog_truncated = (header.count >= header.capacity) ? 1U : 0U;
        g_wav_catalog_stale = 0U;
    }
    (void)f_close(&file);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
#else
    wav_loader_catalog_clear();
#endif
}

void wav_loader_catalog_rebuild(void)
{
#if WAV_LOADER_HAS_FATFS
    g_wav_catalog_last_sd_busy = 0U;
    if ((audio_recorder_is_active() != 0U)
            || (sd_access_gate_streaming_critical_active() != 0U))
    {
        g_wav_catalog_last_sd_busy = 1U;
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        g_wav_catalog_last_sd_busy = 1U;
        return;
    }

    const uint16_t old_count = g_wav_catalog_count;
    const uint8_t old_loaded = g_wav_catalog_loaded;
    const uint8_t old_truncated = g_wav_catalog_truncated;
    const uint8_t old_path_truncated = g_wav_catalog_path_truncated;
    const uint8_t old_stale = g_wav_catalog_stale;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        return;
    }

    g_wav_catalog_count = 0U;
    g_wav_catalog_truncated = 0U;
    g_wav_catalog_path_truncated = 0U;
    g_wav_catalog_loaded = 0U;
    g_wav_catalog_stale = 1U;
    if (wav_loader_catalog_save() != 0U)
    {
        wav_loader_catalog_views_clear();
        g_wav_catalog_stale = 0U;
        g_wav_catalog_loaded = 1U;
    }
    else
    {
        g_wav_catalog_count = old_count;
        g_wav_catalog_loaded = old_loaded;
        g_wav_catalog_truncated = old_truncated;
        g_wav_catalog_path_truncated = old_path_truncated;
        g_wav_catalog_stale = old_stale;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
#else
    wav_loader_catalog_clear();
#endif
}

void wav_loader_catalog_refresh(void)
{
    wav_loader_catalog_rebuild();
}

uint16_t wav_loader_catalog_count(void)
{
    return g_wav_catalog_count;
}

uint16_t wav_loader_catalog_child_count(uint16_t parent_id)
{
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    for (uint8_t i = 0U; i < WAV_LOADER_CATALOG_VIEW_CACHE_COUNT; ++i)
    {
        if ((g_wav_catalog_views[i].valid != 0U)
            && (g_wav_catalog_views[i].parent_id == parent_id))
        {
            g_wav_catalog_views[i].age = ++g_wav_catalog_view_age;
            return g_wav_catalog_views[i].child_count;
        }
    }
    wav_loader_catalog_view_t *const view = wav_loader_catalog_load_view(parent_id, 0U);
    if (view == 0)
    {
        return 0U;
    }
    return view->child_count;
}

uint8_t wav_loader_catalog_last_sd_busy(void)
{
    return g_wav_catalog_last_sd_busy;
}

uint8_t wav_loader_catalog_last_io_error(void)
{
    return g_wav_catalog_last_io_error;
}

const wav_loader_catalog_diag_t *wav_loader_catalog_get_diag(void)
{
    return &g_wav_catalog_diag;
}

uint16_t wav_loader_catalog_get_child_index(uint16_t parent_id, uint16_t child_index)
{
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    wav_loader_catalog_view_t *const view = wav_loader_catalog_load_view(parent_id, child_index);
    if ((view == 0) || (child_index < view->page_start))
    {
        return WAV_LOADER_CATALOG_ROOT_PARENT;
    }
    const uint16_t local_index = (uint16_t)(child_index - view->page_start);
    if (local_index >= view->loaded_count)
    {
        return WAV_LOADER_CATALOG_ROOT_PARENT;
    }
    return view->indices[local_index];
}

uint8_t wav_loader_catalog_truncated(void)
{
    return g_wav_catalog_truncated;
}

uint8_t wav_loader_catalog_path_truncated(void)
{
    return g_wav_catalog_path_truncated;
}

uint8_t wav_loader_catalog_loaded(void)
{
    return g_wav_catalog_loaded;
}

uint8_t wav_loader_catalog_stale(void)
{
    return g_wav_catalog_stale;
}

void wav_loader_catalog_mark_stale(void)
{
    g_wav_catalog_stale = 1U;
    wav_loader_catalog_views_clear();
}

uint8_t wav_loader_catalog_find_path(const char *path, uint16_t *out_index, wav_loader_catalog_entry_t *out_entry)
{
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    if ((path == 0) || (path[0] == '\0') || (g_wav_catalog_loaded == 0U))
    {
        return 0U;
    }

    for (uint8_t v = 0U; v < WAV_LOADER_CATALOG_VIEW_CACHE_COUNT; ++v)
    {
        wav_loader_catalog_view_t *const view = &g_wav_catalog_views[v];
        if (view->valid == 0U)
        {
            continue;
        }
        for (uint16_t i = 0U; i < view->loaded_count; ++i)
        {
            if (strcmp(view->entries[i].path, path) == 0)
            {
                if (out_index != 0)
                {
                    *out_index = view->indices[i];
                }
                if (out_entry != 0)
                {
                    *out_entry = view->entries[i];
                }
                view->age = ++g_wav_catalog_view_age;
                return 1U;
            }
        }
    }

    FIL file;
    wav_loader_catalog_file_header_t header;
    if (wav_loader_catalog_open_read(&file, &header) == 0U)
    {
        return 0U;
    }

    wav_loader_catalog_entry_t entry;
    uint8_t found = 0U;
    for (uint16_t i = 0U; i < header.count; ++i)
    {
        UINT read = 0U;
        FRESULT fr = f_read(&file, &entry, sizeof(entry), &read);
        if ((fr != FR_OK) || (read != sizeof(entry)))
        {
            g_wav_catalog_last_io_error = 1U;
            g_wav_catalog_diag.catalog_view_preserved_on_error_count++;
            break;
        }
        if (strcmp(entry.path, path) == 0)
        {
            if (out_index != 0)
            {
                *out_index = i;
            }
            if (out_entry != 0)
            {
                *out_entry = entry;
            }
            found = 1U;
            break;
        }
    }
    (void)f_close(&file);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    return found;
}

const wav_loader_catalog_entry_t *wav_loader_catalog_get(uint16_t index)
{
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    if ((g_wav_catalog_lookup_valid != 0U) && (g_wav_catalog_lookup_index == index))
    {
        return &g_wav_catalog_lookup_entry;
    }
    if (wav_loader_catalog_read_entry_by_index(index, &g_wav_catalog_lookup_entry) == 0U)
    {
        g_wav_catalog_lookup_valid = 0U;
        return 0;
    }
    g_wav_catalog_lookup_index = index;
    g_wav_catalog_lookup_valid = 1U;
    return &g_wav_catalog_lookup_entry;
}

const wav_loader_catalog_entry_t *wav_loader_catalog_get_child(uint16_t parent_id, uint16_t child_index)
{
    g_wav_catalog_last_sd_busy = 0U;
    g_wav_catalog_last_io_error = 0U;
    wav_loader_catalog_view_t *const view = wav_loader_catalog_load_view(parent_id, child_index);
    if ((view == 0) || (child_index < view->page_start))
    {
        return 0;
    }
    const uint16_t local_index = (uint16_t)(child_index - view->page_start);
    if (local_index >= view->loaded_count)
    {
        return 0;
    }
    return &view->entries[local_index];
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

    if (g_wav_catalog_count == 0U)
    {        return false;
    }

#if WAV_LOADER_HAS_FATFS
    FIL file;
    wav_loader_catalog_file_header_t header;
    if (wav_loader_catalog_open_read(&file, &header) == 0U)
    {
        return false;
    }

    wav_loader_catalog_entry_t entry;
    uint8_t found = 0U;
    for (uint16_t i = 0U; i < header.count; ++i)
    {
        UINT read = 0U;
        FRESULT fr = f_read(&file, &entry, sizeof(entry), &read);
        if ((fr != FR_OK) || (read != sizeof(entry)))
        {
            break;
        }
        if (entry.type == WAV_LOADER_CATALOG_ENTRY_FILE)
        {
            found = 1U;
            break;
        }
    }
    (void)f_close(&file);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if (found == 0U)
    {
        return false;
    }

    if ((snprintf(out_path, max_len, "%s", entry.path) < 0)
        || (strlen(out_path) >= max_len))
    {
        return false;
    }
    return true;
#else
    return false;
#endif
}
