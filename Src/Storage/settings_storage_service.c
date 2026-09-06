#include "Storage/settings_storage_service.h"

#include <string.h>
#include <stdio.h>

#include "App/control_domain.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Storage/audio_recorder.h"
#include "Storage/project_control.h"
#include "Storage/storage_catalog.h"
#include "Storage/wav_convert.h"
#include "Storage/persistent_control_model.h"
#include "Storage/storage_io_wakeup.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "UI/ui_service_wakeup.h"

#define SETTINGS_EVENT_QUEUE_CAPACITY 16U
#define SETTINGS_EVENT_QUEUE_MASK (SETTINGS_EVENT_QUEUE_CAPACITY - 1U)
#define SETTINGS_MULTI_CLEAR_MAX 240U
#define SETTINGS_PATH_MAX 160U

typedef enum
{
    SETTINGS_MULTI_IDLE = 0U,
    SETTINGS_MULTI_IMPORT,
    SETTINGS_MULTI_WAIT_CATALOG,
    SETTINGS_MULTI_LOAD
} settings_multi_state_t;

STORAGE_STATE_SDRAM static volatile storage_settings_event_t
    g_settings_events[SETTINGS_EVENT_QUEUE_CAPACITY];
static volatile uint32_t g_settings_event_head;
static volatile uint32_t g_settings_event_tail;
static uint8_t g_settings_ui_wake_pending;

static uint8_t g_classic_pending;
static uint16_t g_classic_slot;
static uint8_t g_asset_pending;
static uint8_t g_asset_requested;
static uint32_t g_asset_kind;
static uint16_t g_asset_runtime;

static uint8_t g_convert_pending;
static uint16_t g_convert_slot;
static char g_convert_path[SETTINGS_PATH_MAX];
static uint8_t g_convert_percent = 0xFFU;

static settings_multi_state_t g_multi_state;
static uint16_t g_multi_slot;
static char g_multi_path[SETTINGS_PATH_MAX];
static char g_multi_index_path[SETTINGS_PATH_MAX];
static char g_multi_catalog_dir[SETTINGS_PATH_MAX];
static uint16_t g_multi_done;
static uint16_t g_multi_total;

STORAGE_STATE_SDRAM static char
    g_clear_paths[SETTINGS_MULTI_CLEAR_MAX][MULTI_SAMPLE_POOL_PATH_MAX];
static uint16_t g_clear_count;
static uint16_t g_clear_index;
static uint16_t g_clear_deleted;
static uint8_t g_clear_failed;
static uint8_t g_clear_pending;

static wav_loader_catalog_view_service_result_t g_catalog_result_seen =
    WAV_LOADER_CATALOG_VIEW_PENDING;

static storage_io_owner_t settings_asset_owner(void)
{
    switch (g_asset_kind)
    {
        case PERSIST_ASSET_SAMPLE_STREAM: return STORAGE_OWNER_STREAM;
        case PERSIST_ASSET_SAMPLE_RAM: return STORAGE_OWNER_SAMPLE_RAM;
        case PERSIST_ASSET_WAVETABLE: return STORAGE_OWNER_WAVETABLE;
        case PERSIST_ASSET_MULTI: return STORAGE_OWNER_MULTI;
        default: return STORAGE_OWNER_PROJECT;
    }
}

static void settings_copy_path(char *out, const char *path)
{
    if (out == 0) return;
    if (path == 0) path = "";
    (void)snprintf(out, SETTINGS_PATH_MAX, "%s", path);
}

static void settings_copy_multi_path(char *out, const char *path)
{
    if (out == 0) return;
    if (path == 0) path = "";
    (void)snprintf(out, MULTI_SAMPLE_POOL_PATH_MAX, "%s", path);
}

static uint8_t settings_event_is_ui_visible(storage_settings_event_type_t type)
{
    switch (type)
    {
        case STORAGE_SETTINGS_EVENT_CLASSIC_READY:
        case STORAGE_SETTINGS_EVENT_CLASSIC_FAILED:
        case STORAGE_SETTINGS_EVENT_ASSET_READY:
        case STORAGE_SETTINGS_EVENT_CATALOG_READY:
        case STORAGE_SETTINGS_EVENT_CATALOG_FAILED:
        case STORAGE_SETTINGS_EVENT_MULTI_PROGRESS:
        case STORAGE_SETTINGS_EVENT_MULTI_READY:
        case STORAGE_SETTINGS_EVENT_MULTI_FAILED:
        case STORAGE_SETTINGS_EVENT_MULTI_CLEAR_DONE:
        case STORAGE_SETTINGS_EVENT_MULTI_CLEAR_FAILED:
        case STORAGE_SETTINGS_EVENT_CONVERT_PROGRESS:
        case STORAGE_SETTINGS_EVENT_CONVERT_FAILED:
            return 1U;
        default:
            return 0U;
    }
}

static void settings_event_push(storage_settings_event_type_t type)
{
    const uint32_t head = g_settings_event_head;
    const uint32_t tail = g_settings_event_tail;
    if ((head - tail) >= SETTINGS_EVENT_QUEUE_CAPACITY) return;
    storage_settings_event_t *const event =
        (storage_settings_event_t *)&g_settings_events[head & SETTINGS_EVENT_QUEUE_MASK];
    memset(event, 0, sizeof(*event));
    event->type = type;
    g_settings_event_head = head + 1U;
    if (settings_event_is_ui_visible(type) != 0U)
        g_settings_ui_wake_pending = 1U;
}

static void settings_event_wake_ui_if_pending(void)
{
    if (g_settings_ui_wake_pending == 0U) return;
    g_settings_ui_wake_pending = 0U;
    ui_service_dirty_set();
}

static storage_settings_event_t *settings_event_last(void)
{
    if (g_settings_event_head == g_settings_event_tail) return 0;
    return (storage_settings_event_t *)&g_settings_events[
        (g_settings_event_head - 1U) & SETTINGS_EVENT_QUEUE_MASK];
}

uint8_t storage_settings_take_event(storage_settings_event_t *event)
{
    if (event == 0) return 0U;
    const uint32_t tail = g_settings_event_tail;
    if (tail == g_settings_event_head) return 0U;
    *event = g_settings_events[tail & SETTINGS_EVENT_QUEUE_MASK];
    g_settings_event_tail = tail + 1U;
    return 1U;
}

uint8_t storage_settings_get_event_snapshot(storage_settings_event_t *event,
                                            uint32_t *sequence)
{
    const uint32_t head = g_settings_event_head;
    if (sequence != 0) *sequence = head;
    if ((event == 0) || (head == g_settings_event_tail)) return 0U;
    *event = g_settings_events[(head - 1U) & SETTINGS_EVENT_QUEUE_MASK];
    /* The UI consumes the latest immutable view, not the Storage queue. */
    g_settings_event_tail = head;
    return 1U;
}

uint8_t storage_settings_project_progress(project_product_progress_t *progress)
{
    return project_product_get_progress(progress);
}

project_product_command_t storage_settings_project_busy_command(void)
{
    return project_product_ui_busy_command();
}

sd_storage_status_t storage_settings_sd_status(void)
{
    return sd_access_storage_status();
}

const char *storage_settings_sd_busy_label(void)
{
    return sd_access_gate_busy_label();
}

uint8_t storage_settings_project_replacement_active(void)
{
    return project_replacement_is_active();
}

uint8_t storage_settings_convert_active(void)
{
    return wav_convert_is_active();
}

uint8_t storage_settings_multi_clear_active(void)
{
    return multi_sample_pool_clear_is_active();
}

uint8_t storage_settings_audio_recorder_active(void)
{
    return audio_recorder_is_active();
}

uint8_t storage_settings_sample_cache_busy(void)
{
    return sample_cache_has_pending_sd_work();
}

uint8_t storage_settings_multi_load_pending(void)
{
    return multi_sample_load_has_pending();
}

uint8_t storage_settings_request_catalog(uint8_t rebuild)
{
    if (rebuild != 0U) return wav_loader_catalog_rebuild();
    return wav_loader_catalog_refresh();
}

uint8_t storage_settings_catalog_request(storage_catalog_kind_t kind, const char *path)
{
    return storage_catalog_request(kind, path);
}

uint8_t storage_settings_request_classic_load(uint16_t slot, const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    if (sample_global_pool_request_classic_load(slot, path) == 0U) return 0U;
    return storage_settings_track_classic_load(slot);
}

uint8_t storage_settings_request_ram_load(uint16_t slot, const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return sampler_ram_pool_request_load(slot, path);
}

uint8_t storage_settings_request_wavetable_load(uint16_t slot,
                                                const char *path,
                                                wavetable_source_geometry_t geometry)
{
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return wavetable_pool_request_load(slot, path, geometry);
}

uint8_t storage_settings_request_conversion(const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return wav_convert_request_start(path);
}

uint8_t storage_settings_request_preview(const char *path)
{
    return sd_preview_request_begin(path);
}

void storage_settings_request_preview_stop(void)
{
    sd_preview_request_stop();
}

uint8_t storage_settings_preview_active(void)
{
    return sd_preview_is_active();
}

const char *storage_settings_preview_path(void)
{
    return sd_preview_get_path();
}

sd_preview_error_t storage_settings_preview_last_error(void)
{
    return sd_preview_get_last_error();
}

uint8_t storage_settings_project_slot_present(uint8_t slot)
{
    return project_product_slot_present(slot);
}

uint8_t storage_settings_project_list_slots(uint8_t *out, uint8_t capacity)
{
    return project_product_list_slots(out, capacity);
}

uint16_t storage_settings_list_samples(uint32_t kind, uint16_t *out, uint16_t capacity)
{
    return project_control_list_samples(kind, out, capacity);
}

uint16_t storage_settings_list_wavetables(uint16_t *out, uint16_t capacity)
{
    return project_control_list_wavetables(out, capacity);
}

uint16_t storage_settings_list_multis(uint16_t *out, uint16_t capacity)
{
    return project_control_list_multis(out, capacity);
}

uint8_t storage_settings_find_asset(uint32_t kind, const char *path, uint16_t *out_logical)
{
    return project_control_find_asset(kind, path, out_logical);
}

uint8_t storage_settings_resolve_sample_runtime(uint16_t logical,
                                                uint16_t *out_runtime_global,
                                                uint32_t *out_kind)
{
    return project_control_resolve_sample_runtime(logical, out_runtime_global, out_kind);
}

uint8_t storage_settings_resolve_wavetable_runtime(uint16_t logical,
                                                   uint16_t *out_runtime_global)
{
    return project_control_resolve_wavetable_runtime(logical, out_runtime_global);
}

uint8_t storage_settings_resolve_multi_runtime(uint16_t logical,
                                               uint16_t *out_runtime_instrument)
{
    return project_control_resolve_multi_runtime(logical, out_runtime_instrument);
}

const sample_global_slot_t *storage_settings_get_global_slot(uint16_t global_index)
{
    return sample_global_pool_get_slot(global_index);
}

sample_classic_slot_state_t storage_settings_get_classic_state(uint16_t global_index)
{
    return sample_global_pool_get_classic_state(global_index);
}

uint16_t storage_settings_find_free_global_slot(void)
{
    return sample_global_pool_find_free_slot();
}

uint16_t storage_settings_find_free_ram_slot(void)
{
    return sampler_ram_pool_find_free_slot();
}

uint16_t storage_settings_find_free_wavetable_slot(void)
{
    return wavetable_pool_find_free_slot();
}

uint8_t storage_settings_resolve_backend(uint16_t global_index,
                                         sample_global_kind_t kind,
                                         uint16_t *out_backend_index)
{
    return sample_global_pool_resolve_backend(global_index, kind, out_backend_index);
}

uint16_t storage_settings_global_entry_capacity(void)
{
    return sample_global_pool_get_entry_capacity();
}

uint16_t storage_settings_global_entries_used(void)
{
    return sample_global_pool_get_used_entries();
}

uint32_t storage_settings_global_bytes_used(void)
{
    return sample_global_pool_get_used_bytes();
}

uint8_t storage_settings_validate_multi_budget(uint16_t backend_index,
                                               uint32_t cost_bytes)
{
    return sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                               backend_index, cost_bytes);
}

const multi_sample_instrument_t *storage_settings_get_multi_instrument(uint16_t instrument_id)
{
    return multi_sample_pool_get_instrument(instrument_id);
}

const sampler_ram_slot_t *storage_settings_get_ram_slot(uint16_t ram_slot)
{
    return sampler_ram_pool_get_slot(ram_slot);
}

const wavetable_slot_t *storage_settings_get_wavetable_slot(uint16_t wavetable_slot)
{
    return wavetable_pool_get_slot(wavetable_slot);
}

uint16_t storage_settings_multi_slots_used(void)
{
    return multi_sample_pool_get_slot_capacity_used();
}

uint8_t storage_settings_multi_state(uint16_t instrument_id)
{
    return (uint8_t)multi_sample_pool_get_state(instrument_id);
}

const char *storage_settings_multi_import_diagnostic(void)
{
    return multi_sample_import_get_last_diagnostic();
}

uint8_t storage_settings_catalog_loaded(void) { return wav_loader_catalog_loaded(); }
uint8_t storage_settings_catalog_stale(void) { return wav_loader_catalog_stale(); }
uint16_t storage_settings_catalog_count(void) { return wav_loader_catalog_count(); }
uint16_t storage_settings_catalog_child_count(uint16_t parent_id)
{
    return wav_loader_catalog_child_count(parent_id);
}
uint8_t storage_settings_catalog_view_busy(void) { return wav_loader_catalog_view_busy(); }
uint8_t storage_settings_catalog_last_sd_busy(void) { return wav_loader_catalog_last_sd_busy(); }
uint8_t storage_settings_catalog_last_io_error(void) { return wav_loader_catalog_last_io_error(); }
uint8_t storage_settings_catalog_truncated(void) { return wav_loader_catalog_truncated(); }
uint8_t storage_settings_catalog_path_truncated(void) { return wav_loader_catalog_path_truncated(); }
const wav_loader_catalog_entry_t *storage_settings_catalog_get(uint16_t index)
{
    const wav_loader_catalog_entry_t *const entry =
        wav_loader_catalog_get_cached(index);
    if (entry == 0)
        (void)wav_loader_catalog_request_index(index);
    return entry;
}
const wav_loader_catalog_entry_t *storage_settings_catalog_get_child(uint16_t parent_id,
                                                                      uint16_t child_index)
{
    return wav_loader_catalog_get_child(parent_id, child_index);
}
uint16_t storage_settings_catalog_get_child_index(uint16_t parent_id, uint16_t child_index)
{
    return wav_loader_catalog_get_child_index(parent_id, child_index);
}
uint8_t storage_settings_catalog_find_path(const char *path,
                                           uint16_t *out_index,
                                           wav_loader_catalog_entry_t *out_entry)
{
    if (wav_loader_catalog_find_path_cached(path, out_index, out_entry) != 0U)
        return 1U;
    (void)wav_loader_catalog_request_path(path);
    return 0U;
}
uint8_t storage_settings_catalog_snapshot_begin(storage_catalog_kind_t kind,
                                                const char *path,
                                                storage_catalog_snapshot_t *snapshot)
{
    return storage_catalog_snapshot_begin(kind, path, snapshot);
}
uint8_t storage_settings_catalog_snapshot_end(const storage_catalog_snapshot_t *snapshot)
{
    return storage_catalog_snapshot_end(snapshot);
}

uint8_t storage_settings_track_classic_load(uint16_t slot)
{
    g_classic_slot = slot;
    g_classic_pending = 1U;
    storage_io_owner_wakeup(STORAGE_OWNER_STREAM);
    return 1U;
}

uint8_t storage_settings_track_asset_register(uint32_t kind, uint16_t runtime)
{
    g_asset_kind = kind;
    g_asset_runtime = runtime;
    g_asset_requested = 0U;
    g_asset_pending = 1U;
    storage_io_owner_wakeup(settings_asset_owner());
    return 1U;
}

uint8_t storage_settings_track_conversion(uint16_t slot, const char *path)
{
    g_convert_slot = slot;
    settings_copy_path(g_convert_path, path);
    g_convert_percent = 0xFFU;
    g_convert_pending = 1U;
    storage_io_owner_wakeup(STORAGE_OWNER_WAV_CONVERT);
    return 1U;
}

uint8_t storage_settings_begin_multi_import(uint16_t slot,
                                            const char *path,
                                            const char *catalog_dir)
{
    if ((path == 0) || (catalog_dir == 0)) return 0U;
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    g_multi_slot = slot;
    settings_copy_path(g_multi_path, path);
    g_multi_index_path[0] = '\0';
    settings_copy_path(g_multi_catalog_dir, catalog_dir);
    g_multi_done = 0U;
    g_multi_total = 1U;
    g_multi_state = SETTINGS_MULTI_IMPORT;
    if (multi_sample_import_request_folder(path) == 0U)
    {
        g_multi_state = SETTINGS_MULTI_IDLE;
        return 0U;
    }
    return 1U;
}

uint8_t storage_settings_begin_multi_load(uint16_t slot,
                                          const char *path,
                                          const char *index_path)
{
    if ((path == 0) || (index_path == 0) || (index_path[0] == '\0')) return 0U;
    if ((project_transport_stopped_stable() == 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    g_multi_slot = slot;
    settings_copy_path(g_multi_path, path);
    settings_copy_path(g_multi_index_path, index_path);
    g_multi_done = 0U;
    g_multi_total = 1U;
    g_multi_state = SETTINGS_MULTI_LOAD;
    const multi_sample_load_result_t result =
        multi_sample_load_request_instrument(MULTI_SAMPLE_POOL_INVALID_ID,
                                             index_path, slot);
    if ((result != MULTI_SAMPLE_LOAD_OK)
        && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
    {
        g_multi_state = SETTINGS_MULTI_IDLE;
        return 0U;
    }
    return 1U;
}

uint8_t storage_settings_begin_multi_clear(void)
{
    g_clear_count = 0U;
    g_clear_index = 0U;
    g_clear_deleted = 0U;
    g_clear_failed = 0U;
    g_clear_pending = 1U;
    return 1U;
}

uint8_t storage_settings_add_multi_clear_path(const char *path)
{
    if ((path == 0) || (g_clear_count >= SETTINGS_MULTI_CLEAR_MAX)) return 0U;
    settings_copy_multi_path(g_clear_paths[g_clear_count], path);
    ++g_clear_count;
    return 1U;
}

uint8_t storage_settings_commit_multi_clear(void)
{
    if (g_clear_pending == 0U) return 0U;
    if (multi_sample_pool_request_clear_begin() == 0U)
    {
        g_clear_pending = 0U;
        g_clear_count = 0U;
        return 0U;
    }
    return 1U;
}

void storage_settings_cancel_multi(void)
{
    g_multi_state = SETTINGS_MULTI_IDLE;
    g_clear_pending = 0U;
    g_clear_count = 0U;
}

static void settings_service_asset_register(void)
{
    if (g_asset_pending == 0U) return;
    const char *path = 0;
    if (g_asset_kind == PERSIST_ASSET_MULTI)
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(g_asset_runtime);
        if (instrument != 0) path = instrument->index_path;
    }
    else
    {
        const sample_global_slot_t *const slot =
            sample_global_pool_get_slot(g_asset_runtime);
        if (slot != 0) path = slot->path;
    }
    if ((path == 0) || (path[0] == '\0')) return;
    if (g_asset_requested == 0U)
    {
        const control_asset_intent_t intent = {
            .operation = CONTROL_ASSET_REGISTER_RUNTIME,
            .kind = g_asset_kind,
            .runtime = g_asset_runtime
        };
        if (control_domain_request_asset(&intent) == 0U) return;
        g_asset_requested = 1U;
        return;
    }
    uint16_t logical = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (project_control_find_asset(g_asset_kind, path, &logical) == 0U) return;
    settings_event_push(STORAGE_SETTINGS_EVENT_ASSET_READY);
    storage_settings_event_t *const event = settings_event_last();
    if (event != 0)
    {
        event->kind = (uint8_t)(g_asset_kind & 0xFFU);
        event->logical = logical;
        event->slot = g_asset_runtime;
    }
    g_asset_pending = 0U;
}

static void settings_service_classic_load(void)
{
    if (g_classic_pending == 0U) return;
    const sample_cache_slot_readiness_t readiness =
        sample_cache_get_slot_readiness(g_classic_slot);
    if (readiness == SAMPLE_CACHE_SLOT_ERROR)
    {
        settings_event_push(STORAGE_SETTINGS_EVENT_CLASSIC_FAILED);
        storage_settings_event_t *const event = settings_event_last();
        if (event != 0) event->slot = g_classic_slot;
        g_classic_pending = 0U;
        return;
    }
    if (readiness != SAMPLE_CACHE_SLOT_PLAYABLE) return;
    if (g_classic_pending != 0U)
    {
        const sample_global_slot_t *const slot =
            sample_global_pool_get_slot(g_classic_slot);
        if (slot != 0)
        {
            g_asset_kind = PERSIST_ASSET_SAMPLE_STREAM;
            g_asset_runtime = g_classic_slot;
            g_asset_requested = 0U;
            g_asset_pending = 1U;
        }
        g_classic_pending = 0U;
    }
}

static void settings_service_catalog(void)
{
    const wav_loader_catalog_view_service_result_t result =
        wav_loader_catalog_view_last_result();
    if (result == g_catalog_result_seen) return;
    g_catalog_result_seen = result;
    if (result == WAV_LOADER_CATALOG_VIEW_PUBLISHED)
        settings_event_push(STORAGE_SETTINGS_EVENT_CATALOG_READY);
    else if (result == WAV_LOADER_CATALOG_VIEW_ERROR)
        settings_event_push(STORAGE_SETTINGS_EVENT_CATALOG_FAILED);
}

static void settings_service_multi(void)
{
    if (g_multi_state == SETTINGS_MULTI_IMPORT)
    {
        const uint16_t done = multi_sample_import_progress_done();
        const uint16_t total = multi_sample_import_progress_total();
        if ((done != g_multi_done) || (total != g_multi_total))
        {
            g_multi_done = done;
            g_multi_total = (total == 0U) ? 1U : total;
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_PROGRESS);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0) { event->done = g_multi_done; event->total = g_multi_total; }
        }
        if (multi_sample_import_is_busy() != 0U) return;
        if (multi_sample_import_get_last_result() != MULTI_SAMPLE_IMPORT_OK)
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0)
            {
                event->kind = 1U;
                event->result = (uint16_t)multi_sample_import_get_last_result();
            }
            g_multi_state = SETTINGS_MULTI_IDLE;
            return;
        }
        g_multi_state = SETTINGS_MULTI_WAIT_CATALOG;
        if (storage_catalog_request(STORAGE_CATALOG_MULTI, g_multi_catalog_dir) == 0U)
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0)
            {
                event->kind = 2U;
                event->result = (uint16_t)MULTI_SAMPLE_LOAD_INDEX_FAIL;
            }
            g_multi_state = SETTINGS_MULTI_IDLE;
        }
        return;
    }

    if (g_multi_state == SETTINGS_MULTI_WAIT_CATALOG)
    {
        storage_catalog_snapshot_t snapshot;
        if (storage_catalog_snapshot_begin(STORAGE_CATALOG_MULTI,
                                            g_multi_catalog_dir, &snapshot) == 0U)
            return;
        for (uint16_t i = 0U; i < snapshot.count; ++i)
        {
            if (strcmp(snapshot.entries[i].path, g_multi_path) == 0)
            {
                (void)snprintf(g_multi_index_path, sizeof(g_multi_index_path), "%s",
                               snapshot.entries[i].index_path);
                               break;
            }
        }
        if (storage_catalog_snapshot_end(&snapshot) == 0U)
            return;
        if (g_multi_index_path[0] == '\0')
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0)
            {
                event->kind = 2U;
                event->result = (uint16_t)MULTI_SAMPLE_LOAD_INDEX_FAIL;
            }
            g_multi_state = SETTINGS_MULTI_IDLE;
            return;
        }
        g_multi_state = SETTINGS_MULTI_LOAD;
        const multi_sample_load_result_t result =
            multi_sample_load_request_instrument(MULTI_SAMPLE_POOL_INVALID_ID,
                                                 g_multi_index_path, g_multi_slot);
        if ((result != MULTI_SAMPLE_LOAD_OK)
            && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0)
            {
                event->kind = 2U;
                event->result = (uint16_t)result;
            }
            g_multi_state = SETTINGS_MULTI_IDLE;
            return;
        }
    }

    if (g_multi_state == SETTINGS_MULTI_LOAD)
    {
        const multi_sample_instrument_state_t state =
            multi_sample_pool_get_state(g_multi_slot);
        multi_sample_load_diag_t diag;
        multi_sample_get_load_diag(&diag);
        if (state == MULTI_SAMPLE_INSTRUMENT_ERROR
            || diag.state == MULTI_SAMPLE_INSTRUMENT_ERROR)
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0)
            {
                event->kind = 2U;
                event->result = (uint16_t)diag.last_error;
            }
            g_multi_state = SETTINGS_MULTI_IDLE;
            return;
        }
        if (state == MULTI_SAMPLE_INSTRUMENT_LOADING)
        {
            const uint16_t done = diag.pages_ready;
            const uint16_t total = (diag.pages_requested == 0U)
                ? 1U : diag.pages_requested;
            if ((done != g_multi_done) || (total != g_multi_total))
            {
                g_multi_done = done;
                g_multi_total = total;
                settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_PROGRESS);
                storage_settings_event_t *const event = settings_event_last();
                if (event != 0) { event->done = done; event->total = total; }
            }
            return;
        }
        if (state == MULTI_SAMPLE_INSTRUMENT_READY)
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_READY);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0) event->slot = g_multi_slot;
            g_multi_state = SETTINGS_MULTI_IDLE;
        }
    }
}

static void settings_service_clear(void)
{
    if ((g_clear_pending == 0U)
        || (multi_sample_pool_clear_is_active() == 0U)) return;
    uint8_t result = 0U;
    if (multi_sample_import_take_delete_result(&result) != 0U)
    {
        if (result == 1U) ++g_clear_deleted;
        else if (result != 2U) g_clear_failed = 1U;
    }
    if (g_clear_failed != 0U)
    {
        settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_CLEAR_FAILED);
        g_multi_state = SETTINGS_MULTI_IDLE;
        g_clear_pending = 0U;
        (void)multi_sample_pool_request_clear_end();
        return;
    }
    if (g_clear_index < g_clear_count)
    {
        const uint16_t index = g_clear_index++;
        if (multi_sample_import_request_delete_index(g_clear_paths[index]) == 0U)
            g_clear_index = index;
        return;
    }
    settings_event_push(STORAGE_SETTINGS_EVENT_MULTI_CLEAR_DONE);
    storage_settings_event_t *const event = settings_event_last();
    if (event != 0) event->deleted = g_clear_deleted;
    g_clear_pending = 0U;
    (void)multi_sample_pool_request_clear_end();
}

void storage_settings_service_owner(storage_io_owner_t owner)
{
    if (owner == STORAGE_OWNER_CATALOG)
    {
        settings_service_catalog();
        if (g_multi_state == SETTINGS_MULTI_WAIT_CATALOG)
            settings_service_multi();
        settings_event_wake_ui_if_pending();
        return;
    }

    if (owner == STORAGE_OWNER_STREAM)
    {
        settings_service_classic_load();
        if ((g_asset_pending != 0U)
            && (settings_asset_owner() == STORAGE_OWNER_STREAM))
            settings_service_asset_register();
        settings_event_wake_ui_if_pending();
        return;
    }

    if ((owner == STORAGE_OWNER_SAMPLE_RAM)
        || (owner == STORAGE_OWNER_WAVETABLE))
    {
        if ((g_asset_pending != 0U) && (settings_asset_owner() == owner))
            settings_service_asset_register();
        settings_event_wake_ui_if_pending();
        return;
    }

    if (owner == STORAGE_OWNER_MULTI)
    {
        if ((g_asset_pending != 0U)
            && (settings_asset_owner() == STORAGE_OWNER_MULTI))
            settings_service_asset_register();
        if ((g_multi_state == SETTINGS_MULTI_IMPORT)
            || (g_multi_state == SETTINGS_MULTI_LOAD))
            settings_service_multi();
        settings_service_clear();
        settings_event_wake_ui_if_pending();
        return;
    }

    if (owner != STORAGE_OWNER_WAV_CONVERT)
    {
        settings_event_wake_ui_if_pending();
        return;
    }

    if (g_convert_pending != 0U)
    {
        if (wav_convert_is_active() != 0U)
        {
            const uint8_t percent = wav_convert_get_progress_percent();
            if (percent != g_convert_percent)
            {
                g_convert_percent = percent;
                settings_event_push(STORAGE_SETTINGS_EVENT_CONVERT_PROGRESS);
                storage_settings_event_t *const event = settings_event_last();
                if (event != 0) event->percent = percent;
            }
        }
        else if (wav_convert_get_state() == WAV_CONVERT_STATE_FAILED)
        {
            settings_event_push(STORAGE_SETTINGS_EVENT_CONVERT_FAILED);
            storage_settings_event_t *const event = settings_event_last();
            if (event != 0) event->result = (uint16_t)wav_convert_get_last_error();
            g_convert_pending = 0U;
            wav_convert_clear_finished();
        }
        else if (wav_convert_get_state() == WAV_CONVERT_STATE_DONE)
        {
            wav_loader_catalog_mark_stale();
            if (sample_global_pool_request_classic_load(g_convert_slot,
                                                        g_convert_path) == 0U)
            {
                settings_event_push(STORAGE_SETTINGS_EVENT_CONVERT_FAILED);
                g_convert_pending = 0U;
                wav_convert_clear_finished();
            }
            else
            {
                (void)storage_settings_track_classic_load(g_convert_slot);
                g_convert_pending = 0U;
                wav_convert_clear_finished();
            }
        }
    }
    settings_event_wake_ui_if_pending();
}
