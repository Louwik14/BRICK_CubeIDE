#include "Storage/storage_catalog.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "Platform/memory_layout.h"
#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_audio_format.h"
#include "Sampler/sample_page_cache_config.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_io_wakeup.h"

typedef struct
{
    storage_catalog_kind_t kind;
    char path[WAV_LOADER_CATALOG_PATH_MAX];
} storage_catalog_request_t;

typedef struct
{
    storage_catalog_kind_t kind;
    char path[WAV_LOADER_CATALOG_PATH_MAX];
    uint16_t count;
    storage_catalog_entry_t entries[STORAGE_CATALOG_CAPACITY];
} storage_catalog_view_t;

STORAGE_STATE_SDRAM static storage_catalog_view_t g_storage_catalog_view;
static storage_catalog_request_t g_storage_catalog_request;
static volatile uint8_t g_storage_catalog_request_valid;
static volatile uint32_t g_storage_catalog_seq;

#define STORAGE_CATALOG_ENTRIES_PER_PASS (8U)

typedef enum
{
    STORAGE_CATALOG_SCAN_IDLE = 0,
    STORAGE_CATALOG_SCAN_WAVETABLE_DIRS,
    STORAGE_CATALOG_SCAN_WAVETABLE_FILES,
    STORAGE_CATALOG_SCAN_MULTI_ROOT,
    STORAGE_CATALOG_SCAN_MULTI_CLASSIFY,
    STORAGE_CATALOG_SCAN_MULTI_SUMMARY
} storage_catalog_scan_state_t;

typedef struct
{
    storage_catalog_scan_state_t state;
    storage_catalog_request_t request;
    DIR dir;
    DIR classify_dir;
    uint8_t dir_open;
    uint8_t classify_open;
    uint8_t classify_has_subdir;
    uint16_t classify_wav_count;
    uint16_t classify_entry_index;
    uint16_t summary_index;
    uint16_t count;
    uint32_t media_epoch;
    uint8_t error;
    volatile uint8_t active;
} storage_catalog_scan_t;

static storage_catalog_scan_t g_storage_catalog_scan;

static uint8_t storage_catalog_copy_path(char *out, uint32_t size, const char *path)
{
    const int written = (path != NULL) ? snprintf(out, size, "%s", path) : -1;
    return (written >= 0 && (uint32_t)written < size) ? 1U : 0U;
}

static uint8_t storage_catalog_is_wav(const char *name)
{
    const size_t len = (name != NULL) ? strlen(name) : 0U;
    if (len < 4U) return 0U;
    return (uint8_t)(name[len - 4U] == '.'
        && (name[len - 3U] == 'w' || name[len - 3U] == 'W')
        && (name[len - 2U] == 'a' || name[len - 2U] == 'A')
        && (name[len - 1U] == 'v' || name[len - 1U] == 'V'));
}

static uint8_t storage_catalog_append(storage_catalog_entry_t *entries,
                                      uint16_t *count,
                                      const char *dir,
                                      const FILINFO *info,
                                      uint8_t want_dir)
{
    if ((entries == NULL) || (count == NULL) || (dir == NULL) || (info == NULL)
        || (*count >= STORAGE_CATALOG_CAPACITY) || (info->fname[0] == '.')
        || ((((info->fattrib & AM_DIR) != 0U) ? 1U : 0U) != want_dir)
        || ((want_dir == 0U) && (storage_catalog_is_wav(info->fname) == 0U)))
        return 0U;

    storage_catalog_entry_t *const entry = &entries[*count];
    memset(entry, 0, sizeof(*entry));
    const int name_written = snprintf(entry->name, sizeof(entry->name), "%s", info->fname);
    const int path_written = snprintf(entry->path, sizeof(entry->path), "%s/%s", dir, info->fname);
    if ((name_written < 0) || ((uint32_t)name_written >= sizeof(entry->name))
        || (path_written < 0) || ((uint32_t)path_written >= sizeof(entry->path)))
        return 0U;
    entry->is_dir = want_dir;
    (*count)++;
    return 1U;
}

static uint8_t storage_catalog_begin_metadata(void)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = 0U,
        .media_epoch = sd_access_media_epoch(),
        .kind = SD_SCHEDULER_BACKGROUND_METADATA};
    return (sd_scheduler_runtime_background_try_begin(&request)
            == SD_SCHEDULER_BACKGROUND_GO) ? 1U : 0U;
}

static uint8_t storage_catalog_make_index_path(char *out, uint32_t size, const char *path)
{
    const char *const slash = (path != NULL) ? strrchr(path, '/') : NULL;
    const char *const name = (slash != NULL) ? slash + 1 : path;
    if ((name == NULL) || (name[0] == '\0')) return 0U;
    const char *const folder = (slash != NULL) ? path : "0:";
    const size_t folder_len = (slash != NULL) ? (size_t)(slash - path) : 2U;
    const int written = snprintf(out, size, "%.*s/%s.brickmulti",
                                 (int)folder_len, folder, name);
    return (written >= 0 && (uint32_t)written < size) ? 1U : 0U;
}

static void storage_catalog_classify_folder_close(void)
{
    if (g_storage_catalog_scan.classify_open != 0U)
    {
        (void)f_closedir(&g_storage_catalog_scan.classify_dir);
        g_storage_catalog_scan.classify_open = 0U;
    }
}

static uint8_t storage_catalog_classify_folder_begin(const char *path)
{
    g_storage_catalog_scan.classify_has_subdir = 0U;
    g_storage_catalog_scan.classify_wav_count = 0U;
    if (f_opendir(&g_storage_catalog_scan.classify_dir, path) != FR_OK)
        return 0U;
    g_storage_catalog_scan.classify_open = 1U;
    return 1U;
}

static uint8_t storage_catalog_classify_folder_step(uint8_t *complete,
                                                    uint8_t *out_type,
                                                    uint16_t *out_wav_count)
{
    FILINFO info;
    if ((complete == NULL) || (out_type == NULL) || (out_wav_count == NULL))
        return 0U;

    *complete = 0U;
    *out_type = 2U;
    *out_wav_count = g_storage_catalog_scan.classify_wav_count;
    if (g_storage_catalog_scan.classify_open == 0U)
    {
        *complete = 1U;
        return 1U;
    }

    memset(&info, 0, sizeof(info));
    const FRESULT result = f_readdir(&g_storage_catalog_scan.classify_dir, &info);
    if (result != FR_OK)
    {
        g_storage_catalog_scan.error = 1U;
        storage_catalog_classify_folder_close();
        *complete = 1U;
        return 1U;
    }
    if (info.fname[0] == '\0')
    {
        storage_catalog_classify_folder_close();
        *complete = 1U;
        *out_wav_count = g_storage_catalog_scan.classify_wav_count;
        *out_type = (g_storage_catalog_scan.classify_wav_count != 0U)
            ? 0U : ((g_storage_catalog_scan.classify_has_subdir != 0U) ? 1U : 2U);
        return 1U;
    }

    if (info.fname[0] != '.')
    {
        if ((info.fattrib & AM_DIR) != 0U)
            g_storage_catalog_scan.classify_has_subdir = 1U;
        else if ((storage_catalog_is_wav(info.fname) != 0U)
                 && (g_storage_catalog_scan.classify_wav_count < UINT16_MAX))
            g_storage_catalog_scan.classify_wav_count++;
    }
    *out_wav_count = g_storage_catalog_scan.classify_wav_count;
    return 1U;
}

static void storage_catalog_fill_multi_summary(storage_catalog_entry_t *entry)
{
    multi_sample_index_t index;
    uint16_t samples = 0U;
    uint16_t zones = 0U;
    if ((entry == NULL) || (entry->index_path[0] == '\0')
        || (multi_sample_index_peek_counts(entry->index_path, &samples, &zones)
            != MULTI_SAMPLE_INDEX_OK))
        return;
    entry->sample_count = samples;
    entry->zone_count = zones;
    if (multi_sample_index_load(entry->index_path, &index) != MULTI_SAMPLE_INDEX_OK)
        return;
    uint32_t bytes = 0U;
    for (uint16_t i = 0U; i < index.sample_count; ++i)
    {
        const sample_audio_format_t format =
            sample_audio_format_from_channels(index.samples[i].channels);
        bytes += sample_audio_format_multi_start_slot_cost(format)
            * SAMPLE_PREP_MULTI_START_SLOT_PAGES * SAMPLE_PAGE_BYTES;
    }
    const uint32_t slot_bytes = SAMPLE_PREP_MULTI_START_SLOT_PAGES * SAMPLE_PAGE_BYTES;
    const uint32_t slots = (slot_bytes == 0U) ? 0U : ((bytes + slot_bytes - 1U) / slot_bytes);
    entry->slot_cost = (slots > UINT16_MAX) ? UINT16_MAX : (uint16_t)slots;
    entry->prepared = 1U;
}

static void storage_catalog_scan_close(void)
{
    if (g_storage_catalog_scan.dir_open != 0U)
    {
        (void)f_closedir(&g_storage_catalog_scan.dir);
        g_storage_catalog_scan.dir_open = 0U;
    }
    storage_catalog_classify_folder_close();
}

static uint8_t storage_catalog_scan_wavetable_phase(uint8_t want_dir,
                                                    uint32_t *budget)
{
    while (*budget != 0U)
    {
        FILINFO info;
        if (g_storage_catalog_scan.dir_open == 0U)
        {
            if (f_opendir(&g_storage_catalog_scan.dir,
                          g_storage_catalog_scan.request.path) != FR_OK)
            {
                g_storage_catalog_scan.error = 1U;
                return 2U;
            }
            g_storage_catalog_scan.dir_open = 1U;
        }
        memset(&info, 0, sizeof(info));
        const FRESULT result = f_readdir(&g_storage_catalog_scan.dir, &info);
        (*budget)--;
        if (result != FR_OK)
        {
            g_storage_catalog_scan.error = 1U;
            storage_catalog_scan_close();
            return 3U;
        }
        if (info.fname[0] == '\0')
        {
            storage_catalog_scan_close();
            return 1U;
        }
        (void)storage_catalog_append(g_storage_catalog_view.entries,
                                     &g_storage_catalog_scan.count,
                                     g_storage_catalog_scan.request.path,
                                     &info, want_dir);
        if (g_storage_catalog_scan.count >= STORAGE_CATALOG_CAPACITY)
        {
            storage_catalog_scan_close();
            return 1U;
        }
    }
    return 0U;
}

static uint8_t storage_catalog_scan_multi(uint32_t *budget,
                                          uint8_t allow_summary)
{
    while (*budget != 0U)
    {
        if ((g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_SUMMARY)
            && (allow_summary == 0U))
            return 0U;
        if (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_ROOT)
        {
            if (g_storage_catalog_scan.count >= STORAGE_CATALOG_CAPACITY)
            {
                storage_catalog_scan_close();
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_MULTI_SUMMARY;
                continue;
            }
            if (g_storage_catalog_scan.dir_open == 0U)
            {
                if (f_opendir(&g_storage_catalog_scan.dir,
                              g_storage_catalog_scan.request.path) != FR_OK)
                {
                    g_storage_catalog_scan.error = 1U;
                    g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
                    return 1U;
                }
                g_storage_catalog_scan.dir_open = 1U;
            }

            FILINFO info;
            memset(&info, 0, sizeof(info));
            const FRESULT result = f_readdir(&g_storage_catalog_scan.dir, &info);
            (*budget)--;
            if (result != FR_OK)
            {
                g_storage_catalog_scan.error = 1U;
                storage_catalog_scan_close();
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
                return 1U;
            }
            if (info.fname[0] == '\0')
            {
                storage_catalog_scan_close();
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_MULTI_SUMMARY;
                continue;
            }
            if ((info.fname[0] == '.') || ((info.fattrib & AM_DIR) == 0U))
                continue;
            storage_catalog_entry_t *const entry =
                &g_storage_catalog_view.entries[g_storage_catalog_scan.count];
            memset(entry, 0, sizeof(*entry));
            if ((snprintf(entry->name, sizeof(entry->name), "%s", info.fname) < 0)
                || (snprintf(entry->path, sizeof(entry->path), "%s/%s",
                             g_storage_catalog_scan.request.path, info.fname) < 0))
                continue;
            entry->is_dir = 1U;
            (void)storage_catalog_make_index_path(entry->index_path,
                                                   sizeof(entry->index_path), entry->path);
            g_storage_catalog_scan.classify_entry_index = g_storage_catalog_scan.count;
            g_storage_catalog_scan.count++;
            if (storage_catalog_classify_folder_begin(entry->path) == 0U)
            {
                entry->multi_type = 2U;
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_MULTI_ROOT;
            }
            else
            {
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_MULTI_CLASSIFY;
            }
        }
        else if (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_CLASSIFY)
        {
            uint8_t complete = 0U;
            uint8_t type = 2U;
            uint16_t wav_count = 0U;
            (void)storage_catalog_classify_folder_step(&complete, &type, &wav_count);
            (*budget)--;
            if (complete != 0U)
            {
                storage_catalog_entry_t *const entry =
                    &g_storage_catalog_view.entries[g_storage_catalog_scan.classify_entry_index];
                entry->multi_type = type;
                entry->wav_count = wav_count;
                g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_MULTI_ROOT;
            }
        }
        else if (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_SUMMARY)
        {
            if (g_storage_catalog_scan.summary_index >= g_storage_catalog_scan.count)
                return 1U;
            storage_catalog_entry_t *const entry =
                &g_storage_catalog_view.entries[g_storage_catalog_scan.summary_index++];
            if ((entry->multi_type == 0U) && (entry->index_path[0] != '\0'))
                storage_catalog_fill_multi_summary(entry);
            (*budget)--;
        }
        else
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t storage_catalog_scan_step(void)
{
    uint32_t budget = STORAGE_CATALOG_ENTRIES_PER_PASS;
    const storage_catalog_scan_state_t initial_state = g_storage_catalog_scan.state;
    if (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_WAVETABLE_DIRS)
    {
        const uint8_t result = storage_catalog_scan_wavetable_phase(1U, &budget);
        if (result == 1U)
            g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_WAVETABLE_FILES;
        else if (result != 0U)
            g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
    }
    if ((budget != 0U)
        && (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_WAVETABLE_FILES))
    {
        if (storage_catalog_scan_wavetable_phase(0U, &budget) != 0U)
            g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
    }
    if ((budget != 0U)
        && (g_storage_catalog_scan.state >= STORAGE_CATALOG_SCAN_MULTI_ROOT))
    {
        if (storage_catalog_scan_multi(&budget,
                                       (initial_state == STORAGE_CATALOG_SCAN_MULTI_SUMMARY)
                                           ? 1U : 0U) != 0U)
            g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
        else if ((initial_state != STORAGE_CATALOG_SCAN_MULTI_SUMMARY)
                 && (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_SUMMARY))
            return 0U;
    }
    return (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_IDLE) ? 1U : 0U;
}

uint8_t storage_catalog_request(storage_catalog_kind_t kind, const char *path)
{
    if ((path == NULL) || (path[0] == '\0')
        || (g_storage_catalog_scan.active != 0U)
        || (g_storage_catalog_request_valid != 0U)
        || (storage_catalog_copy_path(g_storage_catalog_request.path,
                                      sizeof(g_storage_catalog_request.path), path) == 0U))
        return 0U;
    g_storage_catalog_request.kind = kind;
    __DMB();
    g_storage_catalog_request_valid = 1U;
    storage_io_wakeup(STORAGE_IO_WAKE_WORK);
    return 1U;
}

void storage_catalog_service(void)
{
    uint8_t background_gate_held = 0U;
    uint8_t project_gate_held = 0U;
    if (g_storage_catalog_scan.active == 0U)
    {
        if (g_storage_catalog_request_valid == 0U) return;
        if (storage_catalog_begin_metadata() == 0U) return;
        background_gate_held = 1U;
        if (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)
            (void)sd_access_fs_reprobe_if_no_media();
        g_storage_catalog_scan.request = g_storage_catalog_request;
        g_storage_catalog_request_valid = 0U;
        g_storage_catalog_scan.active = 1U;
        g_storage_catalog_scan.count = 0U;
        g_storage_catalog_scan.summary_index = 0U;
        g_storage_catalog_scan.error = 0U;
        g_storage_catalog_scan.media_epoch = sd_access_media_epoch();
        g_storage_catalog_scan.state = (g_storage_catalog_scan.request.kind == STORAGE_CATALOG_MULTI)
            ? STORAGE_CATALOG_SCAN_MULTI_ROOT : STORAGE_CATALOG_SCAN_WAVETABLE_DIRS;
        g_storage_catalog_seq++;
        g_storage_catalog_view.kind = g_storage_catalog_scan.request.kind;
        (void)storage_catalog_copy_path(g_storage_catalog_view.path,
                                        sizeof(g_storage_catalog_view.path),
                                        g_storage_catalog_scan.request.path);
        memset(g_storage_catalog_view.entries, 0, sizeof(g_storage_catalog_view.entries));
        g_storage_catalog_view.count = 0U;
    }
    else if (g_storage_catalog_scan.state == STORAGE_CATALOG_SCAN_MULTI_SUMMARY)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
            return;
        project_gate_held = 1U;
    }
    else if (storage_catalog_begin_metadata() == 0U)
    {
        return;
    }
    else
    {
        background_gate_held = 1U;
    }

    if (g_storage_catalog_scan.media_epoch != sd_access_media_epoch())
    {
        storage_catalog_scan_close();
        g_storage_catalog_scan.active = 0U;
        g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
        __DMB();
        g_storage_catalog_seq++;
        if (project_gate_held != 0U)
            sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        if (background_gate_held != 0U)
            sd_scheduler_runtime_background_end();
        return;
    }

    if (sd_access_fs_mount_if_needed() != 0U)
    {
        if (storage_catalog_scan_step() != 0U)
        {
            storage_catalog_scan_close();
            g_storage_catalog_view.count = (g_storage_catalog_scan.error == 0U)
                ? g_storage_catalog_scan.count : 0U;
            g_storage_catalog_scan.active = 0U;
            g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
            __DMB();
            g_storage_catalog_seq++;
        }
    }
    else
    {
        storage_catalog_scan_close();
        g_storage_catalog_scan.active = 0U;
        g_storage_catalog_scan.state = STORAGE_CATALOG_SCAN_IDLE;
        __DMB();
        g_storage_catalog_seq++;
    }
    if (project_gate_held != 0U)
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    if (background_gate_held != 0U)
        sd_scheduler_runtime_background_end();
}

uint8_t storage_catalog_snapshot_begin(storage_catalog_kind_t kind,
                                       const char *path,
                                       storage_catalog_snapshot_t *snapshot)
{
    if ((path == NULL) || (snapshot == NULL))
        return 0U;
    if (g_storage_catalog_request_valid != 0U)
        return 0U;
    const uint32_t before = g_storage_catalog_seq;
    if ((before & 1U) != 0U) return 0U;
    __DMB();
    if ((g_storage_catalog_view.kind != kind)
        || (strcmp(g_storage_catalog_view.path, path) != 0))
        return 0U;

    snapshot->entries = g_storage_catalog_view.entries;
    snapshot->count = g_storage_catalog_view.count;
    snapshot->sequence = before;
    return 1U;
}

uint8_t storage_catalog_snapshot_end(const storage_catalog_snapshot_t *snapshot)
{
    if (snapshot == NULL) return 0U;
    __DMB();
    return (snapshot->sequence == g_storage_catalog_seq) ? 1U : 0U;
}
