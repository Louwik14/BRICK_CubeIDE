#include "Storage/project_v1.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_sd_bank.h"
#include "Storage/project_sd_bank.h"
#include "Storage/undo_v1.h"
#include "Seq/seq_runtime.h"
#include "stm32h7xx_hal.h"

UI_SDRAM static ProjectSaveV1 g_project_work;
static uint8_t g_project_active_slot_valid;
static uint8_t g_project_active_slot;
static uint32_t g_project_save_counter;
static project_v1_error_t g_project_last_error;
static project_sd_bank_error_t g_project_last_sd_error;

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

void project_v1_init(void)
{
    memset(&g_project_work, 0, sizeof(g_project_work));
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

    undo_v1_clear_history();

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
    if (pattern_live_apply_boot_snapshot(0U) == 0U)
    {
        project_v1_set_error(PROJECT_V1_ERR_APPLY_FAIL);
        return 0U;
    }

    undo_v1_clear_history();

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
