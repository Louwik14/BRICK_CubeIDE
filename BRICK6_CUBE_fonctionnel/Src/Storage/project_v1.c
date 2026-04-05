#include "Storage/project_v1.h"

#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/pattern_sd_bank.h"
#include "Storage/project_sd_bank.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "stm32h7xx_hal.h"

UI_SDRAM static ProjectSaveV1 g_project_work;
static uint8_t g_project_active_slot_valid;
static uint8_t g_project_active_slot;
static uint32_t g_project_save_counter;

void project_v1_init(void)
{
    memset(&g_project_work, 0, sizeof(g_project_work));
    g_project_active_slot_valid = 0U;
    g_project_active_slot = 0U;
    g_project_save_counter = 0U;
    project_sd_bank_init();
}

uint8_t project_v1_capture_current(ProjectSaveV1 *out_project)
{
    if ((out_project == 0) || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    memset(out_project, 0, sizeof(*out_project));

    if (pattern_live_capture_current(&out_project->live) == 0U)
    {
        return 0U;
    }

    (void)pattern_live_get_active(&out_project->state.active_pattern_bank,
                                  &out_project->state.active_pattern_slot);

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

    return 1U;
}

uint8_t project_v1_apply_snapshot(const ProjectSaveV1 *project, uint8_t resume_transport)
{
    if ((project == 0) || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    (void)resume_transport;

    seq_runtime_stop();
    seq_output_guard_panic(1U);

    if (pattern_live_apply_snapshot(&project->live, 0U) == 0U)
    {
        return 0U;
    }

    pattern_live_set_active_state(project->state.active_pattern_bank,
                                  project->state.active_pattern_slot,
                                  project->state.queued_pattern_valid,
                                  project->state.queued_pattern_bank,
                                  project->state.queued_pattern_slot);

    g_project_active_slot_valid = project->state.active_project_slot_valid;
    g_project_active_slot = project->state.active_project_slot;
    return 1U;
}

uint8_t project_v1_save_slot(uint8_t project_slot)
{
    printf("[PRJ][SAVE] enter slot=%u\r\n", (unsigned)project_slot);
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        printf("[PRJ][SAVE] reject invalid_slot_or_isr slot=%u ipsr=%lu\r\n",
               (unsigned)project_slot,
               (unsigned long)__get_IPSR());
        return 0U;
    }

    if (project_v1_capture_current(&g_project_work) == 0U)
    {
        printf("[PRJ][SAVE] fail capture_current\r\n");
        return 0U;
    }

    g_project_work.state.active_project_slot_valid = 1U;
    g_project_work.state.active_project_slot = project_slot;

    const uint32_t next_counter = g_project_save_counter + 1U;
    if (project_sd_bank_store_slot(project_slot, &g_project_work, next_counter) == 0U)
    {
        printf("[PRJ][SAVE] fail store_slot slot=%u save_counter=%lu\r\n",
               (unsigned)project_slot,
               (unsigned long)next_counter);
        return 0U;
    }

    g_project_save_counter = next_counter;
    g_project_active_slot_valid = 1U;
    g_project_active_slot = project_slot;
    printf("[PRJ][SAVE] ok slot=%u save_counter=%lu\r\n",
           (unsigned)project_slot,
           (unsigned long)g_project_save_counter);
    return 1U;
}

uint8_t project_v1_load_slot(uint8_t project_slot)
{
    printf("[PRJ][LOAD] enter slot=%u\r\n", (unsigned)project_slot);
    if ((project_slot >= PROJECT_V1_SLOT_COUNT) || (__get_IPSR() != 0U))
    {
        printf("[PRJ][LOAD] reject invalid_slot_or_isr slot=%u ipsr=%lu\r\n",
               (unsigned)project_slot,
               (unsigned long)__get_IPSR());
        return 0U;
    }

    uint32_t loaded_counter = 0U;
    if (project_sd_bank_load_slot(project_slot, &g_project_work, &loaded_counter) == 0U)
    {
        printf("[PRJ][LOAD] fail load_slot slot=%u\r\n", (unsigned)project_slot);
        return 0U;
    }

    if (project_v1_apply_snapshot(&g_project_work, 0U) == 0U)
    {
        printf("[PRJ][LOAD] fail apply_snapshot slot=%u\r\n", (unsigned)project_slot);
        return 0U;
    }

    g_project_save_counter = loaded_counter;
    g_project_active_slot_valid = 1U;
    g_project_active_slot = project_slot;
    printf("[PRJ][LOAD] ok slot=%u save_counter=%lu\r\n",
           (unsigned)project_slot,
           (unsigned long)g_project_save_counter);
    return 1U;
}

uint8_t project_v1_get_active_slot(uint8_t *out_valid, uint8_t *out_slot)
{
    if ((out_valid == 0) || (out_slot == 0))
    {
        return 0U;
    }

    *out_valid = g_project_active_slot_valid;
    *out_slot = g_project_active_slot;
    return 1U;
}

uint8_t project_v1_slot_has_data(uint8_t project_slot)
{
    return project_sd_bank_slot_has_data(project_slot);
}
