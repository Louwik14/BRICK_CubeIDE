#include "SD/sd_block_device.h"

#include <string.h>

#include "Platform/brick6_sd_config.h"
#include "SD/bsp_driver_sd.h"
#include "SD/sd_io_hooks.h"
#include "Platform/cache_maintenance.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"

#include "sdmmc.h"
#include "stm32h7xx_hal.h"

#define SD_BLOCK_DEVICE_SECTOR_BYTES (512U)

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    uint8_t *buffer;
    uint32_t owner_generation;
    uint32_t media_epoch;
    uint32_t queued_tick;
    uint32_t start_tick;
    uint32_t callback_tick;
    uint32_t duration_ms;
    sd_block_device_operation_t operation;
    sd_block_device_result_t result;
    sd_block_device_result_t abort_result;
    uint8_t owner_client;
    uint8_t started;
    uint8_t callback_seen;
    uint8_t completed;
} sd_block_device_async_entry_t;

SDRAM_STREAM_SERVICE static sd_block_device_async_entry_t
    g_sd_block_device_async_fifo[SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH];
static uint8_t g_sd_block_device_async_head;
static uint8_t g_sd_block_device_async_tail;
static uint8_t g_sd_block_device_async_count;
static uint8_t g_sd_block_device_abort_discard;
static sd_block_device_operation_t g_sd_block_device_last_hw_operation;
static volatile sd_block_device_hardware_state_t g_sd_block_device_hw_state;
static volatile uint8_t g_sd_block_device_async_rx_complete;
static volatile uint8_t g_sd_block_device_async_tx_complete;
static volatile uint8_t g_sd_block_device_async_abort_complete;
static volatile uint8_t g_sd_block_device_async_error;
static volatile uint32_t g_sd_block_device_async_callback_tick;
static sd_block_device_async_metrics_t g_sd_block_device_metrics;

static void sd_block_device_queue_reset(void)
{
    memset(g_sd_block_device_async_fifo, 0, sizeof(g_sd_block_device_async_fifo));
    g_sd_block_device_async_head = 0U;
    g_sd_block_device_async_tail = 0U;
    g_sd_block_device_async_count = 0U;
    g_sd_block_device_abort_discard = 0U;
    g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_IDLE;
    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_tx_complete = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    g_sd_block_device_async_error = 0U;
    g_sd_block_device_async_callback_tick = 0U;
}

void sd_block_device_async_metrics_reset(void)
{
    memset(&g_sd_block_device_metrics, 0, sizeof(g_sd_block_device_metrics));
}

void sd_block_device_async_metrics_get(sd_block_device_async_metrics_t *out_metrics)
{
    if(out_metrics != 0)
    {
        *out_metrics = g_sd_block_device_metrics;
    }
}

void sd_block_device_async_init(void)
{
    sd_block_device_queue_reset();
    sd_block_device_async_metrics_reset();
    g_sd_block_device_last_hw_operation = SD_BLOCK_DEVICE_OPERATION_NONE;
}

static void sd_block_device_note_direction(sd_block_device_operation_t operation)
{
    if((g_sd_block_device_last_hw_operation == SD_BLOCK_DEVICE_OPERATION_READ)
            && (operation == SD_BLOCK_DEVICE_OPERATION_WRITE))
    {
        g_sd_block_device_metrics.read_to_write_switches++;
    }
    else if((g_sd_block_device_last_hw_operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
            && (operation == SD_BLOCK_DEVICE_OPERATION_READ))
    {
        g_sd_block_device_metrics.write_to_read_switches++;
    }
    g_sd_block_device_last_hw_operation = operation;
}

static void sd_block_device_complete(sd_block_device_async_entry_t *entry,
                                     sd_block_device_result_t result)
{
    const uint32_t now = HAL_GetTick();
    const uint32_t origin = (entry->started != 0U)
        ? entry->start_tick : entry->queued_tick;
    const uint32_t duration = now - origin;
    entry->result = result;
    entry->duration_ms = duration;
    entry->completed = 1U;
    if(duration > g_sd_block_device_metrics.max_transaction_duration_ms)
    {
        g_sd_block_device_metrics.max_transaction_duration_ms = duration;
    }
    if((entry->callback_seen != 0U) && (result == SD_BLOCK_DEVICE_OK))
    {
        const uint32_t ready_latency = now - entry->callback_tick;
        if(ready_latency > g_sd_block_device_metrics.max_card_ready_latency_ms)
        {
            g_sd_block_device_metrics.max_card_ready_latency_ms = ready_latency;
        }
    }
    if(entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
    {
        if(result == SD_BLOCK_DEVICE_OK)
        {
            g_sd_block_device_metrics.write_completed++;
        }
        else
        {
            g_sd_block_device_metrics.write_failed++;
        }
    }
    g_sd_block_device_hw_state = (result == SD_BLOCK_DEVICE_OK)
        ? SD_BLOCK_DEVICE_HW_IDLE : SD_BLOCK_DEVICE_HW_ERROR_LATCHED;
}

static uint8_t sd_block_device_media_valid(
    const sd_block_device_async_entry_t *entry,
    sd_block_device_result_t *failure)
{
    if(brick_sd_is_detected() != SD_PRESENT)
    {
        *failure = SD_BLOCK_DEVICE_CARD_REMOVED;
        return 0U;
    }
    if(sd_access_media_epoch() != entry->media_epoch)
    {
        *failure = SD_BLOCK_DEVICE_MEDIA_CHANGED;
        return 0U;
    }
    return 1U;
}

static void sd_block_device_request_abort(sd_block_device_async_entry_t *entry,
                                          sd_block_device_result_t result,
                                          uint8_t discard)
{
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
    {
        return;
    }
    entry->abort_result = result;
    g_sd_block_device_abort_discard = discard;
    g_sd_block_device_metrics.abort_count++;
    g_sd_block_device_async_abort_complete = 0U;
    if(HAL_SD_Abort_IT(&hsd1) == HAL_OK)
    {
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_ABORTING;
    }
    else
    {
        entry->result = SD_BLOCK_DEVICE_ABORT_FAILED;
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_ERROR_LATCHED;
    }
}

static void sd_block_device_fail_or_abort(sd_block_device_async_entry_t *entry,
                                          sd_block_device_result_t result)
{
    brick_sd_media_fault();
    if(entry->started != 0U)
    {
        sd_block_device_request_abort(entry, result, 0U);
    }
    else
    {
        sd_block_device_complete(entry, result);
    }
}

static void sd_block_device_async_start_head(void)
{
    sd_block_device_result_t media_failure;
    if((g_sd_block_device_async_count == 0U)
            || (g_sd_block_device_hw_state != SD_BLOCK_DEVICE_HW_IDLE))
    {
        return;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if((entry->started != 0U) || (entry->completed != 0U))
    {
        return;
    }
    if(sd_block_device_media_valid(entry, &media_failure) == 0U)
    {
        sd_block_device_fail_or_abort(entry, media_failure);
        return;
    }
    if(BSP_SD_GetCardState() != SD_TRANSFER_OK)
    {
        return;
    }

    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_tx_complete = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    g_sd_block_device_async_error = 0U;
    g_sd_block_device_async_callback_tick = 0U;
    entry->started = 1U;
    entry->start_tick = HAL_GetTick();

    uint8_t start_result;
    if(entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
    {
        dcache_invalidate_by_addr_aligned(
            entry->buffer,
            (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_READ_DMA;
        start_result = brick_sd_read_blocks_dma((uint32_t *)entry->buffer,
                                                entry->lba,
                                                entry->sector_count);
    }
    else
    {
        dcache_clean_by_addr_aligned(
            entry->buffer,
            (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_WRITE_DMA;
        start_result = brick_sd_write_blocks_dma((const uint32_t *)entry->buffer,
                                                 entry->lba,
                                                 entry->sector_count);
    }
    if(start_result != MSD_OK)
    {
        entry->started = 0U;
        brick_sd_media_fault();
        sd_block_device_complete(entry, SD_BLOCK_DEVICE_DMA_START_FAIL);
    }
    else
    {
        sd_block_device_note_direction(entry->operation);
    }
}

static sd_block_device_result_t sd_block_device_validate_submit(
    uint32_t sector_count,
    const void *buffer)
{
    if((buffer == 0) || (sector_count == 0U)
            || (sector_count > SD_BLOCK_DEVICE_MAX_SECTORS_PER_TRANSFER)
            || (((uintptr_t)buffer & (DCACHE_LINE_SIZE_BYTES - 1U)) != 0U))
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    if(__get_IPSR() != 0U)
    {
        return SD_BLOCK_DEVICE_ISR_CONTEXT;
    }
    if(sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
    {
        return SD_BLOCK_DEVICE_GATE_NOT_HELD;
    }
    return SD_BLOCK_DEVICE_OK;
}

sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst)
{
    const sd_block_device_result_t valid =
        sd_block_device_validate_submit(sector_count, dst);
    if(valid != SD_BLOCK_DEVICE_OK)
    {
        return valid;
    }
    if(g_sd_block_device_async_count >= SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH)
    {
        return SD_BLOCK_DEVICE_QUEUE_FULL;
    }

    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_tail];
    memset(entry, 0, sizeof(*entry));
    entry->lba = lba;
    entry->sector_count = sector_count;
    entry->buffer = (uint8_t *)dst;
    entry->operation = SD_BLOCK_DEVICE_OPERATION_READ;
    entry->result = SD_BLOCK_DEVICE_BUSY;
    entry->queued_tick = HAL_GetTick();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    g_sd_block_device_async_tail = (uint8_t)(
        (g_sd_block_device_async_tail + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count++;
    sd_block_device_async_start_head();
    return SD_BLOCK_DEVICE_OK;
}

sd_block_device_result_t sd_block_device_async_write_submit(
    uint32_t lba,
    uint32_t sector_count,
    const void *src,
    uint32_t owner_generation)
{
    const sd_block_device_result_t valid =
        sd_block_device_validate_submit(sector_count, src);
    if(valid != SD_BLOCK_DEVICE_OK)
    {
        return valid;
    }
    if(g_sd_block_device_async_count != 0U)
    {
        g_sd_block_device_metrics.busy_rejects++;
        return SD_BLOCK_DEVICE_BUSY;
    }

    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_tail];
    memset(entry, 0, sizeof(*entry));
    entry->lba = lba;
    entry->sector_count = sector_count;
    entry->buffer = (uint8_t *)(uintptr_t)src;
    entry->operation = SD_BLOCK_DEVICE_OPERATION_WRITE;
    entry->result = SD_BLOCK_DEVICE_BUSY;
    entry->owner_generation = owner_generation;
    entry->queued_tick = HAL_GetTick();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    g_sd_block_device_async_tail = (uint8_t)(
        (g_sd_block_device_async_tail + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count++;
    g_sd_block_device_metrics.write_submitted++;
    sd_block_device_async_start_head();
    return SD_BLOCK_DEVICE_OK;
}

void sd_block_device_async_poll(void)
{
    sd_block_device_result_t media_failure;
    if(g_sd_block_device_async_count == 0U)
    {
        return;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if(entry->completed != 0U)
    {
        return;
    }
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
    {
        if(g_sd_block_device_async_abort_complete != 0U)
        {
            if(g_sd_block_device_abort_discard != 0U)
            {
                sd_block_device_queue_reset();
            }
            else
            {
                sd_block_device_complete(entry, entry->abort_result);
            }
        }
        return;
    }
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ERROR_LATCHED)
    {
        return;
    }
    if(sd_block_device_media_valid(entry, &media_failure) == 0U)
    {
        sd_block_device_fail_or_abort(entry, media_failure);
        return;
    }
    if(entry->started == 0U)
    {
        if((HAL_GetTick() - entry->queued_tick) >= BRICK6_SD_TIMEOUT_MS)
        {
            sd_block_device_fail_or_abort(entry, SD_BLOCK_DEVICE_TIMEOUT);
            return;
        }
        sd_block_device_async_start_head();
        return;
    }
    if(g_sd_block_device_async_error != 0U)
    {
        const sd_block_device_result_t failure =
            (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                ? SD_BLOCK_DEVICE_WRITE_FAIL : SD_BLOCK_DEVICE_READ_FAIL;
        sd_block_device_complete(entry, failure);
        brick_sd_media_fault();
        return;
    }
    if((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
    {
        sd_block_device_fail_or_abort(entry, SD_BLOCK_DEVICE_TIMEOUT);
        return;
    }

    const uint8_t dma_complete =
        (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
            ? g_sd_block_device_async_tx_complete
            : g_sd_block_device_async_rx_complete;
    if(dma_complete != 0U)
    {
        if(entry->callback_seen == 0U)
        {
            entry->callback_seen = 1U;
            entry->callback_tick = g_sd_block_device_async_callback_tick;
            g_sd_block_device_hw_state =
                (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                    ? SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY
                    : SD_BLOCK_DEVICE_HW_READ_WAIT_CARD_READY;
        }
        if(BSP_SD_GetCardState() == SD_TRANSFER_OK)
        {
            if(entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
            {
                dcache_invalidate_by_addr_aligned(
                    entry->buffer,
                    (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
            }
            sd_block_device_complete(entry, SD_BLOCK_DEVICE_OK);
        }
    }
}

uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion)
{
    if((out_completion == 0) || (g_sd_block_device_async_count == 0U))
    {
        return 0U;
    }
    sd_block_device_async_poll();
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if(entry->completed == 0U)
    {
        return 0U;
    }
    out_completion->lba = entry->lba;
    out_completion->sector_count = entry->sector_count;
    out_completion->dst = (entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
        ? entry->buffer : 0;
    out_completion->src = (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
        ? entry->buffer : 0;
    out_completion->owner_generation = entry->owner_generation;
    out_completion->media_epoch = entry->media_epoch;
    out_completion->duration_ms = entry->duration_ms;
    out_completion->operation = entry->operation;
    out_completion->result = entry->result;
    out_completion->owner_client = entry->owner_client;
    memset(entry, 0, sizeof(*entry));
    g_sd_block_device_async_head = (uint8_t)(
        (g_sd_block_device_async_head + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count--;
    g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_IDLE;
    if(g_sd_block_device_async_count != 0U)
    {
        g_sd_block_device_async_fifo[g_sd_block_device_async_head].queued_tick =
            HAL_GetTick();
    }
    sd_block_device_async_start_head();
    return 1U;
}

uint32_t sd_block_device_async_pending_count(void)
{
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
            && (g_sd_block_device_async_abort_complete != 0U))
    {
        sd_block_device_async_poll();
    }
    return g_sd_block_device_async_count;
}

uint8_t sd_block_device_async_write_buffer_locked(const void *src)
{
    if(src == 0)
    {
        return 0U;
    }
    for(uint8_t offset = 0U; offset < g_sd_block_device_async_count; ++offset)
    {
        const uint8_t index = (uint8_t)(
            (g_sd_block_device_async_head + offset) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
        const sd_block_device_async_entry_t *const entry =
            &g_sd_block_device_async_fifo[index];
        if((entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                && (entry->buffer == (const uint8_t *)src))
        {
            return 1U;
        }
    }
    return 0U;
}

sd_block_device_hardware_state_t sd_block_device_async_hardware_state(void)
{
    return g_sd_block_device_hw_state;
}

sd_block_device_result_t sd_block_device_async_abort_active(void)
{
    if(__get_IPSR() != 0U)
    {
        return SD_BLOCK_DEVICE_ISR_CONTEXT;
    }
    if(g_sd_block_device_async_count == 0U)
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if((entry->completed != 0U)
            || (g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING))
    {
        return SD_BLOCK_DEVICE_BUSY;
    }
    if(entry->started == 0U)
    {
        sd_block_device_complete(entry, SD_BLOCK_DEVICE_ABORTED);
    }
    else
    {
        sd_block_device_request_abort(entry, SD_BLOCK_DEVICE_ABORTED, 0U);
    }
    return SD_BLOCK_DEVICE_OK;
}

void sd_block_device_async_cancel(void)
{
    if(g_sd_block_device_async_count == 0U)
    {
        return;
    }
    sd_block_device_async_entry_t *const entry =
        &g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if((entry->started != 0U) && (entry->completed == 0U))
    {
        sd_block_device_request_abort(entry, SD_BLOCK_DEVICE_ABORTED, 1U);
        return;
    }
    sd_block_device_queue_reset();
}

void sd_block_device_async_read_complete_isr(void)
{
    g_sd_block_device_async_callback_tick = HAL_GetTick();
    g_sd_block_device_async_rx_complete = 1U;
}

void sd_block_device_async_write_complete_isr(void)
{
    g_sd_block_device_async_callback_tick = HAL_GetTick();
    g_sd_block_device_async_tx_complete = 1U;
}

void sd_block_device_async_abort_complete_isr(void)
{
    g_sd_block_device_async_callback_tick = HAL_GetTick();
    g_sd_block_device_async_abort_complete = 1U;
}

void sd_block_device_async_error_isr(void)
{
    g_sd_block_device_async_callback_tick = HAL_GetTick();
    g_sd_block_device_async_error = 1U;
}
