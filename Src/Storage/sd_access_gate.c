#include "Storage/sd_access_gate.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

static volatile uint8_t g_sd_access_owner;
static volatile uint8_t g_sd_access_last_owner;
static volatile uint8_t g_sd_access_total_count;
static volatile uint8_t g_sd_access_client_count[SD_ACCESS_CLIENT_MAX + 1U];
static volatile uint32_t g_sd_access_acquire_fail_count[SD_ACCESS_CLIENT_MAX + 1U];
static volatile uint8_t g_sd_access_streaming_critical;
static volatile uint8_t g_sd_access_bulk_exclusive;
static volatile uint32_t g_sd_access_owner_acquire_tick;
static volatile uint32_t g_sd_access_max_hold_ticks;
static volatile uint32_t g_sd_access_owner_acquire_cycle;
static volatile uint32_t g_sd_access_client_cycles[SD_ACCESS_CLIENT_MAX + 1U];
STORAGE_STATE_SDRAM static FATFS g_sd_fs;
static uint8_t g_sd_fs_mounted;
static volatile uint32_t g_sd_media_epoch;
static uint8_t g_sd_media_present_known;
static uint8_t g_sd_media_present;
#if BRICK_TEST_BUILD
static volatile uint8_t g_sd_access_diagnostic_read_only;
#endif

void sd_access_gate_init(void)
{
    g_sd_access_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_access_last_owner = (uint8_t)SD_ACCESS_CLIENT_NONE;
    g_sd_access_total_count = 0U;
    g_sd_access_streaming_critical = 0U;
    g_sd_access_bulk_exclusive = 0U;
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
#if BRICK_TEST_BUILD
    g_sd_access_diagnostic_read_only = 0U;
#endif
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
    if ((g_sd_access_bulk_exclusive != 0U)
        && (client != SD_ACCESS_CLIENT_MULTI_BULK))
    {
        g_sd_access_acquire_fail_count[(uint8_t)client]++;
        __enable_irq();
        return 0U;
    }
#if BRICK_TEST_BUILD
    if (g_sd_access_diagnostic_read_only != 0U)
    {
        const uint8_t allowed =
            ((client == SD_ACCESS_CLIENT_SAMPLE_BOOT)
             || (client == SD_ACCESS_CLIENT_SAMPLE_CACHE)
             || (client == SD_ACCESS_CLIENT_PREVIEW)
             || (client == SD_ACCESS_CLIENT_SAMPLE_STREAM)
             || (client == SD_ACCESS_CLIENT_MULTI_BULK)
             || (client == SD_ACCESS_CLIENT_DIAGNOSTIC_LOG))
                ? 1U
                : 0U;
        if (allowed == 0U)
        {
            g_sd_access_acquire_fail_count[(uint8_t)client]++;
            __enable_irq();
            return 0U;
        }
    }
#endif
    if ((g_sd_access_streaming_critical != 0U)
        && (g_sd_access_total_count == 0U)
        && (client != SD_ACCESS_CLIENT_SAMPLE_STREAM)
        && (client != SD_ACCESS_CLIENT_MULTI_BULK)
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
             || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_PATCH)
             || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_KIT))
                ? 1U
                : 0U;
        const uint8_t requester_is_project_pattern =
            ((client == SD_ACCESS_CLIENT_PROJECT)
             || (client == SD_ACCESS_CLIENT_PATTERN)
             || (client == SD_ACCESS_CLIENT_PATCH)
             || (client == SD_ACCESS_CLIENT_KIT))
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

uint8_t sd_access_gate_begin_bulk_exclusive(void)
{
    uint8_t ok = 0U;
    __disable_irq();
    if ((g_sd_access_bulk_exclusive != 0U)
        || (g_sd_access_total_count == 0U))
    {
        g_sd_access_bulk_exclusive = 1U;
        ok = 1U;
    }
    __enable_irq();
    return ok;
}

void sd_access_gate_end_bulk_exclusive(void)
{
    __disable_irq();
    if ((g_sd_access_total_count == 0U)
        || (g_sd_access_owner == (uint8_t)SD_ACCESS_CLIENT_MULTI_BULK))
    {
        g_sd_access_bulk_exclusive = 0U;
    }
    __enable_irq();
}

uint8_t sd_access_gate_bulk_exclusive_active(void)
{
    uint8_t active;
    __disable_irq();
    active = g_sd_access_bulk_exclusive;
    __enable_irq();
    return active;
}

#if BRICK_TEST_BUILD
void sd_access_gate_set_diagnostic_read_only(uint8_t active)
{
    __disable_irq();
    g_sd_access_diagnostic_read_only = (active != 0U) ? 1U : 0U;
    __enable_irq();
}

uint8_t sd_access_gate_diagnostic_read_only_active(void)
{
    uint8_t active;
    __disable_irq();
    active = g_sd_access_diagnostic_read_only;
    __enable_irq();
    return active;
}
#endif

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
        case SD_ACCESS_CLIENT_KIT:
            return "KIT";
        case SD_ACCESS_CLIENT_MULTI_BULK:
            return "MBULK";
        case SD_ACCESS_CLIENT_SCHEDULED_RECORDER:
            return "SREC";
#if BRICK_TEST_BUILD
        case SD_ACCESS_CLIENT_AUDIO_TEST:
            return "ATEST";
        case SD_ACCESS_CLIENT_DIAGNOSTIC_LOG:
            return "DLOG";
#endif
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
