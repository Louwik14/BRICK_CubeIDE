#include "Storage/project_v1.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/boot_context_flash.h"
#include "Storage/looper_storage.h"
#include "Storage/multi_record_writer.h"
#include "Storage/pattern_sd_bank.h"
#include "Storage/project_sd_bank.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Audio/drum_synth.h"
#include "Audio/fx_master_macro.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_sound_state.h"
#include "Core/track_tone_sound_state.h"
#include "mixer.h"
#include "Param/param_macro.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/multi_sample_index.h"
#include "Sampler/multi_sample_loader.h"
#include "Seq/seq_runtime.h"
#include "stm32h7xx_hal.h"

UI_SDRAM static ProjectSaveV1 g_project_work;
UI_SDRAM static project_v1_macro_state_t g_project_macro_state;
UI_SDRAM static project_v1_multi_track_t g_project_multi_assign[SEQ_TRACK_COUNT];
UI_SDRAM static project_v1_multi_restore_diag_t g_project_multi_restore_diag;
static uint8_t g_project_active_slot_valid;
static uint8_t g_project_active_slot;
static uint32_t g_project_save_counter;
static project_v1_error_t g_project_last_error;
static project_sd_bank_error_t g_project_last_sd_error;
static uint8_t g_project_autoload_progress_active;
static uint16_t g_project_autoload_progress_units[PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT];
static uint8_t g_project_autoload_progress_units_known[PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT];

static uint8_t project_v1_macro_scene_lock_is_valid(uint8_t scene, uint8_t lock)
{
    return (uint8_t)((scene < PROJECT_V1_MACRO_SCENE_COUNT)
            && (lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT));
}

static void project_v1_macro_clear_lock(project_v1_macro_lock_t *lock)
{
    if (lock == 0)
    {
        return;
    }

    lock->track = PROJECT_V1_MACRO_LOCK_TRACK_NONE;
    lock->param = PROJECT_V1_MACRO_LOCK_PARAM_NONE;
    lock->scene_value = 0.0f;
}

static void project_v1_macro_sanitize_state(project_v1_macro_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    if ((uint8_t)state->hall_switch_mode >= (uint8_t)PROJECT_V1_MACRO_HALL_SWITCH_COUNT)
    {
        state->hall_switch_mode = PROJECT_V1_MACRO_HALL_SWITCH_SCENE;
    }

    for (uint8_t macro = 0U; macro < PROJECT_V1_MACRO_POT_COUNT; ++macro)
    {
        if (state->macro_scene[macro] >= PROJECT_V1_MACRO_SCENE_COUNT)
        {
            state->macro_scene[macro] = macro;
        }
    }
}

static void project_v1_set_error(project_v1_error_t err)
{
    g_project_last_error = err;
}

static uint8_t project_v1_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U))
    {
        return 0U;
    }

    dst[0] = '\0';
    if (src == 0)
    {
        return 1U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static uint8_t project_v1_text_equal(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < PROJECT_V1_MULTI_PATH_MAX; ++i)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
        if (a[i] == '\0')
        {
            return 1U;
        }
    }

    return 1U;
}

static uint8_t project_v1_sample_autoload_path_equal(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < PROJECT_V1_SAMPLE_AUTOLOAD_PATH_MAX; ++i)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
        if (a[i] == '\0')
        {
            return 1U;
        }
    }

    return 1U;
}

static void project_v1_sample_autoload_clear(ProjectSaveV1 *project)
{
    if (project == 0)
    {
        return;
    }

    memset(&project->sample_autoload, 0, sizeof(project->sample_autoload));
    project->sample_autoload.version = PROJECT_V1_SAMPLE_AUTOLOAD_VERSION;
}

static uint8_t project_v1_sample_autoload_add(ProjectSaveV1 *project,
                                              project_v1_sample_autoload_kind_t kind,
                                              uint16_t slot_index,
                                              uint16_t global_index,
                                              const char *path)
{
    if ((project == 0)
        || (project->sample_autoload.count >= PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
        || (path == 0)
        || (path[0] == '\0'))
    {
        return 0U;
    }

    project_v1_sample_autoload_slot_t *const slot =
        &project->sample_autoload.slots[project->sample_autoload.count];
    memset(slot, 0, sizeof(*slot));
    slot->slot_index = slot_index;
    slot->global_index = global_index;
    slot->kind = (uint8_t)kind;
    slot->flags = PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED;
    if (path != 0)
    {
        if (project_v1_copy_text(slot->path, sizeof(slot->path), path) == 0U)
        {
            memset(slot, 0, sizeof(*slot));
            return 0U;
        }
    }

    project->sample_autoload.count++;
    return 1U;
}

static void project_v1_capture_sample_autoload(ProjectSaveV1 *project)
{
    project_v1_sample_autoload_clear(project);
    if (project == 0)
    {
        return;
    }

    for (uint16_t slot = 0U; slot < SAMPLE_POOL_SIZE; ++slot)
    {
        const sample_desc_t *const desc = sample_pool_get(slot);
        if ((desc != 0) && (desc->path[0] != '\0'))
        {
            uint16_t global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            (void)sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_STREAM,
                                                     slot,
                                                     &global_index);
            (void)project_v1_sample_autoload_add(project,
                                                 PROJECT_V1_SAMPLE_AUTOLOAD_KIND_STREAM,
                                                 slot,
                                                 global_index,
                                                 desc->path);
        }
    }

    for (uint16_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(slot);
        if ((instrument != 0) && (instrument->index_path[0] != '\0'))
        {
            uint16_t global_index = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            (void)sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                                     slot,
                                                     &global_index);
            (void)project_v1_sample_autoload_add(project,
                                                 PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI,
                                                 slot,
                                                 global_index,
                                                 instrument->index_path);
        }
    }

    for (uint16_t slot = 0U; slot < SAMPLER_RAM_POOL_MAX_SLOTS; ++slot)
    {
        const sampler_ram_slot_t *const ram = sampler_ram_pool_get_slot(slot);
        if ((ram != 0)
            && (ram->path[0] != '\0')
            && ((ram->state == SAMPLER_RAM_SLOT_READY)
                || (ram->state == SAMPLER_RAM_SLOT_ERROR)))
        {
            uint16_t global_index = ram->global_slot;
            if (global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            {
                (void)sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_RAM,
                                                         slot,
                                                         &global_index);
            }
            (void)project_v1_sample_autoload_add(project,
                                                 PROJECT_V1_SAMPLE_AUTOLOAD_KIND_RAM,
                                                 slot,
                                                 global_index,
                                                 ram->path);
        }
    }
}

static uint32_t project_v1_stream_product_cost_bytes(uint32_t frames)
{
    if (frames == 0U)
    {
        return 0U;
    }

    const uint32_t prep_frames = (frames < SAMPLE_PREP_MIN_READY_FRAMES)
        ? frames
        : SAMPLE_PREP_MIN_READY_FRAMES;
    const uint32_t pages = (prep_frames + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES;
    return pages * SAMPLE_PAGE_BYTES;
}

static void project_v1_restore_stream_global_slots(const ProjectSaveV1 *project)
{
    if ((project == 0)
        || (project->sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return;
    }

    const uint16_t count =
        (project->sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? project->sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;
    for (uint16_t backend = 0U; backend < SAMPLE_POOL_SIZE; ++backend)
    {
        sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_STREAM, backend);
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &project->sample_autoload.slots[i];
        if ((slot->kind != (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_STREAM)
            || ((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) == 0U)
            || (slot->slot_index >= SAMPLE_POOL_SIZE)
            || (slot->global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
        {
            continue;
        }

        const sample_desc_t *const desc = sample_pool_get(slot->slot_index);
        if ((desc == 0) || (desc->path[0] == '\0'))
        {
            continue;
        }
        const uint32_t cost_bytes =
            project_v1_stream_product_cost_bytes(desc->length_frames);
        (void)sample_global_pool_register_stream_at((uint16_t)slot->global_index,
                                                    slot->slot_index,
                                                    desc->path,
                                                    cost_bytes);
    }
}

static void project_v1_clear_autoload_progress_units(void)
{
    memset(g_project_autoload_progress_units, 0, sizeof(g_project_autoload_progress_units));
    memset(g_project_autoload_progress_units_known,
           0,
           sizeof(g_project_autoload_progress_units_known));
}

static void project_v1_prepare_autoload_progress_units(const ProjectSaveV1 *project)
{
    project_v1_clear_autoload_progress_units();
    if ((project == 0)
        || (project->sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return;
    }

    const uint16_t count =
        (project->sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? project->sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &project->sample_autoload.slots[i];
        if (((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) == 0U)
            || (slot->path[0] == '\0'))
        {
            continue;
        }

        if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_STREAM)
        {
            g_project_autoload_progress_units[i] = 1U;
            g_project_autoload_progress_units_known[i] = 1U;
        }
        else if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_RAM)
        {
            g_project_autoload_progress_units[i] = 1U;
            g_project_autoload_progress_units_known[i] = 1U;
        }
        else if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI)
        {
            uint16_t sample_count = 0U;
            uint16_t zone_count = 0U;
            if ((multi_sample_index_peek_counts(slot->path, &sample_count, &zone_count)
                 == MULTI_SAMPLE_INDEX_OK)
                && (sample_count != 0U))
            {
                g_project_autoload_progress_units[i] = sample_count;
                g_project_autoload_progress_units_known[i] = 1U;
            }
        }
    }
}

static uint16_t project_v1_autoload_slot_units(uint16_t index,
                                               const project_v1_sample_autoload_slot_t *slot)
{
    if ((index < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
        && (g_project_autoload_progress_units_known[index] != 0U)
        && (g_project_autoload_progress_units[index] != 0U))
    {
        return g_project_autoload_progress_units[index];
    }

    if ((slot != 0)
        && (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI)
        && (slot->slot_index < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(slot->slot_index);
        if ((instrument != 0) && (instrument->sample_count != 0U))
        {
            return instrument->sample_count;
        }
    }

    return 1U;
}

static uint16_t project_v1_sample_autoload_find_multi_slot(const ProjectSaveV1 *project,
                                                           const char *path)
{
    if ((project == 0) || (path == 0) || (path[0] == '\0')
        || (project->sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return MULTI_SAMPLE_POOL_INVALID_ID;
    }

    const uint16_t count =
        (project->sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? project->sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &project->sample_autoload.slots[i];
        if ((slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI)
            && ((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) != 0U)
            && (slot->slot_index < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            && (project_v1_sample_autoload_path_equal(slot->path, path) != 0U))
        {
            return slot->slot_index;
        }
    }

    return MULTI_SAMPLE_POOL_INVALID_ID;
}

static void project_v1_multi_restore_autoload_slots(const ProjectSaveV1 *project)
{
    for (uint16_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        if (multi_sample_pool_get_state(slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            brick6_sampler_runtime_stop_multi_instrument(slot);
            (void)multi_sample_pool_clear_instrument(slot);
        }
    }

    if ((project == 0)
        || (project->sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return;
    }

    const uint16_t count =
        (project->sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? project->sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &project->sample_autoload.slots[i];
        if ((slot->kind != (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI)
            || ((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) == 0U)
            || (slot->slot_index >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            || (slot->path[0] == '\0'))
        {
            continue;
        }

        const multi_sample_load_result_t result =
            multi_sample_load_instrument(slot->path, slot->slot_index);
        if ((result == MULTI_SAMPLE_LOAD_OK)
            || (result == MULTI_SAMPLE_LOAD_ALREADY_READY)
            || (result == MULTI_SAMPLE_LOAD_SD_BUSY))
        {
            if ((result != MULTI_SAMPLE_LOAD_ALREADY_READY)
                && (slot->global_index < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
            {
                (void)sample_global_pool_register_multi_loading_at((uint16_t)slot->global_index,
                                                                   slot->slot_index,
                                                                   slot->path);
            }
            g_project_multi_restore_diag.restore_load_requested = 1U;
        }
        else
        {
            g_project_multi_restore_diag.restore_load_error = 1U;
        }
    }
}

static void project_v1_ram_restore_autoload_slots(const ProjectSaveV1 *project)
{
    sampler_ram_pool_reset();
    if ((project == 0)
        || (project->sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return;
    }

    const uint16_t count =
        (project->sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? project->sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &project->sample_autoload.slots[i];
        if ((slot->kind != (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_RAM)
            || ((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) == 0U)
            || (slot->slot_index >= SAMPLER_RAM_POOL_MAX_SLOTS)
            || (slot->global_index >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            || (slot->path[0] == '\0'))
        {
            continue;
        }

        (void)sampler_ram_pool_load_wav_at(slot->slot_index,
                                           (uint16_t)slot->global_index,
                                           slot->path);
    }
}

static void project_v1_multi_clear_assignments(void)
{
    memset(&g_project_multi_assign, 0, sizeof(g_project_multi_assign));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_project_multi_assign[track].gain = 1.0f;
        brick6_sampler_runtime_set_multi_gain(track, 1.0f);
        brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
    }
}

static void project_v1_reset_blank_transient_runtime(void)
{
    sd_preview_stop();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        brick6_sampler_runtime_reset_track(track);
    }
    brick6_sampler_runtime_service();
    brick6_looper_runtime_init();
    brick6_braids_runtime_init();
    drum_synth_all_notes_off_all();
    mixer_reset_runtime_state();
    fx_master_macro_init(48000.0f);
    track_sound_state_init();
    track_tone_sound_state_init();
}

static uint16_t project_v1_multi_find_restored_instrument(const ProjectSaveV1 *project,
                                                          uint8_t track)
{
    if ((project == 0) || (track >= SEQ_TRACK_COUNT)
        || (project->multi[track].path[0] == '\0'))
    {
        return MULTI_SAMPLE_POOL_INVALID_ID;
    }

    const uint16_t autoload_slot =
        project_v1_sample_autoload_find_multi_slot(project, project->multi[track].path);
    if (autoload_slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
    {
        return autoload_slot;
    }

    for (uint16_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(slot);
        if ((instrument != 0) && (instrument->index_path[0] != '\0')
            && (project_v1_sample_autoload_path_equal(instrument->index_path,
                                                      project->multi[track].path) != 0U))
        {
            return slot;
        }
    }

    for (uint8_t prev = 0U; prev < track; ++prev)
    {
        if ((project->multi[prev].path[0] != '\0')
            && (project_v1_text_equal(project->multi[prev].path,
                                      project->multi[track].path) != 0U))
        {
            uint16_t instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            if (brick6_sampler_runtime_get_multi_instrument(prev, &instrument_id) != 0U)
            {
                return instrument_id;
            }
        }
    }

    return track;
}

static void project_v1_multi_restore_from_snapshot(const ProjectSaveV1 *project)
{
    memset(&g_project_multi_restore_diag, 0, sizeof(g_project_multi_restore_diag));
    if (project == 0)
    {
        return;
    }
    project_v1_multi_restore_autoload_slots(project);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const project_v1_multi_track_t *const src = &project->multi[track];
        project_v1_multi_track_t *const dst = &g_project_multi_assign[track];

        dst->gain = src->gain;
        if (dst->gain < 0.0f)
        {
            dst->gain = 0.0f;
        }
        else if (dst->gain > 4.0f)
        {
            dst->gain = 4.0f;
        }
        brick6_sampler_runtime_set_multi_gain(track, dst->gain);

        if (src->path[0] == '\0')
        {
            dst->path[0] = '\0';
            brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
            continue;
        }

        if (project_v1_copy_text(dst->path, sizeof(dst->path), src->path) == 0U)
        {
            dst->path[0] = '\0';
            brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
            g_project_multi_restore_diag.restore_missing_path = 1U;
            continue;
        }

        const uint16_t instrument_id = project_v1_multi_find_restored_instrument(project, track);
        if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
        {
            brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
            g_project_multi_restore_diag.restore_load_error = 1U;
            continue;
        }

        brick6_sampler_runtime_set_multi_instrument(track, instrument_id);
        (void)project_v1_copy_text(g_project_multi_restore_diag.restored_multi_path,
                                   sizeof(g_project_multi_restore_diag.restored_multi_path),
                                   dst->path);
        g_project_multi_restore_diag.restored_track = track;

        uint8_t shared_with_previous = 0U;
        for (uint8_t prev = 0U; prev < track; ++prev)
        {
            if ((project->multi[prev].path[0] != '\0')
                && (project_v1_text_equal(project->multi[prev].path, dst->path) != 0U))
            {
                shared_with_previous = 1U;
                break;
            }
        }

        if ((shared_with_previous == 0U)
            && (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_EMPTY))
        {
            const multi_sample_load_result_t result =
                multi_sample_load_instrument(dst->path, instrument_id);
            if ((result == MULTI_SAMPLE_LOAD_OK)
                || (result == MULTI_SAMPLE_LOAD_ALREADY_READY))
            {
                g_project_multi_restore_diag.restore_load_requested = 1U;
            }
            else
            {
                brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
                g_project_multi_restore_diag.restore_load_error = 1U;
            }
        }
    }
}

static void project_v1_set_sd_operation_error(project_v1_error_t err)
{
    g_project_last_sd_error = project_sd_bank_get_last_error();
    project_v1_set_error(err);
}

static uint8_t project_v1_record_active_guard(void)
{
    if ((multi_record_writer_any_active() == 0U)
            && (looper_storage_raw_export_is_active() == 0U))
    {
        return 0U;
    }

    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_RECORD_ACTIVE);
    return 1U;
}

static void project_boot_ctx_commit_current_state_if_valid(void)
{
    if (g_project_active_slot_valid == 0U)
    {
        return;
    }

    (void)boot_context_flash_commit(g_project_active_slot);
}

void project_v1_macro_init(void)
{
    g_project_macro_state.hall_switch_mode = PROJECT_V1_MACRO_HALL_SWITCH_SCENE;
    for (uint8_t macro = 0U; macro < PROJECT_V1_MACRO_POT_COUNT; ++macro)
    {
        g_project_macro_state.macro_scene[macro] = macro;
    }

    for (uint8_t scene = 0U; scene < PROJECT_V1_MACRO_SCENE_COUNT; ++scene)
    {
        for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
        {
            project_v1_macro_clear_lock(&g_project_macro_state.scenes[scene].locks[lock]);
        }
    }
}

project_v1_macro_hall_switch_mode_t project_v1_macro_get_hall_switch_mode(void)
{
    return g_project_macro_state.hall_switch_mode;
}

void project_v1_macro_set_hall_switch_mode(project_v1_macro_hall_switch_mode_t mode)
{
    if ((uint8_t)mode >= (uint8_t)PROJECT_V1_MACRO_HALL_SWITCH_COUNT)
    {
        return;
    }

    g_project_macro_state.hall_switch_mode = mode;
}

uint8_t project_v1_macro_get_active_bank(void)
{
    return g_project_macro_state.macro_scene[0U];
}

void project_v1_macro_set_active_bank(uint8_t bank)
{
    if (bank >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return;
    }

    if (g_project_macro_state.macro_scene[0U] == bank)
    {
        return;
    }

    g_project_macro_state.macro_scene[0U] = bank;
    param_macro_sync_active_bank();
}

uint8_t project_v1_macro_get_macro_scene(uint8_t macro)
{
    if (macro >= PROJECT_V1_MACRO_POT_COUNT)
    {
        return 0U;
    }

    if (g_project_macro_state.macro_scene[macro] >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return macro;
    }

    return g_project_macro_state.macro_scene[macro];
}

static void project_v1_macro_set_macro_scene_impl(uint8_t macro, uint8_t scene, uint8_t sync_runtime)
{
    if ((macro >= PROJECT_V1_MACRO_POT_COUNT) || (scene >= PROJECT_V1_MACRO_SCENE_COUNT))
    {
        return;
    }

    if (g_project_macro_state.macro_scene[macro] == scene)
    {
        return;
    }

    g_project_macro_state.macro_scene[macro] = scene;
    if (sync_runtime != 0U)
    {
        param_macro_sync_active_bank();
    }
}

void project_v1_macro_set_macro_scene(uint8_t macro, uint8_t scene)
{
    project_v1_macro_set_macro_scene_impl(macro, scene, 1U);
}

void project_v1_macro_set_macro_scene_no_sync(uint8_t macro, uint8_t scene)
{
    project_v1_macro_set_macro_scene_impl(macro, scene, 0U);
}

uint8_t project_v1_macro_scene_has_locks(uint8_t scene)
{
    if (scene >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return 0U;
    }

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        if (project_v1_macro_scene_lock_is_empty(scene, lock) == 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

uint8_t project_v1_macro_assign_scene_lock(uint8_t scene, uint8_t track, param_id_t param, float scene_value)
{
    project_v1_macro_lock_t next;

    if ((scene >= PROJECT_V1_MACRO_SCENE_COUNT)
            || (track >= SEQ_TRACK_COUNT)
            || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    next.track = track;
    next.param = param;
    next.scene_value = scene_value;

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        project_v1_macro_lock_t current;
        if (project_v1_macro_get_scene_lock(scene, lock, &current) == 0U)
        {
            continue;
        }

        if ((current.track == track) && (current.param == param))
        {
            return project_v1_macro_set_scene_lock(scene, lock, &next);
        }
    }

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        if (project_v1_macro_scene_lock_is_empty(scene, lock) != 0U)
        {
            return project_v1_macro_set_scene_lock(scene, lock, &next);
        }
    }

    return 0U;
}

uint8_t project_v1_macro_clear_scene_lock(uint8_t scene, uint8_t track, param_id_t param)
{
    project_v1_macro_lock_t empty;

    if ((scene >= PROJECT_V1_MACRO_SCENE_COUNT)
            || (track >= SEQ_TRACK_COUNT)
            || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    project_v1_macro_clear_lock(&empty);
    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        project_v1_macro_lock_t current;
        if (project_v1_macro_get_scene_lock(scene, lock, &current) == 0U)
        {
            continue;
        }

        if ((current.track == track) && (current.param == param))
        {
            return project_v1_macro_set_scene_lock(scene, lock, &empty);
        }
    }

    return 0U;
}

uint8_t project_v1_macro_get_scene_lock_for_param(uint8_t scene,
                                                  uint8_t track,
                                                  param_id_t param,
                                                  project_v1_macro_lock_t *out_lock)
{
    if ((out_lock == 0)
            || (scene >= PROJECT_V1_MACRO_SCENE_COUNT)
            || (track >= SEQ_TRACK_COUNT)
            || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        project_v1_macro_lock_t current;
        if (project_v1_macro_get_scene_lock(scene, lock, &current) == 0U)
        {
            continue;
        }

        if ((current.track == track) && (current.param == param))
        {
            *out_lock = current;
            return 1U;
        }
    }

    return 0U;
}

uint8_t project_v1_macro_scene_lock_is_empty(uint8_t scene, uint8_t lock)
{
    project_v1_macro_lock_t current;
    if (project_v1_macro_get_scene_lock(scene, lock, &current) == 0U)
    {
        return 1U;
    }

    return ((current.track == PROJECT_V1_MACRO_LOCK_TRACK_NONE)
            || (current.param == PROJECT_V1_MACRO_LOCK_PARAM_NONE)) ? 1U : 0U;
}

uint8_t project_v1_macro_get_scene_lock(uint8_t scene, uint8_t lock, project_v1_macro_lock_t *out_lock)
{
    if ((out_lock == 0) || (project_v1_macro_scene_lock_is_valid(scene, lock) == 0U))
    {
        return 0U;
    }

    *out_lock = g_project_macro_state.scenes[scene].locks[lock];
    return 1U;
}

uint8_t project_v1_macro_set_scene_lock(uint8_t scene, uint8_t lock, const project_v1_macro_lock_t *in_lock)
{
    if ((in_lock == 0) || (project_v1_macro_scene_lock_is_valid(scene, lock) == 0U))
    {
        return 0U;
    }

    g_project_macro_state.scenes[scene].locks[lock] = *in_lock;
    param_macro_sync_active_bank();
    return 1U;
}

uint8_t project_v1_macro_slot_is_empty(uint8_t bank, uint8_t macro, uint8_t slot)
{
    (void)macro;
    return project_v1_macro_scene_lock_is_empty(bank, slot);
}

uint8_t project_v1_macro_get_slot(uint8_t bank, uint8_t macro, uint8_t slot, project_v1_macro_slot_t *out_slot)
{
    (void)macro;
    return project_v1_macro_get_scene_lock(bank, slot, out_slot);
}

uint8_t project_v1_macro_set_slot(uint8_t bank,
                                  uint8_t macro,
                                  uint8_t slot,
                                  const project_v1_macro_slot_t *in_slot)
{
    (void)macro;
    return project_v1_macro_set_scene_lock(bank, slot, in_slot);
}

void project_v1_init(void)
{
    memset(&g_project_work, 0, sizeof(g_project_work));
    project_v1_macro_init();
    project_v1_multi_clear_assignments();
    memset(&g_project_multi_restore_diag, 0, sizeof(g_project_multi_restore_diag));
    g_project_active_slot_valid = 0U;
    g_project_active_slot = 0U;
    g_project_save_counter = 0U;
    g_project_autoload_progress_active = 0U;
    project_v1_clear_autoload_progress_units();
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_sd_bank_init();
    boot_context_flash_init();
}

uint8_t project_v1_capture_current(ProjectSaveV1 *out_project)
{
    if ((out_project == 0) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((out_project == 0) ? PROJECT_V1_ERR_INVALID_ARG : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }

    memset(out_project, 0, sizeof(*out_project));
    sample_pool_capture_project_snapshot(&out_project->sample_pool);
    project_v1_capture_sample_autoload(out_project);
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        out_project->multi[track] = g_project_multi_assign[track];
        out_project->multi[track].gain = brick6_sampler_runtime_get_multi_gain(track);
    }

    if (pattern_live_capture_current(&out_project->live) == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_CAPTURE_FAIL);
        return 0U;
    }

    (void)pattern_live_get_active(&out_project->state.active_pattern_bank,
                                  &out_project->state.active_pattern_slot);
    out_project->state.active_pattern_index = out_project->state.active_pattern_slot;

    (void)pattern_live_get_queued(&out_project->state.queued_pattern_valid,
                                  &out_project->state.queued_pattern_bank,
                                  &out_project->state.queued_pattern_slot);

    out_project->macro = g_project_macro_state;
    out_project->state.active_project_slot_valid = g_project_active_slot_valid;
    out_project->state.active_project_slot = g_project_active_slot;

    for (uint8_t bank = 0U; bank < PROJECT_V1_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PROJECT_V1_PATTERN_COUNT; ++pattern)
        {
            out_project->state.bank_has_data[bank][pattern] = pattern_sd_bank_slot_has_data(bank, pattern);
        }
    }

    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_apply_snapshot(const ProjectSaveV1 *project, uint8_t resume_transport)
{
    if ((project == 0) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project == 0) ? PROJECT_V1_ERR_INVALID_ARG : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }

    if (pattern_live_apply_snapshot(&project->live, resume_transport) == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_APPLY_FAIL);
        return 0U;
    }

    uint8_t restored_active_pattern = project->state.active_pattern_index;
    if (restored_active_pattern >= PROJECT_V1_PATTERN_COUNT)
    {
        restored_active_pattern = project->state.active_pattern_slot;
    }

    pattern_live_set_active_state(project->state.active_pattern_bank,
                                  restored_active_pattern,
                                  project->state.queued_pattern_valid,
                                  project->state.queued_pattern_bank,
                                  project->state.queued_pattern_slot);

    project_v1_multi_restore_from_snapshot(project);

    g_project_macro_state = project->macro;
    project_v1_macro_sanitize_state(&g_project_macro_state);
    param_macro_sync_active_bank();
    g_project_active_slot_valid = project->state.active_project_slot_valid;
    g_project_active_slot = project->state.active_project_slot;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_set_track_multi_path(uint8_t track, const char *path)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_ARG);
        return 0U;
    }

    if ((path == 0) || (path[0] == '\0'))
    {
        g_project_multi_assign[track].path[0] = '\0';
        brick6_sampler_runtime_set_multi_instrument(track, MULTI_SAMPLE_POOL_INVALID_ID);
        project_v1_set_error(PROJECT_V1_ERR_NONE);
        return 1U;
    }

    if (project_v1_copy_text(g_project_multi_assign[track].path,
                             sizeof(g_project_multi_assign[track].path),
                             path)
        == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_ARG);
        return 0U;
    }

    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_get_track_multi_path(uint8_t track, char *out_path, uint32_t out_size)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_path == 0) || (out_size == 0U))
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_ARG);
        return 0U;
    }

    const uint8_t ok =
        project_v1_copy_text(out_path, out_size, g_project_multi_assign[track].path);
    project_v1_set_error((ok != 0U) ? PROJECT_V1_ERR_NONE : PROJECT_V1_ERR_INVALID_ARG);
    return ok;
}

void project_v1_get_multi_restore_diag(project_v1_multi_restore_diag_t *out_diag)
{
    if (out_diag != 0)
    {
        *out_diag = g_project_multi_restore_diag;
    }
}

uint8_t project_v1_get_autoload_progress(project_v1_autoload_progress_t *out_progress)
{
    if (out_progress == 0)
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_ARG);
        return 0U;
    }

    memset(out_progress, 0, sizeof(*out_progress));
    out_progress->complete = 1U;

    if ((g_project_autoload_progress_active == 0U)
        || (g_project_work.sample_autoload.version != PROJECT_V1_SAMPLE_AUTOLOAD_VERSION))
    {
        return 1U;
    }

    out_progress->active = 1U;
    out_progress->complete = 0U;
    const uint8_t multi_pending = multi_sample_load_has_pending();
    multi_sample_load_diag_t multi_diag;
    multi_sample_get_load_diag(&multi_diag);
    const uint16_t count =
        (g_project_work.sample_autoload.count < PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT)
            ? g_project_work.sample_autoload.count
            : PROJECT_V1_SAMPLE_AUTOLOAD_SLOT_COUNT;

    for (uint16_t i = 0U; i < count; ++i)
    {
        const project_v1_sample_autoload_slot_t *const slot =
            &g_project_work.sample_autoload.slots[i];
        if (((slot->flags & PROJECT_V1_SAMPLE_AUTOLOAD_FLAG_ENABLED) == 0U)
            || (slot->path[0] == '\0')
            || (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_EMPTY))
        {
            continue;
        }

        if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_STREAM)
        {
            out_progress->total = (uint16_t)(out_progress->total
                                             + project_v1_autoload_slot_units(i, slot));
            if (slot->slot_index >= SAMPLE_POOL_SIZE)
            {
                out_progress->done = (uint16_t)(out_progress->done
                                                + project_v1_autoload_slot_units(i, slot));
                continue;
            }

            if (sample_pool_get_state(slot->slot_index) != SAMPLE_POOL_SLOT_PREPARING)
            {
                out_progress->done = (uint16_t)(out_progress->done
                                                + project_v1_autoload_slot_units(i, slot));
            }
        }
        else if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_MULTI)
        {
            const uint16_t slot_units = project_v1_autoload_slot_units(i, slot);
            out_progress->total = (uint16_t)(out_progress->total + slot_units);
            if (slot->slot_index >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            {
                out_progress->done = (uint16_t)(out_progress->done + slot_units);
                continue;
            }

            const multi_sample_instrument_state_t state =
                multi_sample_pool_get_state(slot->slot_index);
            if ((state == MULTI_SAMPLE_INSTRUMENT_READY)
                || (state == MULTI_SAMPLE_INSTRUMENT_ERROR)
                || ((multi_pending == 0U) && (state != MULTI_SAMPLE_INSTRUMENT_LOADING)))
            {
                out_progress->done = (uint16_t)(out_progress->done + slot_units);
                continue;
            }

            if ((state == MULTI_SAMPLE_INSTRUMENT_LOADING)
                && (multi_diag.instrument_id == slot->slot_index)
                && (multi_diag.total_samples != 0U))
            {
                uint16_t ready_samples = multi_diag.samples_ready;
                if (ready_samples > multi_diag.total_samples)
                {
                    ready_samples = multi_diag.total_samples;
                }
                if (multi_diag.total_samples == slot_units)
                {
                    out_progress->done = (uint16_t)(out_progress->done + ready_samples);
                }
                else
                {
                    const uint32_t scaled =
                        ((uint32_t)ready_samples * (uint32_t)slot_units)
                        / (uint32_t)multi_diag.total_samples;
                    out_progress->done =
                        (uint16_t)(out_progress->done
                                   + ((scaled < slot_units) ? (uint16_t)scaled : slot_units));
                }
            }
        }
        else if (slot->kind == (uint8_t)PROJECT_V1_SAMPLE_AUTOLOAD_KIND_RAM)
        {
            out_progress->total = (uint16_t)(out_progress->total
                                             + project_v1_autoload_slot_units(i, slot));
            out_progress->done = (uint16_t)(out_progress->done
                                            + project_v1_autoload_slot_units(i, slot));
        }
    }

    if ((out_progress->done >= out_progress->total)
        && (multi_sample_load_has_pending() == 0U))
    {
        out_progress->complete = 1U;
        g_project_autoload_progress_active = 0U;
    }

    return 1U;
}

uint8_t project_v1_save_slot(uint8_t project_slot)
{
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    if (project_v1_capture_current(&g_project_work) == 0U)
    {
        return 0U;
    }

    g_project_work.state.active_project_slot_valid = 1U;
    g_project_work.state.active_project_slot = project_slot;

    if (project_v1_store_snapshot_to_slot(project_slot, &g_project_work, 1U) == 0U)
    {
        return 0U;
    }
    return 1U;
}

uint8_t project_v1_store_snapshot_to_slot(uint8_t project_slot,
                                          const ProjectSaveV1 *project,
                                          uint8_t mark_active_slot)
{
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (project == 0) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT
                                                                      : ((project == 0) ? PROJECT_V1_ERR_INVALID_ARG
                                                                                        : PROJECT_V1_ERR_ISR_CONTEXT));
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    const uint32_t next_counter = g_project_save_counter + 1U;
    if (project_sd_bank_store_slot(project_slot, project, next_counter) == 0U)
    {
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_STORE_FAIL);
        return 0U;
    }

    g_project_save_counter = next_counter;
    if (mark_active_slot != 0U)
    {
        g_project_active_slot_valid = 1U;
        g_project_active_slot = project_slot;
    }
    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    project_boot_ctx_commit_current_state_if_valid();    return 1U;
}

uint8_t project_v1_load_slot(uint8_t project_slot)
{    g_project_autoload_progress_active = 0U;
    project_v1_clear_autoload_progress_units();
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    uint32_t loaded_counter = 0U;
    if (project_sd_bank_load_slot(project_slot, &g_project_work, &loaded_counter) == 0U)
    {
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_LOAD_FAIL);        return 0U;
    }
    project_v1_prepare_autoload_progress_units(&g_project_work);
    sample_global_pool_reset();
    sampler_ram_pool_reset();
    sample_pool_restore_project_snapshot(&g_project_work.sample_pool);
    project_v1_restore_stream_global_slots(&g_project_work);
    project_v1_ram_restore_autoload_slots(&g_project_work);

    if (project_v1_apply_snapshot(&g_project_work, 0U) == 0U)
    {
        project_v1_clear_autoload_progress_units();
        return 0U;
    }

    undo_v2_clear_all();

    if (project_sd_bank_commit_slot_patterns(project_slot) == 0U)
    {
        project_v1_clear_autoload_progress_units();
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_LOAD_FAIL);        return 0U;
    }

    g_project_save_counter = loaded_counter;
    g_project_active_slot_valid = 1U;
    g_project_active_slot = project_slot;
    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    g_project_autoload_progress_active = 1U;
    project_boot_ctx_commit_current_state_if_valid();    return 1U;
}

uint8_t project_v1_load_blank(void)
{
    if (__get_IPSR() != 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    project_v1_reset_blank_transient_runtime();
    sample_global_pool_reset();
    sampler_ram_pool_reset();
    sample_pool_init();
    for (uint16_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        if (multi_sample_pool_get_state(slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            brick6_sampler_runtime_stop_multi_instrument(slot);
            (void)multi_sample_pool_clear_instrument(slot);
        }
    }
    project_v1_macro_init();
    project_v1_multi_clear_assignments();
    memset(&g_project_multi_restore_diag, 0, sizeof(g_project_multi_restore_diag));
    if (pattern_live_apply_boot_snapshot(0U) == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_APPLY_FAIL);
        return 0U;
    }

    param_macro_sync_active_bank();

    undo_v2_clear_all();

    g_project_save_counter = 0U;
    g_project_autoload_progress_active = 0U;
    project_v1_clear_autoload_progress_units();
    g_project_active_slot_valid = 0U;
    g_project_active_slot = 0U;
    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    boot_context_flash_clear();
    return 1U;
}

uint8_t project_v1_delete_slot(uint8_t project_slot)
{
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    if (project_sd_bank_delete_slot(project_slot) == 0U)
    {
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_DELETE_FAIL);
        return 0U;
    }

    if ((g_project_active_slot_valid != 0U) && (g_project_active_slot == project_slot))
    {
        g_project_active_slot_valid = 0U;
        g_project_active_slot = 0U;
        boot_context_flash_clear();
    }

    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_get_active_slot(uint8_t *out_valid, uint8_t *out_slot)
{
    if ((out_valid == 0) || (out_slot == 0))
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_ARG);
        return 0U;
    }

    *out_valid = g_project_active_slot_valid;
    *out_slot = g_project_active_slot;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_slot_has_data(uint8_t project_slot)
{
    if (project_slot >= PROJECT_V1_SLOT_COUNT)
    {
        project_v1_set_error(PROJECT_V1_ERR_INVALID_SLOT);
        return 0U;
    }
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    const uint8_t has_data = project_sd_bank_slot_has_data(project_slot);
    g_project_last_sd_error = project_sd_bank_get_last_error();
    if (g_project_last_sd_error != PROJECT_SD_BANK_ERR_NONE)
    {
        project_v1_set_error(PROJECT_V1_ERR_SD_LOAD_FAIL);
        return 0U;
    }

    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return has_data;
}

void project_v1_refresh_slots(void)
{
    if (project_v1_record_active_guard() != 0U)
    {
        return;
    }

    project_sd_bank_refresh_slots();
    g_project_last_sd_error = project_sd_bank_get_last_error();
    if (g_project_last_sd_error != PROJECT_SD_BANK_ERR_NONE)
    {
        project_v1_set_error(PROJECT_V1_ERR_SD_LOAD_FAIL);
        return;
    }

    project_v1_set_error(PROJECT_V1_ERR_NONE);
}

uint8_t project_v1_list_slots(uint8_t *out_slots, uint8_t max_slots)
{
    if (project_v1_record_active_guard() != 0U)
    {
        return 0U;
    }

    const uint8_t count = project_sd_bank_list_slots(out_slots, max_slots);
    g_project_last_sd_error = project_sd_bank_get_last_error();
    if (g_project_last_sd_error != PROJECT_SD_BANK_ERR_NONE)
    {
        project_v1_set_error((g_project_last_sd_error == PROJECT_SD_BANK_ERR_INVALID_ARG)
                                 ? PROJECT_V1_ERR_INVALID_ARG
                                 : PROJECT_V1_ERR_SD_LOAD_FAIL);
        return 0U;
    }

    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return count;
}

project_v1_error_t project_v1_get_last_error(void)
{
    return g_project_last_error;
}

const char *project_v1_error_to_string(project_v1_error_t err)
{
    switch (err)
    {
        case PROJECT_V1_ERR_NONE: return "NONE";
        case PROJECT_V1_ERR_INVALID_SLOT: return "INVALID_SLOT";
        case PROJECT_V1_ERR_INVALID_ARG: return "INVALID_ARG";
        case PROJECT_V1_ERR_ISR_CONTEXT: return "ISR_CONTEXT";
        case PROJECT_V1_ERR_CAPTURE_FAIL: return "CAPTURE_FAIL";
        case PROJECT_V1_ERR_APPLY_FAIL: return "APPLY_FAIL";
        case PROJECT_V1_ERR_SD_LOAD_FAIL: return "SD_LOAD_FAIL";
        case PROJECT_V1_ERR_SD_STORE_FAIL: return "SD_STORE_FAIL";
        case PROJECT_V1_ERR_SD_DELETE_FAIL: return "SD_DELETE_FAIL";
        case PROJECT_V1_ERR_RECORD_ACTIVE: return "RECORD_ACTIVE";
        default: return "UNKNOWN";
    }
}

uint8_t project_v1_get_last_sd_error_code(void)
{
    return (uint8_t)g_project_last_sd_error;
}

uint8_t project_v1_restore_boot_context(void)
{
    boot_context_flash_data_t ctx;

    if (boot_context_flash_load(&ctx) == 0U)
    {
        return 0U;
    }

    if (ctx.active_project_slot >= PROJECT_V1_SLOT_COUNT)
    {
        return 0U;
    }

    if (project_v1_load_slot(ctx.active_project_slot) == 0U)
    {
        return 0U;
    }

    return 1U;
}
