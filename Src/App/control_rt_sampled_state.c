#include "App/control_rt_sampled_state.h"

#include "App/Hall/hall_calibration.h"
#include "App/power_shutdown.h"
#include "Seq/seq_edit.h"
#include "UI/ui_hall_mode_flow.h"
#include "UI/ui_event.h"
#include "buttons.h"
#include "stm32h7xx_hal.h"

static uint32_t g_last_sample_ms;
static uint8_t g_sample_initialized;

void control_rt_sampled_state_init(void)
{
    buttons_init();
    power_shutdown_init();
    g_last_sample_ms = 0U;
    g_sample_initialized = 0U;
}

void control_rt_sampled_state_process(uint32_t now_ms)
{
    const uint32_t elapsed_ms = (g_sample_initialized == 0U)
        ? 0U : (uint32_t)(now_ms - g_last_sample_ms);

    /* Sampled state is refreshed only as part of a real CONTROL wake. */
    buttons_update(elapsed_ms);
    ui_event_from_inputs();
    power_shutdown_sample(now_ms);
    g_last_sample_ms = now_ms;
    g_sample_initialized = 1U;

    uint32_t deadline_ms = 0U;
    if (power_shutdown_next_deadline(now_ms, &deadline_ms) != 0U
        && ((int32_t)(now_ms - deadline_ms) >= 0))
        (void)power_shutdown_process_deadline(now_ms);
    if (seq_edit_step_hold_next_deadline(now_ms, &deadline_ms) != 0U
        && ((int32_t)(now_ms - deadline_ms) >= 0))
        seq_edit_step_hold_process_deadline(now_ms);
}

uint8_t control_rt_sampled_state_next_deadline(uint32_t now_ms,
                                                uint32_t *out_deadline_ms)
{
    if (out_deadline_ms == 0)
    {
        return 0U;
    }
    uint8_t found = 0U;
    uint32_t next_deadline_ms = 0U;

    /* No default cadence: every returned deadline belongs to an owner. */
    uint32_t deadline_ms = 0U;
    if (power_shutdown_next_deadline(now_ms, &deadline_ms) != 0U
        && ((found == 0U) || ((int32_t)(deadline_ms - next_deadline_ms) < 0)))
    {
        next_deadline_ms = deadline_ms;
        found = 1U;
    }
    if (seq_edit_step_hold_next_deadline(now_ms, &deadline_ms) != 0U
        && ((found == 0U) || ((int32_t)(deadline_ms - next_deadline_ms) < 0)))
    {
        next_deadline_ms = deadline_ms;
        found = 1U;
    }
    if (hall_calibration_next_deadline(now_ms, &deadline_ms) != 0U
        && ((found == 0U) || ((int32_t)(deadline_ms - next_deadline_ms) < 0)))
    {
        next_deadline_ms = deadline_ms;
        found = 1U;
    }
    if (hall_user_calibration_next_deadline(now_ms, &deadline_ms) != 0U
        && ((found == 0U) || ((int32_t)(deadline_ms - next_deadline_ms) < 0)))
    {
        next_deadline_ms = deadline_ms;
        found = 1U;
    }
    *out_deadline_ms = next_deadline_ms;
    return found;
}
