#include "SD/sd_block_device.h"

#include <string.h>

#include "Core/brick6_sd_config.h"
#include "SD/bsp_driver_sd.h"
#include "SD/sd_io_hooks.h"
#include "Storage/cache_maintenance.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"

#include "sdmmc.h"
#include "stm32h7xx_hal.h"

#define SD_BLOCK_DEVICE_SECTOR_BYTES (512U)

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    uint8_t *dst;
    sd_block_device_result_t result;
    uint32_t start_tick;
    uint8_t started;
    uint8_t completed;
} sd_block_device_async_entry_t;

SDRAM_STREAM_SERVICE static sd_block_device_async_entry_t
    g_sd_block_device_async_fifo[SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH];
static uint8_t g_sd_block_device_async_head;
static uint8_t g_sd_block_device_async_tail;
static uint8_t g_sd_block_device_async_count;
static volatile uint8_t g_sd_block_device_async_rx_complete;
static volatile uint8_t g_sd_block_device_async_error;

void sd_block_device_async_init(void)
{
    memset(g_sd_block_device_async_fifo, 0, sizeof(g_sd_block_device_async_fifo));
    g_sd_block_device_async_head = 0U;
    g_sd_block_device_async_tail = 0U;
    g_sd_block_device_async_count = 0U;
    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_error = 0U;
}

static void sd_block_device_async_start_head(void)
{
    if (g_sd_block_device_async_count == 0U)
    {
        return;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if ((entry->started != 0U) || (entry->completed != 0U))
    {
        return;
    }
    if (BSP_SD_GetCardState() != SD_TRANSFER_OK)
    {
        return;
    }

    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_error = 0U;
    dcache_invalidate_by_addr_aligned(
        entry->dst, (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
    entry->started = 1U;
    entry->start_tick = HAL_GetTick();
    if (brick_sd_read_blocks_dma((uint32_t *)entry->dst,
                                 entry->lba,
                                 entry->sector_count) != MSD_OK)
    {
        brick_sd_media_fault();
        entry->result = SD_BLOCK_DEVICE_READ_FAIL;
        entry->completed = 1U;
    }
}

sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst)
{
    if ((dst == 0) || (sector_count == 0U)
        || (((uintptr_t)dst & 31U) != 0U))
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    if (__get_IPSR() != 0U)
    {
        return SD_BLOCK_DEVICE_ISR_CONTEXT;
    }
    if (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
    {
        return SD_BLOCK_DEVICE_GATE_NOT_HELD;
    }
    if (g_sd_block_device_async_count >= SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH)
    {
        return SD_BLOCK_DEVICE_QUEUE_FULL;
    }

    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_tail];
    memset(entry, 0, sizeof(*entry));
    entry->lba = lba;
    entry->sector_count = sector_count;
    entry->dst = (uint8_t *)dst;
    entry->result = SD_BLOCK_DEVICE_BUSY;
    entry->start_tick = HAL_GetTick();
    g_sd_block_device_async_tail = (uint8_t)(
        (g_sd_block_device_async_tail + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count++;
    sd_block_device_async_start_head();
    return SD_BLOCK_DEVICE_OK;
}

void sd_block_device_async_poll(void)
{
    if (g_sd_block_device_async_count == 0U)
    {
        return;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if (entry->completed != 0U)
    {
        return;
    }
    if (entry->started == 0U)
    {
        if ((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
        {
            brick_sd_media_fault();
            entry->result = SD_BLOCK_DEVICE_READ_FAIL;
            entry->completed = 1U;
            return;
        }
        sd_block_device_async_start_head();
        return;
    }
    if (g_sd_block_device_async_error != 0U)
    {
        brick_sd_media_fault();
        entry->result = SD_BLOCK_DEVICE_READ_FAIL;
        entry->completed = 1U;
        return;
    }
    if ((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
    {
        (void)HAL_SD_Abort(&hsd1);
        brick_sd_media_fault();
        entry->result = SD_BLOCK_DEVICE_READ_FAIL;
        entry->completed = 1U;
        return;
    }
    if ((g_sd_block_device_async_rx_complete != 0U)
        && (BSP_SD_GetCardState() == SD_TRANSFER_OK))
    {
        dcache_invalidate_by_addr_aligned(
            entry->dst, (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        entry->result = SD_BLOCK_DEVICE_OK;
        entry->completed = 1U;
    }
}

uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion)
{
    if ((out_completion == 0) || (g_sd_block_device_async_count == 0U))
    {
        return 0U;
    }
    sd_block_device_async_poll();
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if (entry->completed == 0U)
    {
        return 0U;
    }
    out_completion->lba = entry->lba;
    out_completion->sector_count = entry->sector_count;
    out_completion->dst = entry->dst;
    out_completion->result = entry->result;
    memset(entry, 0, sizeof(*entry));
    g_sd_block_device_async_head = (uint8_t)(
        (g_sd_block_device_async_head + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count--;
    if (g_sd_block_device_async_count != 0U)
    {
        g_sd_block_device_async_fifo[g_sd_block_device_async_head].start_tick = HAL_GetTick();
    }
    sd_block_device_async_start_head();
    return 1U;
}

uint32_t sd_block_device_async_pending_count(void)
{
    return g_sd_block_device_async_count;
}

void sd_block_device_async_cancel(void)
{
    if (g_sd_block_device_async_count != 0U)
    {
        const sd_block_device_async_entry_t *const entry =
            &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
        if ((entry->started != 0U) && (entry->completed == 0U))
        {
            (void)HAL_SD_Abort(&hsd1);
        }
    }
    sd_block_device_async_init();
}

void sd_block_device_async_read_complete_isr(void)
{
    g_sd_block_device_async_rx_complete = 1U;
}

void sd_block_device_async_error_isr(void)
{
    g_sd_block_device_async_error = 1U;
}
