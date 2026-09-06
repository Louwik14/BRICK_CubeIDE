#pragma once

#include <stdint.h>

#include "Storage/wav_loader.h"
#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/persistent_control_model.h"
#include "Storage/sd_preview.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_convert.h"
#include "Storage/storage_catalog.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/multi_sample_import.h"
#include "Sampler/multi_sample_loader.h"
#include "Storage/storage_io_wakeup.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t storage_settings_request_catalog(uint8_t rebuild);
uint8_t storage_settings_catalog_request(storage_catalog_kind_t kind, const char *path);
uint8_t storage_settings_request_classic_load(uint16_t slot, const char *path);
uint8_t storage_settings_request_ram_load(uint16_t slot, const char *path);
uint8_t storage_settings_request_wavetable_load(uint16_t slot,
                                                const char *path,
                                                wavetable_source_geometry_t geometry);
uint8_t storage_settings_request_conversion(const char *path);
uint8_t storage_settings_request_classic_with_conversion(uint16_t slot,
                                                         const char *path);
uint8_t storage_settings_request_preview(const char *path);
void storage_settings_request_preview_stop(void);
uint8_t storage_settings_preview_active(void);
const char *storage_settings_preview_path(void);
sd_preview_error_t storage_settings_preview_last_error(void);
uint8_t storage_settings_begin_multi_import(uint16_t slot,
                                            const char *path,
                                            const char *catalog_dir);
uint8_t storage_settings_begin_multi_load(uint16_t slot,
                                          const char *path,
                                          const char *index_path);
uint8_t storage_settings_begin_multi_replacement(
    uint16_t old_logical, uint16_t slot, const char *source_path,
    const char *index_path, uint8_t import_required);
uint8_t storage_settings_begin_multi_clear(void);
uint8_t storage_settings_add_multi_clear_path(const char *path);
uint8_t storage_settings_commit_multi_clear(void);
void storage_settings_cancel_multi(void);
void storage_settings_cancel_multi_clear(void);
/* Read-only settings projection.  These functions copy or return immutable
 * owner snapshots; UI code never reaches the underlying Storage products. */
uint8_t storage_settings_project_progress(project_product_progress_t *progress);
project_product_command_t storage_settings_project_busy_command(void);
sd_storage_status_t storage_settings_sd_status(void);
const char *storage_settings_sd_busy_label(void);
uint8_t storage_settings_project_replacement_active(void);
uint8_t storage_settings_convert_active(void);
uint8_t storage_settings_multi_clear_active(void);
uint8_t storage_settings_audio_recorder_active(void);
uint8_t storage_settings_sample_cache_busy(void);
uint8_t storage_settings_multi_load_pending(void);

uint8_t storage_settings_project_list_slots(uint8_t *out, uint8_t capacity);
uint8_t storage_settings_project_slot_present(uint8_t slot);
uint16_t storage_settings_list_samples(uint32_t kind, uint16_t *out, uint16_t capacity);
uint16_t storage_settings_list_wavetables(uint16_t *out, uint16_t capacity);
uint16_t storage_settings_list_multis(uint16_t *out, uint16_t capacity);
uint8_t storage_settings_find_asset(uint32_t kind, const char *path, uint16_t *out_logical);
uint8_t storage_settings_resolve_sample_runtime(uint16_t logical,
                                                uint16_t *out_runtime_global,
                                                uint32_t *out_kind);
uint8_t storage_settings_resolve_wavetable_runtime(uint16_t logical,
                                                   uint16_t *out_runtime_global);
uint8_t storage_settings_resolve_multi_runtime(uint16_t logical,
                                               uint16_t *out_runtime_instrument);
const sample_global_slot_t *storage_settings_get_global_slot(uint16_t global_index);
sample_classic_slot_state_t storage_settings_get_classic_state(uint16_t global_index);
uint16_t storage_settings_find_free_global_slot(void);
uint16_t storage_settings_find_free_ram_slot(void);
uint16_t storage_settings_find_free_wavetable_slot(void);
uint8_t storage_settings_resolve_backend(uint16_t global_index,
                                         sample_global_kind_t kind,
                                         uint16_t *out_backend_index);
uint16_t storage_settings_global_entry_capacity(void);
uint16_t storage_settings_global_entries_used(void);
uint32_t storage_settings_global_bytes_used(void);
uint8_t storage_settings_validate_multi_budget(uint16_t backend_index,
                                               uint32_t cost_bytes);
const multi_sample_instrument_t *storage_settings_get_multi_instrument(uint16_t instrument_id);
const sampler_ram_slot_t *storage_settings_get_ram_slot(uint16_t ram_slot);
const wavetable_slot_t *storage_settings_get_wavetable_slot(uint16_t wavetable_slot);
uint16_t storage_settings_multi_slots_used(void);
uint8_t storage_settings_multi_state(uint16_t instrument_id);
const char *storage_settings_multi_import_diagnostic(void);
uint8_t storage_settings_catalog_loaded(void);
uint8_t storage_settings_catalog_stale(void);
uint16_t storage_settings_catalog_count(void);
uint16_t storage_settings_catalog_child_count(uint16_t parent_id);
uint8_t storage_settings_catalog_view_busy(void);
uint8_t storage_settings_catalog_last_sd_busy(void);
uint8_t storage_settings_catalog_last_io_error(void);
uint8_t storage_settings_catalog_truncated(void);
uint8_t storage_settings_catalog_path_truncated(void);
const wav_loader_catalog_entry_t *storage_settings_catalog_get(uint16_t index);
const wav_loader_catalog_entry_t *storage_settings_catalog_get_child(uint16_t parent_id,
                                                                      uint16_t child_index);
uint16_t storage_settings_catalog_get_child_index(uint16_t parent_id, uint16_t child_index);
uint8_t storage_settings_catalog_find_path(const char *path,
                                           uint16_t *out_index,
                                           wav_loader_catalog_entry_t *out_entry);
uint8_t storage_settings_catalog_snapshot_begin(storage_catalog_kind_t kind,
                                                const char *path,
                                                storage_catalog_snapshot_t *snapshot);
uint8_t storage_settings_catalog_snapshot_end(const storage_catalog_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
