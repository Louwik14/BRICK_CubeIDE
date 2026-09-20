#include "SD/sd_block_device.h"

#include <string.h>

#include "Platform/brick6_sd_config.h"
#include "SD/bsp_driver_sd.h"
#include "SD/sd_io_hooks.h"
#include "SD/sdmmc_async_transport.h"
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
    uint32_t token;
    sd_block_device_operation_t operation;
    sd_block_device_result_t result;
    sd_block_device_result_t abort_result;
    uint8_t owner_client;
    uint8_t started;
    uint8_t callback_seen;
    uint8_t completed;
    volatile uint8_t irq_complete;
    volatile uint8_t irq_error;
    uint8_t prepared;
    uint8_t chained_next;
} sd_block_device_async_entry_t;

SDRAM_STREAM_SERVICE static sd_block_device_async_entry_t
    g_sd_block_device_async_fifo[SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH];
static uint8_t g_sd_block_device_async_head;
static uint8_t g_sd_block_device_async_tail;
static uint8_t g_sd_block_device_async_count;
static uint8_t g_sd_block_device_abort_discard;
static uint8_t g_sd_block_device_fault_latched;
static volatile sd_block_device_hardware_state_t g_sd_block_device_hw_state;
static volatile uint8_t g_sd_block_device_async_rx_complete;
static volatile uint8_t g_sd_block_device_async_tx_complete;
static volatile uint8_t g_sd_block_device_async_abort_complete;
static volatile uint8_t g_sd_block_device_async_error;
static uint8_t g_sd_block_device_active_index;
static uint8_t g_sd_block_device_active_valid;
static uint8_t g_sd_block_device_prepared_index;
static uint8_t g_sd_block_device_prepared_valid;
static uint32_t g_sd_block_device_next_token;

static void sd_block_device_invalidate_prepared_with_result(
    sd_block_device_result_t result);

static void sd_block_device_queue_reset(void)
{
    (void)sdmmc_async_transport_invalidate_next();
    memset(g_sd_block_device_async_fifo, 0, sizeof(g_sd_block_device_async_fifo));
    g_sd_block_device_async_head = 0U;
    g_sd_block_device_async_tail = 0U;
    g_sd_block_device_async_count = 0U;
    g_sd_block_device_abort_discard = 0U;
    g_sd_block_device_hw_state = (g_sd_block_device_fault_latched != 0U)
        ? SD_BLOCK_DEVICE_HW_ERROR_LATCHED : SD_BLOCK_DEVICE_HW_IDLE;
    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_tx_complete = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    g_sd_block_device_async_error = 0U;
    g_sd_block_device_active_index = 0U;
    g_sd_block_device_active_valid = 0U;
    g_sd_block_device_prepared_index = 0U;
    g_sd_block_device_prepared_valid = 0U;
}

void sd_block_device_async_init(void)
{
    g_sd_block_device_fault_latched = 0U;
    sdmmc_async_transport_init();
    g_sd_block_device_next_token = 1U;
    sd_block_device_queue_reset();
}

static void sd_block_device_complete(sd_block_device_async_entry_t *entry,
                                     sd_block_device_result_t result)
{
    entry->result = result;
    entry->completed = 1U;
    if(g_sd_block_device_active_valid == 0U)
    {
        g_sd_block_device_hw_state = (g_sd_block_device_fault_latched != 0U)
            ? SD_BLOCK_DEVICE_HW_ERROR_LATCHED : SD_BLOCK_DEVICE_HW_IDLE;
    }
}

static void sd_block_device_force_quiescence(void)
{
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    (void)HAL_SD_DeInit(&hsd1);
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    brick_sd_media_fault();
    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_tx_complete = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    g_sd_block_device_async_error = 0U;
    g_sd_block_device_active_valid = 0U;
    g_sd_block_device_fault_latched = 1U;
}

static void sd_block_device_finish_abort(sd_block_device_async_entry_t *entry,
                                         sd_block_device_result_t result)
{
    const uint8_t discard = g_sd_block_device_abort_discard;
    g_sd_block_device_abort_discard = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    sd_block_device_complete(entry, result);
    if(discard != 0U)
    {
        sd_block_device_queue_reset();
    }
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
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
            && (entry->irq_complete == 0U))
    {
        return;
    }
    sd_block_device_invalidate_prepared_with_result(result);
    entry->abort_result = result;
    g_sd_block_device_abort_discard = discard;
    g_sd_block_device_async_abort_complete = 0U;
    entry->start_tick = HAL_GetTick();
    g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_ABORTING;
    if(sdmmc_async_transport_abort() == 0U)
    {
        sd_block_device_force_quiescence();
        sd_block_device_finish_abort(entry, SD_BLOCK_DEVICE_ABORT_FAILED);
        return;
    }
    g_sd_block_device_active_valid = 0U;
    sd_block_device_finish_abort(entry, result);
}

static void sd_block_device_fail_or_abort(sd_block_device_async_entry_t *entry,
                                          sd_block_device_result_t result)
{
    brick_sd_media_fault();
    if((g_sd_block_device_active_valid != 0U)
            && (&g_sd_block_device_async_fifo[
                    g_sd_block_device_active_index] != entry))
    {
        sd_block_device_request_abort(
            &g_sd_block_device_async_fifo[g_sd_block_device_active_index],
            result, 0U);
        sd_block_device_complete(entry, result);
        return;
    }
    if(entry->started != 0U)
    {
        sd_block_device_request_abort(entry, result, 0U);
    }
    else
    {
        sd_block_device_complete(entry, result);
    }
}

static uint32_t sd_block_device_allocate_token(void)
{
    uint32_t token = g_sd_block_device_next_token++;
    if(token == 0U)
    {
        token = g_sd_block_device_next_token++;
    }
    return token;
}

static void sd_block_device_invalidate_prepared_with_result(
    sd_block_device_result_t result)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(g_sd_block_device_prepared_valid == 0U)
    {
        __set_PRIMASK(primask);
        return;
    }
    sd_block_device_async_entry_t *const prepared =
        &g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
    (void)sdmmc_async_transport_invalidate_next();
    prepared->prepared = 0U;
    prepared->result = result;
    prepared->completed = 1U;
    g_sd_block_device_prepared_valid = 0U;
    __set_PRIMASK(primask);
}

void sd_block_device_async_invalidate_prepared(void)
{
    sd_block_device_invalidate_prepared_with_result(
        SD_BLOCK_DEVICE_MEDIA_CHANGED);
}

static uint8_t sd_block_device_prepare_next(uint8_t index)
{
    if((g_sd_block_device_active_valid == 0U)
            || (g_sd_block_device_prepared_valid != 0U)
            || (g_sd_block_device_hw_state != SD_BLOCK_DEVICE_HW_READ_DMA))
    {
        return 0U;
    }
    sd_block_device_async_entry_t *const active =
        &g_sd_block_device_async_fifo[g_sd_block_device_active_index];
    sd_block_device_async_entry_t *const next =
        &g_sd_block_device_async_fifo[index];
    if((active->operation != SD_BLOCK_DEVICE_OPERATION_READ)
            || (next->operation != SD_BLOCK_DEVICE_OPERATION_READ)
            || (active->owner_client != next->owner_client)
            || (active->media_epoch != next->media_epoch)
            || (sd_access_media_epoch() != next->media_epoch)
            || (sd_access_gate_current_owner()
                != (sd_access_client_t)next->owner_client))
    {
        return 0U;
    }

    dcache_invalidate_by_addr_aligned(
        next->buffer,
        (size_t)next->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
    const sdmmc_async_prepared_transfer_t transfer = {
        .lba = next->lba,
        .sector_count = next->sector_count,
        .buffer = next->buffer,
        .token = next->token,
        .owner_generation = next->owner_generation,
        .media_epoch = next->media_epoch,
        .owner = next->owner_client,
        .operation = 0U,
        .flags = 0U,
    };
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(sdmmc_async_transport_arm_next(&transfer) == 0U)
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    next->prepared = 1U;
    next->start_tick = HAL_GetTick();
    g_sd_block_device_prepared_index = index;
    g_sd_block_device_prepared_valid = 1U;
    __set_PRIMASK(primask);
    return 1U;
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
    g_sd_block_device_async_rx_complete = 0U;
    g_sd_block_device_async_tx_complete = 0U;
    g_sd_block_device_async_abort_complete = 0U;
    g_sd_block_device_async_error = 0U;
    entry->started = 1U;
    entry->start_tick = HAL_GetTick();
    g_sd_block_device_active_index = g_sd_block_device_async_head;
    g_sd_block_device_active_valid = 1U;

    uint8_t start_result;
    if(entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
    {
        dcache_invalidate_by_addr_aligned(
            entry->buffer,
            (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_READ_DMA;
        start_result = (sdmmc_async_transport_start_read(
            entry->buffer, entry->lba, entry->sector_count) != 0U)
                ? MSD_OK : MSD_ERROR;
    }
    else
    {
        dcache_clean_by_addr_aligned(
            entry->buffer,
            (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_WRITE_DMA;
        start_result = (sdmmc_async_transport_start_write(
            entry->buffer, entry->lba, entry->sector_count) != 0U)
                ? MSD_OK : MSD_ERROR;
    }
    if(start_result != MSD_OK)
    {
        entry->started = 0U;
        g_sd_block_device_active_valid = 0U;
        brick_sd_media_fault();
        sd_block_device_complete(entry, SD_BLOCK_DEVICE_DMA_START_FAIL);
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
    if(g_sd_block_device_fault_latched != 0U)
    {
        return SD_BLOCK_DEVICE_ABORT_FAILED;
    }
    return SD_BLOCK_DEVICE_OK;
}

static sd_block_device_result_t sd_block_device_async_read_submit_internal(
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation)
{
    const sd_block_device_result_t valid =
        sd_block_device_validate_submit(sector_count, dst);
    if(valid != SD_BLOCK_DEVICE_OK)
    {
        return valid;
    }
    if(g_sd_block_device_async_count >= 2U)
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
    entry->owner_generation = owner_generation;
    entry->token = sd_block_device_allocate_token();
    entry->queued_tick = HAL_GetTick();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    const uint8_t entry_index = g_sd_block_device_async_tail;
    const uint8_t queued_before = g_sd_block_device_async_count;
    g_sd_block_device_async_tail = (uint8_t)(
        (g_sd_block_device_async_tail + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count++;
    if(queued_before == 0U)
    {
        sd_block_device_async_start_head();
    }
    else if(sd_block_device_prepare_next(entry_index) == 0U)
    {
        g_sd_block_device_async_tail = entry_index;
        g_sd_block_device_async_count--;
        memset(entry, 0, sizeof(*entry));
        return SD_BLOCK_DEVICE_BUSY;
    }
    return SD_BLOCK_DEVICE_OK;
}

sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst)
{
    return sd_block_device_async_read_submit_internal(lba, sector_count, dst, 0U);
}

sd_block_device_result_t sd_block_device_async_read_submit(
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation)
{
    return sd_block_device_async_read_submit_internal(
        lba, sector_count, dst, owner_generation);
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
    entry->token = sd_block_device_allocate_token();
    entry->queued_tick = HAL_GetTick();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    g_sd_block_device_async_tail = (uint8_t)(
        (g_sd_block_device_async_tail + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count++;
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
            sd_block_device_finish_abort(entry, entry->abort_result);
        }
        else if((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
        {
            sd_block_device_force_quiescence();
            sd_block_device_finish_abort(entry, SD_BLOCK_DEVICE_ABORT_FAILED);
        }
        return;
    }
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ERROR_LATCHED)
    {
        sd_block_device_complete(entry, SD_BLOCK_DEVICE_ABORT_FAILED);
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
    if((entry->irq_error != 0U)
            || ((g_sd_block_device_async_error != 0U)
                && (g_sd_block_device_active_valid != 0U)
                && (g_sd_block_device_active_index
                    == g_sd_block_device_async_head)))
    {
        const sd_block_device_result_t failure =
            (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                ? SD_BLOCK_DEVICE_WRITE_FAIL : SD_BLOCK_DEVICE_READ_FAIL;
        sd_block_device_fail_or_abort(entry, failure);
        return;
    }
    const uint8_t dma_complete = entry->irq_complete;
    if(dma_complete != 0U)
    {
        if(entry->callback_seen == 0U)
        {
            if((entry->chained_next == 0U)
                    && (sdmmc_async_transport_release_complete() == 0U))
            {
                const sd_block_device_result_t failure =
                    (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                        ? SD_BLOCK_DEVICE_WRITE_FAIL
                        : SD_BLOCK_DEVICE_READ_FAIL;
                sd_block_device_fail_or_abort(entry, failure);
                return;
            }
            entry->callback_seen = 1U;
            if(g_sd_block_device_active_valid == 0U)
            {
                g_sd_block_device_hw_state =
                    (entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                        ? SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY
                        : SD_BLOCK_DEVICE_HW_READ_WAIT_CARD_READY;
            }
        }
        /* A successful read plus CMD12 already leaves the card transferable.
         * Writes may still program internally, so keep their CMD13 readiness
         * check in the worker rather than in the SDMMC interrupt. */
        if((entry->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
                && (BSP_SD_GetCardState() != SD_TRANSFER_OK))
        {
            if((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
            {
                sd_block_device_fail_or_abort(entry, SD_BLOCK_DEVICE_TIMEOUT);
            }
            return;
        }
        if(entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
        {
            dcache_invalidate_by_addr_aligned(
                entry->buffer,
                (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
        }
        sd_block_device_complete(entry, SD_BLOCK_DEVICE_OK);
        return;
    }
    if((HAL_GetTick() - entry->start_tick) >= BRICK6_SD_TIMEOUT_MS)
    {
        sd_block_device_fail_or_abort(entry, SD_BLOCK_DEVICE_TIMEOUT);
        return;
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
    out_completion->operation = entry->operation;
    out_completion->result = entry->result;
    out_completion->owner_client = entry->owner_client;
    memset(entry, 0, sizeof(*entry));
    g_sd_block_device_async_head = (uint8_t)(
        (g_sd_block_device_async_head + 1U) % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
    g_sd_block_device_async_count--;
    if(g_sd_block_device_active_valid == 0U)
    {
        g_sd_block_device_hw_state = (g_sd_block_device_fault_latched != 0U)
            ? SD_BLOCK_DEVICE_HW_ERROR_LATCHED : SD_BLOCK_DEVICE_HW_IDLE;
    }
    if(g_sd_block_device_async_count != 0U)
    {
        g_sd_block_device_async_fifo[g_sd_block_device_async_head].queued_tick =
            HAL_GetTick();
    }
    if(g_sd_block_device_active_valid == 0U)
    {
        sd_block_device_async_start_head();
    }
    return 1U;
}

uint32_t sd_block_device_async_pending_count(void)
{
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
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
    sd_block_device_invalidate_prepared_with_result(SD_BLOCK_DEVICE_ABORTED);
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

sd_block_device_result_t sd_block_device_async_abort_generation(
    uint32_t owner_generation)
{
    if((__get_IPSR() != 0U) || (owner_generation == 0U))
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    if(g_sd_block_device_prepared_valid != 0U)
    {
        sd_block_device_async_entry_t *const prepared =
            &g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
        if(prepared->owner_generation == owner_generation)
        {
            sd_block_device_invalidate_prepared_with_result(
                SD_BLOCK_DEVICE_ABORTED);
            if((g_sd_block_device_active_valid != 0U)
                    && (g_sd_block_device_async_fifo[
                            g_sd_block_device_active_index].owner_generation
                        == owner_generation))
            {
                sd_block_device_request_abort(
                    &g_sd_block_device_async_fifo[
                        g_sd_block_device_active_index],
                    SD_BLOCK_DEVICE_ABORTED, 0U);
            }
            return SD_BLOCK_DEVICE_OK;
        }
    }
    if(g_sd_block_device_active_valid != 0U)
    {
        sd_block_device_async_entry_t *const active =
            &g_sd_block_device_async_fifo[g_sd_block_device_active_index];
        if(active->owner_generation == owner_generation)
        {
            sd_block_device_request_abort(
                active, SD_BLOCK_DEVICE_ABORTED, 0U);
            return SD_BLOCK_DEVICE_OK;
        }
    }
    for(uint8_t offset = 0U; offset < g_sd_block_device_async_count; ++offset)
    {
        const uint8_t index = (uint8_t)(
            (g_sd_block_device_async_head + offset)
            % SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
        sd_block_device_async_entry_t *const entry =
            &g_sd_block_device_async_fifo[index];
        if(entry->owner_generation == owner_generation)
        {
            entry->result = SD_BLOCK_DEVICE_ABORTED;
            entry->completed = 1U;
            return SD_BLOCK_DEVICE_OK;
        }
    }
    return SD_BLOCK_DEVICE_INVALID_ARG;
}

void sd_block_device_async_cancel(void)
{
    if(g_sd_block_device_async_count == 0U)
    {
        return;
    }
    sd_block_device_invalidate_prepared_with_result(SD_BLOCK_DEVICE_ABORTED);
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
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_READ_DMA)
            && (g_sd_block_device_active_valid != 0U))
    {
        sd_block_device_async_entry_t *const entry =
            &g_sd_block_device_async_fifo[g_sd_block_device_active_index];
        entry->irq_complete = 1U;
        if(g_sd_block_device_prepared_valid != 0U)
        {
            uint32_t token = 0U;
            const sdmmc_async_chain_result_t chain =
                sdmmc_async_transport_chain_next(&token);
            sd_block_device_async_entry_t *const next =
                &g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
            if((chain == SDMMC_ASYNC_CHAIN_STARTED) && (token == next->token))
            {
                entry->chained_next = 1U;
                next->prepared = 0U;
                next->started = 1U;
                g_sd_block_device_active_index = g_sd_block_device_prepared_index;
                g_sd_block_device_active_valid = 1U;
                g_sd_block_device_prepared_valid = 0U;
                g_sd_block_device_async_rx_complete = 0U;
                g_sd_block_device_async_error = 0U;
                g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_READ_DMA;
                return;
            }
            next->prepared = 0U;
            next->result = SD_BLOCK_DEVICE_DMA_START_FAIL;
            next->completed = 1U;
            g_sd_block_device_prepared_valid = 0U;
        }
        g_sd_block_device_active_valid = 0U;
        g_sd_block_device_async_rx_complete = 1U;
    }
}

void sd_block_device_async_write_complete_isr(void)
{
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_WRITE_DMA)
            && (g_sd_block_device_active_valid != 0U))
    {
        sd_block_device_async_entry_t *const entry =
            &g_sd_block_device_async_fifo[g_sd_block_device_active_index];
        entry->irq_complete = 1U;
        g_sd_block_device_active_valid = 0U;
        g_sd_block_device_async_tx_complete = 1U;
    }
}

void sd_block_device_async_abort_complete_isr(void)
{
    if(g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_ABORTING)
    {
        g_sd_block_device_async_abort_complete = 1U;
    }
}

void sd_block_device_async_error_isr(void)
{
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_READ_DMA)
            || (g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_READ_WAIT_CARD_READY)
            || (g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_WRITE_DMA)
            || (g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY))
    {
        if(g_sd_block_device_active_valid != 0U)
        {
            g_sd_block_device_async_fifo[g_sd_block_device_active_index]
                .irq_error = 1U;
        }
        sd_block_device_invalidate_prepared_with_result(
            SD_BLOCK_DEVICE_READ_FAIL);
        g_sd_block_device_async_error = 1U;
    }
}
