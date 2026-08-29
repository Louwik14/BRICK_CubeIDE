#include "Platform/diagnostic_watchdog.h"

#include "Platform/crash_capsule.h"
#include "stm32h7xx_hal.h"

#define DIAGNOSTIC_WATCHDOG_LSI_TIMEOUT_MS       100U
#define DIAGNOSTIC_WATCHDOG_UPDATE_TIMEOUT_MS    100U
#define DIAGNOSTIC_WATCHDOG_PRESCALER            6U
#define DIAGNOSTIC_WATCHDOG_RELOAD               1499U
#define DIAGNOSTIC_WATCHDOG_WINDOW               4095U
#define DIAGNOSTIC_WATCHDOG_CHECKPOINT_TICKS     1500U

#define DIAGNOSTIC_WATCHDOG_KEY_START            0xCCCCU
#define DIAGNOSTIC_WATCHDOG_KEY_RELOAD           0xAAAAU
#define DIAGNOSTIC_WATCHDOG_KEY_WRITE_ACCESS     0x5555U

typedef struct
{
    uint32_t last_engine_tick;
    uint32_t last_checkpoint_tick;
    uint32_t heartbeat_count;
    uint8_t armed;
} diagnostic_watchdog_runtime_t;

static diagnostic_watchdog_runtime_t g_diagnostic_watchdog;

static uint8_t diagnostic_watchdog_wait_lsi(void)
{
    const uint32_t started_ms = HAL_GetTick();
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U)
    {
        if ((HAL_GetTick() - started_ms)
            >= DIAGNOSTIC_WATCHDOG_LSI_TIMEOUT_MS)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t diagnostic_watchdog_wait_update(void)
{
    const uint32_t started_ms = HAL_GetTick();
    while ((IWDG1->SR & (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU)) != 0U)
    {
        if ((HAL_GetTick() - started_ms)
            >= DIAGNOSTIC_WATCHDOG_UPDATE_TIMEOUT_MS)
        {
            return 0U;
        }
    }
    return 1U;
}

void diagnostic_watchdog_init(void)
{
    g_diagnostic_watchdog.last_engine_tick = 0U;
    g_diagnostic_watchdog.last_checkpoint_tick = 0U;
    g_diagnostic_watchdog.heartbeat_count = 0U;
    g_diagnostic_watchdog.armed = 0U;
}

uint8_t diagnostic_watchdog_arm(uint32_t engine_tick)
{
    if (g_diagnostic_watchdog.armed != 0U)
    {
        g_diagnostic_watchdog.last_engine_tick = engine_tick;
        g_diagnostic_watchdog.last_checkpoint_tick = engine_tick;
        g_diagnostic_watchdog.heartbeat_count = 0U;
        crash_capsule_watchdog_arm(engine_tick);
        return 1U;
    }

    RCC->CSR |= RCC_CSR_LSION;
    if (diagnostic_watchdog_wait_lsi() == 0U)
    {
        return 0U;
    }

#if defined(DEBUG)
    __HAL_DBGMCU_FREEZE_IWDG1();
#endif

    crash_capsule_watchdog_arm(engine_tick);
    IWDG1->KR = DIAGNOSTIC_WATCHDOG_KEY_START;
    IWDG1->KR = DIAGNOSTIC_WATCHDOG_KEY_WRITE_ACCESS;
    IWDG1->PR = DIAGNOSTIC_WATCHDOG_PRESCALER;
    IWDG1->RLR = DIAGNOSTIC_WATCHDOG_RELOAD;
    IWDG1->WINR = DIAGNOSTIC_WATCHDOG_WINDOW;
    if (diagnostic_watchdog_wait_update() == 0U)
    {
        NVIC_SystemReset();
    }
    IWDG1->KR = DIAGNOSTIC_WATCHDOG_KEY_RELOAD;

    g_diagnostic_watchdog.last_engine_tick = engine_tick;
    g_diagnostic_watchdog.last_checkpoint_tick = engine_tick;
    g_diagnostic_watchdog.heartbeat_count = 0U;
    g_diagnostic_watchdog.armed = 1U;
    return 1U;
}

void diagnostic_watchdog_main_loop_heartbeat(uint32_t engine_tick)
{
    if ((g_diagnostic_watchdog.armed == 0U)
        || (engine_tick == g_diagnostic_watchdog.last_engine_tick))
    {
        return;
    }

    g_diagnostic_watchdog.last_engine_tick = engine_tick;
    g_diagnostic_watchdog.heartbeat_count++;
    IWDG1->KR = DIAGNOSTIC_WATCHDOG_KEY_RELOAD;

    if ((engine_tick - g_diagnostic_watchdog.last_checkpoint_tick)
        >= DIAGNOSTIC_WATCHDOG_CHECKPOINT_TICKS)
    {
        g_diagnostic_watchdog.last_checkpoint_tick = engine_tick;
        crash_capsule_watchdog_checkpoint(
            g_diagnostic_watchdog.heartbeat_count, engine_tick);
    }
}
