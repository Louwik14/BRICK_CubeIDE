#include "Storage/sd_access_gate.h"

#include <stdio.h>

#include "stm32h7xx_hal.h"

static volatile uint8_t g_sd_access_owner;
static FATFS g_sd_fs;
static uint8_t g_sd_fs_mounted;

void sd_access_gate_init(void)
{
    g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_fs_mounted = 0U;
}

uint8_t sd_access_fs_mount_if_needed(void)
{
    if (g_sd_fs_mounted != 0U)
    {
        return 1U;
    }

    sd_access_trace_begin("shared_f_mount");
    const FRESULT fr = f_mount(&g_sd_fs, "0:", 1U);
    sd_access_trace_end("shared_f_mount", (int)fr, 0U);
    if (fr != FR_OK)
    {
        return 0U;
    }

    g_sd_fs_mounted = 1U;
    return 1U;
}

uint8_t sd_access_gate_try_acquire(sd_access_client_t client)
{
    if ((client == SD_ACCESS_CLIENT_NONE) || (client > SD_ACCESS_CLIENT_PROJECT))
    {
        return 0U;
    }

    __disable_irq();
    if (((g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PROJECT) && (client == SD_ACCESS_CLIENT_PATTERN))
        || ((g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PATTERN) && (client == SD_ACCESS_CLIENT_PROJECT)))
    {
        __enable_irq();
        return 1U;
    }

    if ((g_sd_access_owner != (uint8_t)SD_ACCESS_CLIENT_NONE)
        && (g_sd_access_owner != (uint8_t)client))
    {
        __enable_irq();
        return 0U;
    }

    g_sd_access_owner = (uint8_t)client;
    __enable_irq();
    return 1U;
}

void sd_access_gate_release(sd_access_client_t client)
{
    __disable_irq();
    if (g_sd_access_owner == (uint8_t)client)
    {
        g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    }
    __enable_irq();
}

void sd_access_trace_begin(const char *op)
{
#if SD_ACCESS_TRACE_ENABLED
    if (op != 0)
    {
        printf("[SD][TRACE] begin %s\r\n", op);
    }
#else
    (void)op;
#endif
}

void sd_access_trace_end(const char *op, int result, uint32_t elapsed_ms)
{
#if SD_ACCESS_TRACE_ENABLED
    if (op != 0)
    {
        printf("[SD][TRACE] end %s res=%d dt=%lums\r\n",
               op,
               result,
               (unsigned long)elapsed_ms);
    }
#else
    (void)op;
    (void)result;
    (void)elapsed_ms;
#endif
}

void sd_access_trace_timeout(const char *stage, uint32_t elapsed_ms)
{
#if SD_ACCESS_TRACE_ENABLED
    if (stage != 0)
    {
        printf("[SD][TRACE] timeout %s dt=%lums\r\n",
               stage,
               (unsigned long)elapsed_ms);
    }
#else
    (void)stage;
    (void)elapsed_ms;
#endif
}
