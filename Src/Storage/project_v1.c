#include "Storage/project_v1.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_sd_bank.h"
#include "Storage/project_sd_bank.h"
#include "Storage/undo_v2.h"
#include "Param/param_macro.h"
#include "Seq/seq_runtime.h"
#include "stm32h7xx_hal.h"

UI_SDRAM static ProjectSaveV1 g_project_work;
static project_v1_macro_state_t g_project_macro_state;
static uint8_t g_project_active_slot_valid;
static uint8_t g_project_active_slot;
static uint32_t g_project_save_counter;
static project_v1_error_t g_project_last_error;
static project_sd_bank_error_t g_project_last_sd_error;

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

static void project_v1_set_sd_operation_error(project_v1_error_t err)
{
    g_project_last_sd_error = project_sd_bank_get_last_error();
    project_v1_set_error(err);
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
    g_project_active_slot_valid = 0U;
    g_project_active_slot = 0U;
    g_project_save_counter = 0U;
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

    g_project_macro_state = project->macro;
    project_v1_macro_sanitize_state(&g_project_macro_state);
    param_macro_sync_active_bank();
    g_project_active_slot_valid = project->state.active_project_slot_valid;
    g_project_active_slot = project->state.active_project_slot;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    return 1U;
}

uint8_t project_v1_save_slot(uint8_t project_slot)
{
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT : PROJECT_V1_ERR_ISR_CONTEXT);
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
{    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        project_v1_set_error((project_slot >= PROJECT_V1_SLOT_COUNT) ? PROJECT_V1_ERR_INVALID_SLOT : PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }

    uint32_t loaded_counter = 0U;
    if (project_sd_bank_load_slot(project_slot, &g_project_work, &loaded_counter) == 0U)
    {
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_LOAD_FAIL);        return 0U;
    }
    sample_pool_restore_project_snapshot(&g_project_work.sample_pool);

    if (project_v1_apply_snapshot(&g_project_work, 0U) == 0U)
    {        return 0U;
    }

    undo_v2_clear_all();

    if (project_sd_bank_commit_slot_patterns(project_slot) == 0U)
    {
        project_v1_set_sd_operation_error(PROJECT_V1_ERR_SD_LOAD_FAIL);        return 0U;
    }

    g_project_save_counter = loaded_counter;
    g_project_active_slot_valid = 1U;
    g_project_active_slot = project_slot;
    g_project_last_sd_error = PROJECT_SD_BANK_ERR_NONE;
    project_v1_set_error(PROJECT_V1_ERR_NONE);
    project_boot_ctx_commit_current_state_if_valid();    return 1U;
}

uint8_t project_v1_load_blank(void)
{
    if (__get_IPSR() != 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_ISR_CONTEXT);
        return 0U;
    }

    sample_pool_init();
    project_v1_macro_init();
    if (pattern_live_apply_boot_snapshot(0U) == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_APPLY_FAIL);
        return 0U;
    }

    param_macro_sync_active_bank();

    undo_v2_clear_all();

    g_project_save_counter = 0U;
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
