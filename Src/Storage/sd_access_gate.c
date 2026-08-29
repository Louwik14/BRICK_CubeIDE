#include "Storage/sd_access_gate.h"

#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

static volatile uint8_t g_sd_access_owner;
static volatile uint8_t g_sd_access_last_owner;
static volatile uint8_t g_sd_access_total_count;
static volatile uint8_t g_sd_access_client_count[SD_ACCESS_CLIENT_MAX + 1U];
static volatile uint32_t g_sd_access_acquire_fail_count[SD_ACCESS_CLIENT_MAX + 1U];
static volatile uint8_t g_sd_access_streaming_critical;
static volatile uint32_t g_sd_access_owner_acquire_tick;
static volatile uint32_t g_sd_access_max_hold_ticks;
static volatile uint32_t g_sd_access_owner_acquire_cycle;
static volatile uint32_t g_sd_access_client_cycles[SD_ACCESS_CLIENT_MAX + 1U];
STORAGE_STATE_SDRAM static FATFS g_sd_fs;
static uint8_t g_sd_fs_mounted;
static volatile uint32_t g_sd_media_epoch;
static uint8_t g_sd_media_present_known;
static uint8_t g_sd_media_present;

void sd_access_gate_init(void)
{
    g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_access_last_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_access_total_count = 0U;
    g_sd_access_streaming_critical = 0U;
    g_sd_access_owner_acquire_tick = 0U;
    g_sd_access_max_hold_ticks = 0U;
    for (uint8_t i = 0U; i <= (uint8_t)SD_ACCESS_CLIENT_MAX; ++i)
    {
        g_sd_access_client_count[i] = 0U;
        g_sd_access_acquire_fail_count[i] = 0U;
        g_sd_access_client_cycles[i] = 0U;
    }
    g_sd_fs_mounted = 0U;
    g_sd_media_present_known = 0U;
    g_sd_media_present = 0U;
    g_sd_media_epoch++;
    if (g_sd_media_epoch == 0U)
    {
        g_sd_media_epoch = 1U;
    }
    g_sd_access_owner_acquire_cycle = 0U;
}

uint8_t sd_access_fs_mount_if_needed(void)
{
    if (g_sd_fs_mounted != 0U)
    {
        return 1U;
    }

    const FRESULT fr = f_mount(&g_sd_fs, "0:", 1U);
    if (fr != FR_OK)
    {
        return 0U;
    }

    g_sd_fs_mounted = 1U;
    return 1U;
}

void sd_access_fs_invalidate_mount(void)
{
    g_sd_fs_mounted = 0U;
    sd_access_media_epoch_advance();
}

uint32_t sd_access_media_epoch(void)
{
    uint32_t epoch;
    __disable_irq();
    epoch = g_sd_media_epoch;
    __enable_irq();
    return epoch;
}

void sd_access_media_epoch_advance(void)
{
    __disable_irq();
    g_sd_media_epoch++;
    if (g_sd_media_epoch == 0U)
    {
        g_sd_media_epoch = 1U;
    }
    __enable_irq();
}

void sd_access_media_set_present(uint8_t present)
{
    present = (present != 0U) ? 1U : 0U;
    if (g_sd_media_present_known == 0U)
    {
        g_sd_media_present_known = 1U;
        g_sd_media_present = present;
        return;
    }
    if (g_sd_media_present == present)
    {
        return;
    }
    g_sd_media_present = present;
    g_sd_fs_mounted = 0U;
    sd_access_media_epoch_advance();
}

uint8_t sd_access_gate_try_acquire(sd_access_client_t client)
{
    if ((client == SD_ACCESS_CLIENT_NONE) || (client > SD_ACCESS_CLIENT_MAX))
    {
        return 0U;
    }

    __disable_irq();
    if ((g_sd_access_streaming_critical != 0U)
        && (g_sd_access_total_count == 0U)
        && (client != SD_ACCESS_CLIENT_SAMPLE_STREAM)
        && (client != SD_ACCESS_CLIENT_SCHEDULED_RECORDER))
    {
        g_sd_access_acquire_fail_count[(uint8_t)client]++;
        __enable_irq();
        return 0U;
    }

    if (g_sd_access_total_count == 0U)
    {
        g_sd_access_owner = (uint8_t)client;
        g_sd_access_owner_acquire_tick = HAL_GetTick();
        g_sd_access_owner_acquire_cycle = DWT->CYCCNT;
    }
    else if (g_sd_access_owner != (uint8_t)client)
    {
        if ((client == SD_ACCESS_CLIENT_PREVIEW)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PREVIEW)
            || (client == SD_ACCESS_CLIENT_WAV_CONVERT)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_WAV_CONVERT)
            || (client == SD_ACCESS_CLIENT_EDITOR_CACHE)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_EDITOR_CACHE)
            || (client == SD_ACCESS_CLIENT_WAVEFORM_CACHE)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_WAVEFORM_CACHE)
            || (client == SD_ACCESS_CLIENT_SAMPLE_CACHE)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_SAMPLE_CACHE)
            || (client == SD_ACCESS_CLIENT_SAMPLE_STREAM)
            || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_SAMPLE_STREAM))
        {
            g_sd_access_acquire_fail_count[(uint8_t)client]++;
            __enable_irq();
            return 0U;
        }

        const uint8_t owner_is_project_pattern =
            ((g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PROJECT)
             || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PATTERN)
             || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PATCH))
                ? 1U
                : 0U;
        const uint8_t requester_is_project_pattern =
            ((client == SD_ACCESS_CLIENT_PROJECT)
             || (client == SD_ACCESS_CLIENT_PATTERN)
             || (client == SD_ACCESS_CLIENT_PATCH))
                ? 1U
                : 0U;

        if ((owner_is_project_pattern == 0U) || (requester_is_project_pattern == 0U))
        {
            g_sd_access_acquire_fail_count[(uint8_t)client]++;
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
    if (((uint8_t)client <= (uint8_t)SD_ACCESS_CLIENT_MAX)
        && (g_sd_access_client_count[(uint8_t)client] != 0U)
        && (g_sd_access_total_count != 0U))
    {
        g_sd_access_client_count[(uint8_t)client]--;
        g_sd_access_total_count--;
    }

    if (g_sd_access_total_count == 0U)
    {
        if (g_sd_access_owner != (uint8_t)SD_ACCESS_CLIENT_NONE)
        {
            g_sd_access_client_cycles[g_sd_access_owner] +=
                DWT->CYCCNT - g_sd_access_owner_acquire_cycle;
        }
        const uint32_t hold_ticks = HAL_GetTick() - g_sd_access_owner_acquire_tick;
        if ((g_sd_access_owner != (uint8_t)SD_ACCESS_CLIENT_NONE)
            && (hold_ticks > g_sd_access_max_hold_ticks))
        {
            g_sd_access_max_hold_ticks = hold_ticks;
        }
        g_sd_access_last_owner = g_sd_access_owner;
        g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
        g_sd_access_owner_acquire_tick = 0U;
    }
    __enable_irq();
}

uint32_t sd_access_gate_client_cycles(sd_access_client_t client)
{
    uint32_t cycles = 0U;
    if ((client == SD_ACCESS_CLIENT_NONE) || (client > SD_ACCESS_CLIENT_MAX))
    {
        return 0U;
    }
    __disable_irq();
    cycles = g_sd_access_client_cycles[(uint8_t)client];
    __enable_irq();
    return cycles;
}

void sd_access_gate_set_streaming_critical(uint8_t active)
{
    __disable_irq();
    g_sd_access_streaming_critical = (active != 0U) ? 1U : 0U;
    __enable_irq();
}

uint8_t sd_access_gate_streaming_critical_active(void)
{
    uint8_t active;
    __disable_irq();
    active = g_sd_access_streaming_critical;
    __enable_irq();
    return active;
}

sd_access_client_t sd_access_gate_current_owner(void)
{
    uint8_t owner;
    __disable_irq();
    owner = g_sd_access_owner;
    __enable_irq();
    return (sd_access_client_t)owner;
}

sd_access_client_t sd_access_gate_last_owner(void)
{
    uint8_t owner;
    __disable_irq();
    owner = g_sd_access_last_owner;
    __enable_irq();
    return (sd_access_client_t)owner;
}

uint32_t sd_access_gate_max_hold_ticks(void)
{
    uint32_t ticks;
    __disable_irq();
    ticks = g_sd_access_max_hold_ticks;
    __enable_irq();
    return ticks;
}

uint32_t sd_access_gate_acquire_fail_count(sd_access_client_t client)
{
    uint32_t count = 0U;
    if ((client == SD_ACCESS_CLIENT_NONE) || (client > SD_ACCESS_CLIENT_MAX))
    {
        return 0U;
    }

    __disable_irq();
    count = g_sd_access_acquire_fail_count[(uint8_t)client];
    __enable_irq();
    return count;
}

const char *sd_access_gate_client_label(sd_access_client_t client)
{
    switch (client)
    {
        case SD_ACCESS_CLIENT_RECORDER:
            return "REC";
        case SD_ACCESS_CLIENT_SAMPLE_BOOT:
            return "BOOT";
        case SD_ACCESS_CLIENT_PATTERN:
            return "PATT";
        case SD_ACCESS_CLIENT_PROJECT:
            return "PROJ";
        case SD_ACCESS_CLIENT_SAMPLE_CACHE:
            return "CACHE";
        case SD_ACCESS_CLIENT_PREVIEW:
            return "PREV";
        case SD_ACCESS_CLIENT_WAV_CONVERT:
            return "CONV";
        case SD_ACCESS_CLIENT_EDITOR_CACHE:
            return "EDIT";
        case SD_ACCESS_CLIENT_WAVEFORM_CACHE:
            return "WAVE";
        case SD_ACCESS_CLIENT_SAMPLE_STREAM:
            return "STREAM";
        case SD_ACCESS_CLIENT_PATCH:
            return "PATCH";
        case SD_ACCESS_CLIENT_SCHEDULED_RECORDER:
            return "SREC";
        case SD_ACCESS_CLIENT_BACKGROUND:
            return "BG";
        default:
            return "NONE";
    }
}

const char *sd_access_gate_busy_label(void)
{
    sd_access_client_t owner = sd_access_gate_current_owner();
    if (owner != SD_ACCESS_CLIENT_NONE)
    {
        return sd_access_gate_client_label(owner);
    }

    if (sd_access_gate_streaming_critical_active() != 0U)
    {
        return "STREAM";
    }

    /* last_owner is diagnostic history, never evidence of current activity. */
    return sd_access_gate_client_label(SD_ACCESS_CLIENT_NONE);
}
