#include "Storage/settings_storage_service.h"

#include <string.h>

#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_cache.h"
#include "Storage/audio_recorder.h"
#include "Storage/project_control.h"
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_convert.h"
#include "Seq/seq_runtime.h"

uint8_t storage_settings_project_slot_present(uint8_t slot)
{
    return project_product_slot_present(slot);
}

uint8_t storage_settings_project_list_slots(uint8_t *out, uint8_t capacity)
{
    return project_product_list_slots(out, capacity);
}

uint8_t storage_settings_request_catalog(uint8_t rebuild)
{
    return (rebuild != 0U) ? wav_loader_catalog_rebuild()
                           : wav_loader_catalog_refresh();
}

uint8_t storage_settings_catalog_request(storage_catalog_kind_t kind, const char *path)
{
    return storage_catalog_request(kind, path);
}

uint8_t storage_settings_request_classic_load(uint16_t slot, const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return sample_global_pool_request_classic_load(slot, path);
}

uint8_t storage_settings_request_ram_load(uint16_t slot, const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return sampler_ram_pool_request_load(slot, path);
}

uint8_t storage_settings_request_wavetable_load(uint16_t slot,
                                                const char *path,
                                                wavetable_source_geometry_t geometry)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return wavetable_pool_request_load(slot, path, geometry);
}

uint8_t storage_settings_request_conversion(const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return wav_convert_request_start(path);
}

uint8_t storage_settings_request_classic_with_conversion(uint16_t slot,
                                                         const char *path)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return wav_convert_request_classic_cycle(slot, path);
}

uint8_t storage_settings_request_preview(const char *path)
{
    return sd_preview_request_begin(path);
}

void storage_settings_request_preview_stop(void) { sd_preview_request_stop(); }
uint8_t storage_settings_preview_active(void) { return sd_preview_is_active(); }
const char *storage_settings_preview_path(void) { return sd_preview_get_path(); }
sd_preview_error_t storage_settings_preview_last_error(void)
{
    return sd_preview_get_last_error();
}

uint8_t storage_settings_begin_multi_import(uint16_t slot,
                                            const char *path,
                                            const char *catalog_dir)
{
    (void)catalog_dir;
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return multi_sample_import_request_folder_for_load(path, slot);
}

uint8_t storage_settings_begin_multi_load(uint16_t slot,
                                          const char *path,
                                          const char *index_path)
{
    (void)path;
    if ((index_path == NULL) || (index_path[0] == '\0')
        || (project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    const multi_sample_load_result_t result =
        multi_sample_load_request_instrument(MULTI_SAMPLE_POOL_INVALID_ID,
                                             index_path, slot);
    return (result == MULTI_SAMPLE_LOAD_OK)
        || (result == MULTI_SAMPLE_LOAD_ALREADY_READY);
}

uint8_t storage_settings_begin_multi_replacement(
    uint16_t old_logical, uint16_t slot, const char *source_path,
    const char *index_path, uint8_t import_required)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sd_access_storage_status() == SD_STORAGE_STATUS_NO_MEDIA)) return 0U;
    return (multi_sample_load_request_replacement(
                old_logical, slot, source_path, index_path, import_required)
            == MULTI_SAMPLE_LOAD_OK) ? 1U : 0U;
}

uint8_t storage_settings_begin_multi_clear(void)
{
    if ((project_transport_stopped_stable() == 0U)
        || (seq_runtime_is_start_pending() != 0U)) return 0U;
    return multi_sample_import_clear_batch_begin();
}

uint8_t storage_settings_add_multi_clear_path(const char *path)
{
    return multi_sample_import_clear_batch_add(path);
}

uint8_t storage_settings_commit_multi_clear(void)
{
    return multi_sample_import_clear_batch_commit();
}

void storage_settings_cancel_multi(void)
{
    multi_sample_import_clear_batch_cancel();
    multi_sample_cancel_all_loads();
}

uint8_t storage_settings_project_replacement_active(void)
{
    return project_replacement_is_active();
}
uint8_t storage_settings_convert_active(void) { return wav_convert_is_active(); }
uint8_t storage_settings_multi_clear_active(void)
{
    return multi_sample_import_clear_batch_active();
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
const char *storage_settings_sd_busy_label(void) { return sd_access_gate_busy_label(); }

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

const sample_global_slot_t *storage_settings_get_global_slot(uint16_t index)
{
    return sample_global_pool_get_slot(index);
}
sample_classic_slot_state_t storage_settings_get_classic_state(uint16_t index)
{
    return sample_global_pool_get_classic_state(index);
}
uint16_t storage_settings_find_free_global_slot(void) { return sample_global_pool_find_free_slot(); }
uint16_t storage_settings_find_free_ram_slot(void) { return sampler_ram_pool_find_free_slot(); }
uint16_t storage_settings_find_free_wavetable_slot(void) { return wavetable_pool_find_free_slot(); }
uint8_t storage_settings_resolve_backend(uint16_t global_index,
                                         sample_global_kind_t kind,
                                         uint16_t *out_backend_index)
{
    return sample_global_pool_resolve_backend(global_index, kind, out_backend_index);
}
uint16_t storage_settings_global_entry_capacity(void) { return sample_global_pool_get_entry_capacity(); }
uint16_t storage_settings_global_entries_used(void) { return sample_global_pool_get_used_entries(); }
uint32_t storage_settings_global_bytes_used(void) { return sample_global_pool_get_used_bytes(); }
uint8_t storage_settings_validate_multi_budget(uint16_t backend_index, uint32_t cost_bytes)
{
    return sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                               backend_index, cost_bytes);
}
const multi_sample_instrument_t *storage_settings_get_multi_instrument(uint16_t id)
{
    return multi_sample_pool_get_instrument(id);
}
const sampler_ram_slot_t *storage_settings_get_ram_slot(uint16_t slot)
{
    return sampler_ram_pool_get_slot(slot);
}
const wavetable_slot_t *storage_settings_get_wavetable_slot(uint16_t slot)
{
    return wavetable_pool_get_slot(slot);
}
uint16_t storage_settings_multi_slots_used(void) { return multi_sample_pool_get_slot_capacity_used(); }
uint8_t storage_settings_multi_state(uint16_t id)
{
    return (uint8_t)multi_sample_pool_get_state(id);
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
    const wav_loader_catalog_entry_t *entry = wav_loader_catalog_get_cached(index);
    if (entry == NULL) (void)wav_loader_catalog_request_index(index);
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
    if (wav_loader_catalog_find_path_cached(path, out_index, out_entry) != 0U) return 1U;
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
