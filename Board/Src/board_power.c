#include "Board/board_power.h"

#include "main.h"
#include "App/control_rt_wakeup.h"
#include "stm32h7xx_hal.h"
#include "usb_role_manager.h"

#define POWER_BOOT_PRESS_MS 1000UL
#define POWER_OFF_HOLD_MS 2000UL
#define POWER_BUTTON_DEBOUNCE_MS 20UL

static volatile uint8_t g_shutdown_initialized;
static volatile uint8_t g_shutdown_raw_state;
static uint8_t g_shutdown_stable_state;
static uint8_t g_shutdown_armed;
static uint8_t g_shutdown_hold_active;
static uint8_t g_shutdown_request_fired;
static volatile uint32_t g_shutdown_raw_changed_ms;
static uint32_t g_shutdown_hold_start_ms;

void board_power_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void board_power_shutdown_init(void)
{
    g_shutdown_initialized = 0U;
    g_shutdown_raw_state = 0U;
    g_shutdown_stable_state = 0U;
    g_shutdown_armed = 0U;
    g_shutdown_hold_active = 0U;
    g_shutdown_request_fired = 0U;
    g_shutdown_raw_changed_ms = 0U;
    g_shutdown_hold_start_ms = 0U;
}

void board_power_shutdown_sample(uint32_t now_ms)
{
    const uint8_t sampled_state =
        (HAL_GPIO_ReadPin(POWER_BUTTON_SENSE_GPIO_Port,
                          POWER_BUTTON_SENSE_Pin) == GPIO_PIN_SET) ? 1U : 0U;

    if (g_shutdown_initialized == 0U)
    {
        g_shutdown_initialized = 1U;
        g_shutdown_raw_state = sampled_state;
        g_shutdown_stable_state = sampled_state;
        g_shutdown_armed = (sampled_state == 0U) ? 1U : 0U;
        g_shutdown_raw_changed_ms = now_ms;
    }
    else if (sampled_state != g_shutdown_raw_state)
    {
        g_shutdown_raw_state = sampled_state;
        g_shutdown_raw_changed_ms = now_ms;
    }
}

void board_power_shutdown_poll_irq(void)
{
    const uint32_t now_ms = HAL_GetTick();
    const uint8_t sampled_state =
        (HAL_GPIO_ReadPin(POWER_BUTTON_SENSE_GPIO_Port,
                          POWER_BUTTON_SENSE_Pin) == GPIO_PIN_SET) ? 1U : 0U;

    if (g_shutdown_initialized == 0U)
    {
        board_power_shutdown_sample(now_ms);
        return;
    }

    if (sampled_state != g_shutdown_raw_state)
    {
        g_shutdown_raw_state = sampled_state;
        g_shutdown_raw_changed_ms = now_ms;
        control_rt_wakeup(CONTROL_RT_WAKE_INPUT);
    }
}

uint8_t board_power_shutdown_process_deadline(uint32_t now_ms)
{
    if (g_shutdown_initialized == 0U)
        return 0U;

    if ((g_shutdown_stable_state != g_shutdown_raw_state)
        && ((uint32_t)(now_ms - g_shutdown_raw_changed_ms)
            >= POWER_BUTTON_DEBOUNCE_MS))
    {
        g_shutdown_stable_state = g_shutdown_raw_state;
    }

    if (g_shutdown_armed == 0U)
    {
        if (g_shutdown_stable_state == 0U) g_shutdown_armed = 1U;
        return 0U;
    }

    if (g_shutdown_stable_state == 0U)
    {
        g_shutdown_hold_active = 0U;
        g_shutdown_request_fired = 0U;
        g_shutdown_hold_start_ms = 0U;
        return 0U;
    }

    if (g_shutdown_hold_active == 0U)
    {
        g_shutdown_hold_active = 1U;
        g_shutdown_hold_start_ms = now_ms;
        return 0U;
    }

    if ((g_shutdown_request_fired == 0U)
        && ((uint32_t)(now_ms - g_shutdown_hold_start_ms)
            >= POWER_OFF_HOLD_MS))
    {
        g_shutdown_request_fired = 1U;
        return 1U;
    }
    return 0U;
}

uint8_t board_power_shutdown_next_deadline(uint32_t now_ms,
                                            uint32_t *out_deadline_ms)
{
    if (out_deadline_ms == NULL || g_shutdown_initialized == 0U)
        return 0U;

    if (g_shutdown_stable_state != g_shutdown_raw_state)
    {
        *out_deadline_ms = g_shutdown_raw_changed_ms
            + POWER_BUTTON_DEBOUNCE_MS;
        if ((int32_t)(*out_deadline_ms - now_ms) < 0)
            *out_deadline_ms = now_ms;
        return 1U;
    }

    if ((g_shutdown_armed != 0U)
        && (g_shutdown_stable_state != 0U)
        && (g_shutdown_hold_active != 0U)
        && (g_shutdown_request_fired == 0U))
    {
        *out_deadline_ms = g_shutdown_hold_start_ms + POWER_OFF_HOLD_MS;
        if ((int32_t)(*out_deadline_ms - now_ms) < 0)
            *out_deadline_ms = now_ms;
        return 1U;
    }

    return 0U;
}

void board_power_usb_host_off(void)
{
    usb_role_manager_shutdown();
}

void board_power_shutdown_cut(void)
{
    HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);
}

void board_power_hold_enable_after_boot_press(void)
{
    uint32_t high_start_ms = 0U;
    uint8_t high_seen = 0U;

    HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);

    for (;;)
    {
        const uint32_t now_ms = HAL_GetTick();
        const GPIO_PinState button_state =
            HAL_GPIO_ReadPin(POWER_BUTTON_SENSE_GPIO_Port, POWER_BUTTON_SENSE_Pin);

        if (button_state != GPIO_PIN_SET)
        {
            HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);
            high_seen = 0U;
            high_start_ms = 0U;
            continue;
        }

        if (high_seen == 0U)
        {
            high_seen = 1U;
            high_start_ms = now_ms;
            continue;
        }

        if ((now_ms - high_start_ms) >= POWER_BOOT_PRESS_MS)
        {
            HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_SET);
            return;
        }
    }
}
