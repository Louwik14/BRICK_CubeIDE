#include "Storage/sd_access_gate.h"

#include "stm32h7xx_hal.h"

static volatile uint8_t g_sd_access_owner;
static volatile uint8_t g_sd_access_total_count;
static volatile uint8_t g_sd_access_client_count[SD_ACCESS_CLIENT_PREVIEW + 1U];
static FATFS g_sd_fs;
static uint8_t g_sd_fs_mounted;

void sd_access_gate_init(void)
{
    g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_access_total_count = 0U;
    for (uint8_t i = 0U; i <= (uint8_t)SD_ACCESS_CLIENT_PREVIEW; ++i)
    {
        g_sd_access_client_count[i] = 0U;
    }
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
    if ((client == SD_ACCESS_CLIENT_NONE) || (client > SD_ACCESS_CLIENT_PREVIEW))
    {
        return 0U;
    }

    __disable_irq();
    if (g_sd_access_total_count == 0U)
    {
        g_sd_access_owner = (uint8_t)client;
    }
    else if (g_sd_access_owner != (uint8_t)client)
    {
        if ((client == (uint8_t)SD_ACCESS_CLIENT_PREVIEW)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PREVIEW)
            || (client == (uint8_t)SD_ACCESS_CLIENT_SAMPLE_CACHE)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_SAMPLE_CACHE))
        {
            __enable_irq();
            return 0U;
        }

        const uint8_t owner_is_project_pattern =
            ((g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PROJECT)
             || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PATTERN))
                ? 1U
                : 0U;
        const uint8_t requester_is_project_pattern =
            ((client == SD_ACCESS_CLIENT_PROJECT) || (client == SD_ACCESS_CLIENT_PATTERN))
                ? 1U
                : 0U;

        if ((owner_is_project_pattern == 0U) || (requester_is_project_pattern == 0U))
        {
            __enable_irq();
            return 0U;
        }
    }

    g_sd_access_total_count++;
    g_sd_access_client_count[(uint8_t)client]++;
    __enable_irq();
    return 1U;
}

void sd_access_gate_release(sd_access_client_t client)
{
    __disable_irq();
    if (((uint8_t)client <= (uint8_t)SD_ACCESS_CLIENT_PREVIEW)
        && (g_sd_access_client_count[(uint8_t)client] != 0U)
        && (g_sd_access_total_count != 0U))
    {
        g_sd_access_client_count[(uint8_t)client]--;
        g_sd_access_total_count--;
    }

    if (g_sd_access_total_count == 0U)
    {
        g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    }
    __enable_irq();
}

void sd_access_trace_begin(const char *op)
{
    (void)op;
}

void sd_access_trace_end(const char *op, int result, uint32_t elapsed_ms)
{
    (void)op;
    (void)result;
    (void)elapsed_ms;
}

void sd_access_trace_timeout(const char *stage, uint32_t elapsed_ms)
{
    (void)stage;
    (void)elapsed_ms;
}
