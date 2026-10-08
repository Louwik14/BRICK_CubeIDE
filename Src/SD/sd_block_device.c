#include "SD/sd_block_device.h"

#include <string.h>

#include "Platform/brick6_sd_config.h"
#include "SD/bsp_driver_sd.h"
#include "SD/sd_io_hooks.h"
#include "SD/sdmmc_async_transport.h"
#include "Platform/cache_maintenance.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/rec_sd_trace.h"
#include "Platform/stream_rec_perf.h"

#include "sdmmc.h"
#include "stm32h7xx_hal.h"

#define SD_BLOCK_DEVICE_SECTOR_BYTES (512U)
_Static_assert(sizeof(sd_block_device_async_request_t) <= 128U,
               "block-device async request budget exceeded");
SDRAM_STREAM_SERVICE static sd_block_device_async_request_t
    *g_sd_block_device_async_fifo[SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH];
SDRAM_STREAM_SERVICE static sd_block_device_async_request_t
    g_sd_block_device_legacy_request;
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
static uint32_t g_trace_write_submit_reject;
static uint8_t g_sd_block_device_initialized;
static sd_block_device_result_t g_sd_block_device_last_result;

static uint32_t sd_block_device_timestamp_cycles(void)
{
    return DWT->CYCCNT;
}

void sd_block_device_debug_snapshot(sd_block_device_debug_snapshot_t *out)
{
    if (out == 0) return;
    out->pending = g_sd_block_device_async_count;
    out->operation = 0U;
    out->owner_client = 0U;
    out->result = (uint32_t)g_sd_block_device_last_result;
    out->progressive_chunks_invalidated = 0U;
    if (g_sd_block_device_async_count != 0U)
    {
        const sd_block_device_async_request_t *const entry =
            g_sd_block_device_async_fifo[g_sd_block_device_async_head];
        out->operation = (uint8_t)entry->operation;
        out->owner_client = entry->owner_client;
        out->result = (uint32_t)entry->result;
        out->progressive_chunks_invalidated =
            entry->progressive_chunks_invalidated;
    }
    out->fault_latched = g_sd_block_device_fault_latched;
    out->irq_error = g_sd_block_device_async_error;
    out->hardware_state = (uint8_t)g_sd_block_device_hw_state;
    out->reserved = 0U;
}

static void sd_block_device_invalidate_prepared_with_result(
    sd_block_device_result_t result);

static void sd_block_device_queue_reset(void)
{
    (void)sdmmc_async_transport_invalidate_next();
    for(uint8_t i = 0U;
        (g_sd_block_device_initialized != 0U)
            && (i < SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH);
        ++i)
    {
        sd_block_device_async_request_t *const request =
            g_sd_block_device_async_fifo[i];
        if(request != 0)
        {
            request->result = SD_BLOCK_DEVICE_ABORTED;
            request->completed = 1U;
            request->queued = 0U;
        }
    }
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
    if(g_sd_block_device_initialized == 0U)
    {
        memset(g_sd_block_device_async_fifo, 0,
               sizeof(g_sd_block_device_async_fifo));
        memset(&g_sd_block_device_legacy_request, 0,
               sizeof(g_sd_block_device_legacy_request));
        g_sd_block_device_initialized = 1U;
    }
    g_sd_block_device_fault_latched = 0U;
    g_trace_write_submit_reject = UINT32_MAX;
    sdmmc_async_transport_init();
    g_sd_block_device_next_token = 1U;
    g_sd_block_device_last_result = SD_BLOCK_DEVICE_OK;
    sd_block_device_queue_reset();
}

static void sd_block_device_complete(sd_block_device_async_request_t *entry,
                                     sd_block_device_result_t result)
{
    g_sd_block_device_last_result = result;
    if ((result == SD_BLOCK_DEVICE_OK) && (entry->started != 0U)
        && (entry->perf_dma_cycles != 0U))
    {
        brick_perf_wall((entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
            ? PERF_WALL_STREAM_DMA : PERF_WALL_REC_WRITE,
            ((entry->perf_complete_cycles != 0U)
                ? entry->perf_complete_cycles : sd_block_device_timestamp_cycles())
                - entry->perf_dma_cycles);
    }
    if (result != SD_BLOCK_DEVICE_OK)
        rec_sd_trace_note_sd(REC_SD_TRACE_SD_IO, entry->lba,
            (rec_sd_trace_sd_meta_t){
                .requester = entry->owner_client,
                .operation = REC_SD_OP_DMA,
                .admission = REC_SD_ADMISSION_ERROR,
                .block_result = (uint8_t)result,
                .fatfs_result = 0xFFU });
    entry->result = result;
    entry->perf_publish_cycles = sd_block_device_timestamp_cycles();
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

static void sd_block_device_finish_abort(sd_block_device_async_request_t *entry,
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
    const sd_block_device_async_request_t *entry,
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

static void sd_block_device_request_abort(sd_block_device_async_request_t *entry,
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

static void sd_block_device_fail_or_abort(sd_block_device_async_request_t *entry,
                                          sd_block_device_result_t result)
{
    brick_sd_media_fault();
    if((g_sd_block_device_active_valid != 0U)
            && (g_sd_block_device_async_fifo[
                    g_sd_block_device_active_index] != entry))
    {
        sd_block_device_request_abort(
            g_sd_block_device_async_fifo[g_sd_block_device_active_index],
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
    sd_block_device_async_request_t *const prepared =
        g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
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
    sd_block_device_async_request_t *const active =
        g_sd_block_device_async_fifo[g_sd_block_device_active_index];
    sd_block_device_async_request_t *const next =
        g_sd_block_device_async_fifo[index];
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

    /* Stream page ownership may prove that no dirty CPU line exists. */
    if (next->read_destination_cpu_clean == 0U)
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
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_async_head];
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

    entry->perf_launch_enter_cycles = sd_block_device_timestamp_cycles();
    PERF_START(dma_launch_start);
    uint8_t start_result;
    if(entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
    {
        /* Generic clients retain the conservative pre-DMA invalidate. */
        if (entry->read_destination_cpu_clean == 0U)
        {
            entry->perf_pre_cache_start_cycles =
                sd_block_device_timestamp_cycles();
            dcache_invalidate_by_addr_aligned(
                entry->buffer,
                (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
            entry->perf_pre_cache_end_cycles =
                sd_block_device_timestamp_cycles();
        }
        g_sd_block_device_hw_state = SD_BLOCK_DEVICE_HW_READ_DMA;
        start_result = (((entry->progress_isr != NULL)
            ? sdmmc_async_transport_start_read_progressive(
                entry->buffer, entry->lba, entry->sector_count)
            : sdmmc_async_transport_start_read(
                entry->buffer, entry->lba, entry->sector_count)) != 0U)
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
    PERF_END((entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
        ? PERF_CPU_STREAM_DMA_LAUNCH : PERF_CPU_REC_DMA_LAUNCH,
        dma_launch_start);
    if (start_result == MSD_OK)
    {
        entry->perf_command_cycles =
            sdmmc_async_transport_command_cycles();
        entry->perf_dma_cycles = sd_block_device_timestamp_cycles();
        brick_perf_wall((entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
            ? PERF_WALL_STREAM_SUBMIT_DMA : PERF_WALL_REC_SUBMIT_DMA,
            entry->perf_dma_cycles - entry->perf_submit_cycles);
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
    sd_block_device_async_request_t *request,
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation,
    uint8_t destination_cpu_clean,
    sd_block_device_read_progress_isr_t progress_isr,
    void *progress_context)
{
    const uint32_t submit_enter_cycles = sd_block_device_timestamp_cycles();
    if((request == 0) || (request->queued != 0U))
    {
        return SD_BLOCK_DEVICE_BUSY;
    }
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

    sd_block_device_async_request_t *const entry = request;
    memset(entry, 0, sizeof(*entry));
    entry->perf_submit_enter_cycles = submit_enter_cycles;
    entry->lba = lba;
    entry->sector_count = sector_count;
    entry->buffer = (uint8_t *)dst;
    entry->operation = SD_BLOCK_DEVICE_OPERATION_READ;
    entry->result = SD_BLOCK_DEVICE_BUSY;
    entry->owner_generation = owner_generation;
    entry->read_destination_cpu_clean = destination_cpu_clean;
    entry->progress_isr = progress_isr;
    entry->progress_context = progress_context;
    entry->token = sd_block_device_allocate_token();
    entry->queued_tick = HAL_GetTick();
    entry->perf_submit_cycles = sd_block_device_timestamp_cycles();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    entry->queued = 1U;
    const uint8_t entry_index = g_sd_block_device_async_tail;
    g_sd_block_device_async_fifo[entry_index] = entry;
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
        g_sd_block_device_async_fifo[entry_index] = 0;
        memset(entry, 0, sizeof(*entry));
        return SD_BLOCK_DEVICE_BUSY;
    }
    return SD_BLOCK_DEVICE_OK;
}

sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst)
{
    return sd_block_device_async_read_submit_internal(
        &g_sd_block_device_legacy_request, lba, sector_count, dst, 0U, 0U,
        NULL, NULL);
}

sd_block_device_result_t sd_block_device_async_read_submit(
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation)
{
    return sd_block_device_async_read_submit_internal(
        &g_sd_block_device_legacy_request, lba, sector_count, dst,
        owner_generation, 0U, NULL, NULL);
}

sd_block_device_result_t sd_block_device_async_read_submit_cpu_clean(
    uint32_t lba, uint32_t sector_count, void *dst,
    uint32_t owner_generation)
{
    return sd_block_device_async_read_submit_internal(
        &g_sd_block_device_legacy_request, lba, sector_count, dst,
        owner_generation, 1U, NULL, NULL);
}

sd_block_device_result_t sd_block_device_async_read_submit_request(
    sd_block_device_async_request_t *request, uint32_t lba,
    uint32_t sector_count, void *dst, uint32_t owner_generation,
    uint8_t destination_cpu_clean)
{
    return sd_block_device_async_read_submit_internal(
        request, lba, sector_count, dst, owner_generation,
        destination_cpu_clean, NULL, NULL);
}

sd_block_device_result_t sd_block_device_async_read_submit_progressive_request(
    sd_block_device_async_request_t *request, uint32_t lba,
    uint32_t sector_count, void *dst, uint32_t owner_generation,
    uint8_t destination_cpu_clean,
    sd_block_device_read_progress_isr_t progress_isr,
    void *progress_context)
{
    if(progress_isr == NULL)
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    return sd_block_device_async_read_submit_internal(
        request, lba, sector_count, dst, owner_generation,
        destination_cpu_clean, progress_isr, progress_context);
}

sd_block_device_result_t sd_block_device_async_write_submit(
    uint32_t lba,
    uint32_t sector_count,
    const void *src,
    uint32_t owner_generation)
{
    const uint32_t submit_enter_cycles = sd_block_device_timestamp_cycles();
    const sd_block_device_result_t valid =
        sd_block_device_validate_submit(sector_count, src);
    if(valid != SD_BLOCK_DEVICE_OK)
    {
        if (valid != SD_BLOCK_DEVICE_BUSY)
            rec_sd_trace_note_sd(REC_SD_TRACE_SD_IO, lba,
                (rec_sd_trace_sd_meta_t){
                    .requester = (uint8_t)sd_access_gate_current_owner(),
                    .operation = REC_SD_OP_WRITE,
                    .admission = REC_SD_ADMISSION_ERROR,
                    .block_result = (uint8_t)valid,
                    .fatfs_result = 0xFFU });
        return valid;
    }
    if(g_sd_block_device_async_count != 0U)
    {
        const uint32_t signature = (uint32_t)sd_access_gate_current_owner()
            | ((uint32_t)g_sd_block_device_hw_state << 8U)
            | ((uint32_t)g_sd_block_device_async_count << 16U);
        if (signature != g_trace_write_submit_reject)
        {
            g_trace_write_submit_reject = signature;
            rec_sd_trace_note_sd(REC_SD_TRACE_SD_IO, lba,
                (rec_sd_trace_sd_meta_t){
                    .requester = (uint8_t)sd_access_gate_current_owner(),
                    .operation = REC_SD_OP_WRITE,
                    .admission = REC_SD_ADMISSION_DEFERRED,
                    .block_result = SD_BLOCK_DEVICE_BUSY,
                    .fatfs_result = 0xFFU });
        }
        return SD_BLOCK_DEVICE_BUSY;
    }
    g_trace_write_submit_reject = UINT32_MAX;

    sd_block_device_async_request_t *const entry =
        &g_sd_block_device_legacy_request;
    if(entry->queued != 0U)
    {
        return SD_BLOCK_DEVICE_BUSY;
    }
    memset(entry, 0, sizeof(*entry));
    entry->perf_submit_enter_cycles = submit_enter_cycles;
    entry->lba = lba;
    entry->sector_count = sector_count;
    entry->buffer = (uint8_t *)(uintptr_t)src;
    entry->operation = SD_BLOCK_DEVICE_OPERATION_WRITE;
    entry->result = SD_BLOCK_DEVICE_BUSY;
    entry->owner_generation = owner_generation;
    entry->token = sd_block_device_allocate_token();
    entry->queued_tick = HAL_GetTick();
    entry->perf_submit_cycles = sd_block_device_timestamp_cycles();
    entry->media_epoch = sd_access_media_epoch();
    entry->owner_client = (uint8_t)sd_access_gate_current_owner();
    entry->queued = 1U;
    g_sd_block_device_async_fifo[g_sd_block_device_async_tail] = entry;
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
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_async_head];
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
        if((entry->operation == SD_BLOCK_DEVICE_OPERATION_READ)
            && (entry->progressive_chunks_invalidated
                < SDMMC_ASYNC_PROGRESS_CHUNKS))
        {
            /* Always discard stale/speculative CPU lines before publishing
             * DMA data to its consumer, including the CPU-clean fast path. */
            entry->perf_cache_start_cycles =
                sd_block_device_timestamp_cycles();
            dcache_invalidate_by_addr_aligned(
                entry->buffer,
                (size_t)entry->sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES);
            entry->perf_cache_end_cycles =
                sd_block_device_timestamp_cycles();
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

uint8_t sd_block_device_async_take_completion_request(
    sd_block_device_async_request_t **out_request)
{
    if((out_request == 0) || (g_sd_block_device_async_count == 0U))
    {
        return 0U;
    }
    sd_block_device_async_poll();
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_async_head];
    if(entry->completed == 0U)
    {
        return 0U;
    }
    *out_request = entry;
    entry->queued = 0U;
    g_sd_block_device_async_fifo[g_sd_block_device_async_head] = 0;
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
        g_sd_block_device_async_fifo[g_sd_block_device_async_head]->queued_tick =
            HAL_GetTick();
    }
    if(g_sd_block_device_active_valid == 0U)
    {
        sd_block_device_async_start_head();
    }
    return 1U;
}

uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion)
{
    sd_block_device_async_request_t *request = 0;
    if((out_completion == 0)
            || (sd_block_device_async_take_completion_request(&request) == 0U))
    {
        return 0U;
    }
    out_completion->lba = request->lba;
    out_completion->sector_count = request->sector_count;
    out_completion->dst =
        (request->operation == SD_BLOCK_DEVICE_OPERATION_READ)
            ? request->buffer : 0;
    out_completion->src =
        (request->operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
            ? request->buffer : 0;
    out_completion->owner_generation = request->owner_generation;
    out_completion->media_epoch = request->media_epoch;
    out_completion->operation = request->operation;
    out_completion->result = request->result;
    out_completion->owner_client = request->owner_client;
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
        const sd_block_device_async_request_t *const entry =
            g_sd_block_device_async_fifo[index];
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
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_async_head];
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
        sd_block_device_async_request_t *const prepared =
            g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
        if(prepared->owner_generation == owner_generation)
        {
            sd_block_device_invalidate_prepared_with_result(
                SD_BLOCK_DEVICE_ABORTED);
            if((g_sd_block_device_active_valid != 0U)
                    && (g_sd_block_device_async_fifo[
                            g_sd_block_device_active_index]->owner_generation
                        == owner_generation))
            {
                sd_block_device_request_abort(
                    g_sd_block_device_async_fifo[
                        g_sd_block_device_active_index],
                    SD_BLOCK_DEVICE_ABORTED, 0U);
            }
            return SD_BLOCK_DEVICE_OK;
        }
    }
    if(g_sd_block_device_active_valid != 0U)
    {
        sd_block_device_async_request_t *const active =
            g_sd_block_device_async_fifo[g_sd_block_device_active_index];
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
        sd_block_device_async_request_t *const entry =
            g_sd_block_device_async_fifo[index];
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
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_async_head];
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
        sd_block_device_async_request_t *const entry =
            g_sd_block_device_async_fifo[g_sd_block_device_active_index];
        entry->irq_complete = 1U;
        entry->perf_command_cycles =
            sdmmc_async_transport_command_cycles();
        entry->perf_data_start_cycles =
            sdmmc_async_transport_data_start_cycles();
        entry->perf_data_end_cycles =
            sdmmc_async_transport_data_end_cycles();
        entry->perf_complete_cycles = sd_block_device_timestamp_cycles();
        if(g_sd_block_device_prepared_valid != 0U)
        {
            uint32_t token = 0U;
            const sdmmc_async_chain_result_t chain =
                sdmmc_async_transport_chain_next(&token);
            sd_block_device_async_request_t *const next =
                g_sd_block_device_async_fifo[g_sd_block_device_prepared_index];
            if((chain == SDMMC_ASYNC_CHAIN_STARTED) && (token == next->token))
            {
                entry->chained_next = 1U;
                next->prepared = 0U;
                next->started = 1U;
                next->perf_command_cycles =
                    sdmmc_async_transport_command_cycles();
                next->perf_dma_cycles = sd_block_device_timestamp_cycles();
                brick_perf_wall(PERF_WALL_STREAM_SUBMIT_DMA,
                                next->perf_dma_cycles - next->perf_submit_cycles);
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

void sd_block_device_async_read_chunk_isr(uint32_t chunk_index)
{
    if((chunk_index >= SDMMC_ASYNC_PROGRESS_CHUNKS)
        || (g_sd_block_device_hw_state != SD_BLOCK_DEVICE_HW_READ_DMA)
        || (g_sd_block_device_active_valid == 0U))
    {
        return;
    }
    sd_block_device_async_request_t *const entry =
        g_sd_block_device_async_fifo[g_sd_block_device_active_index];
    if((entry == NULL) || (entry->progress_isr == NULL)
        || (chunk_index != entry->progressive_chunks_invalidated))
    {
        return;
    }
    uint8_t *const chunk = entry->buffer
        + (chunk_index * SDMMC_ASYNC_PROGRESS_CHUNK_BYTES);
    dcache_invalidate_by_addr_aligned(
        chunk, SDMMC_ASYNC_PROGRESS_CHUNK_BYTES);
    __DMB();
    entry->progressive_chunks_invalidated = (uint8_t)(chunk_index + 1U);
    (void)entry->progress_isr(
        entry->progress_context,
        (chunk_index + 1U) * SDMMC_ASYNC_PROGRESS_CHUNK_BYTES,
        sd_block_device_timestamp_cycles());
}

void sd_block_device_async_write_complete_isr(void)
{
    if((g_sd_block_device_hw_state == SD_BLOCK_DEVICE_HW_WRITE_DMA)
            && (g_sd_block_device_active_valid != 0U))
    {
        sd_block_device_async_request_t *const entry =
            g_sd_block_device_async_fifo[g_sd_block_device_active_index];
        entry->irq_complete = 1U;
        entry->perf_complete_cycles = sd_block_device_timestamp_cycles();
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
                ->irq_error = 1U;
        }
        sd_block_device_invalidate_prepared_with_result(
            SD_BLOCK_DEVICE_READ_FAIL);
        g_sd_block_device_async_error = 1U;
    }
}
