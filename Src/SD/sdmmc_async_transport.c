#include "SD/sdmmc_async_transport.h"

#include <stddef.h>

#include "Platform/memory_layout.h"
#include "sdmmc.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_ll_sdmmc.h"

#define SDMMC_ASYNC_SECTOR_BYTES (512U)
#define SDMMC_ASYNC_PROGRESS_BYTES (4096U)
#define SDMMC_ASYNC_PROGRESS_SECTORS (32U)
#define SDMMC_ASYNC_PROGRESS_CHUNKS (4U)
#define SDMMC_ASYNC_REARM_DEADLINE_US (164U)
#define SDMMC_ASYNC_COMMAND_INTERRUPTS \
    (SDMMC_IT_CMDREND | SDMMC_IT_CTIMEOUT | SDMMC_IT_CCRCFAIL)
#define SDMMC_ASYNC_DATA_INTERRUPTS \
    (SDMMC_IT_DATAEND | SDMMC_IT_DTIMEOUT | SDMMC_IT_DCRCFAIL \
     | SDMMC_IT_TXUNDERR | SDMMC_IT_RXOVERR)
#define SDMMC_ASYNC_ALL_INTERRUPTS \
    (SDMMC_ASYNC_COMMAND_INTERRUPTS | SDMMC_ASYNC_DATA_INTERRUPTS \
     | SDMMC_IT_BUSYD0END | SDMMC_IT_IDMABTC)

typedef enum
{
    SDMMC_ASYNC_OPERATION_NONE = 0,
    SDMMC_ASYNC_OPERATION_READ,
    SDMMC_ASYNC_OPERATION_WRITE
} sdmmc_async_operation_t;

typedef struct
{
    volatile sdmmc_async_state_t state;
    sdmmc_async_operation_t operation;
    uint32_t sector_count;
    uint32_t command_cycles;
    uint32_t data_start_cycles;
    uint32_t data_end_cycles;
    uint8_t command;
    uint8_t multi_block;
    uint8_t progressive;
    uint8_t next_chunk;
    volatile uint8_t completed_chunk_pending;
    uint8_t completed_chunk_index;
    uint8_t *buffer;
} sdmmc_async_context_t;

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    void *buffer;
    uint32_t token;
    uint32_t owner_generation;
    uint32_t media_epoch;
    uint8_t owner;
    uint8_t operation;
    uint8_t flags;
    volatile sdmmc_async_prepared_state_t state;
} sdmmc_async_prepared_descriptor_t;

static sdmmc_async_context_t g_sdmmc_async;
static sdmmc_async_prepared_descriptor_t g_sdmmc_async_prepared;
volatile sdmmc_async_progress_diag_t g_sdmmc_async_progress_diag;

static void sdmmc_async_trace_first_progressive_irq(uint32_t status)
{
    if((g_sdmmc_async.progressive == 0U)
        || (g_sdmmc_async_progress_diag.cmd18_count != 1U))
    {
        return;
    }
    const uint32_t index = g_sdmmc_async_progress_diag.trace_count;
    if(index >= SDMMC_ASYNC_PROGRESS_TRACE_COUNT)
    {
        return;
    }
    volatile sdmmc_async_progress_trace_t *const trace =
        &g_sdmmc_async_progress_diag.trace[index];
    const uint8_t active = (uint8_t)(
        ((hsd1.Instance->IDMACTRL & SDMMC_IDMA_IDMABACT) != 0U) ? 1U : 0U);
    trace->irq_index = index;
    trace->sta = status;
    trace->mask = hsd1.Instance->MASK;
    trace->idmabase0 = hsd1.Instance->IDMABASE0;
    trace->idmabase1 = hsd1.Instance->IDMABASE1;
    trace->idmabsize = hsd1.Instance->IDMABSIZE;
    trace->dcount = hsd1.Instance->DCOUNT;
    trace->dlen = hsd1.Instance->DLEN;
    trace->progressive_chunk_index = g_sdmmc_async.next_chunk;
    trace->idmabact = active;
    trace->completed_buffer_hw = ((status & SDMMC_FLAG_IDMABTC) != 0U)
        ? (uint8_t)(active ^ 1U) : UINT8_MAX;
    trace->expected_completed_buffer =
        (uint8_t)(g_sdmmc_async.next_chunk & 1U);
    __DMB();
    g_sdmmc_async_progress_diag.trace_count = index + 1U;
}

static void sdmmc_async_disable_transport_interrupts(void)
{
    __HAL_SD_DISABLE_IT(&hsd1, SDMMC_ASYNC_ALL_INTERRUPTS);
}

static void sdmmc_async_quiesce_data_path(void)
{
    sdmmc_async_disable_transport_interrupts();
    hsd1.Instance->IDMACTRL = SDMMC_DISABLE_IDMA;
    hsd1.Instance->DLEN = 0U;
    hsd1.Instance->DCTRL = 0U;
    __SDMMC_CMDTRANS_DISABLE(hsd1.Instance);
}

static void sdmmc_async_send_command(uint8_t command, uint32_t argument)
{
    const SDMMC_CmdInitTypeDef config = {
        .Argument = argument,
        .CmdIndex = command,
        .Response = SDMMC_RESPONSE_SHORT,
        .WaitForInterrupt = SDMMC_WAIT_NO,
        .CPSM = SDMMC_CPSM_ENABLE,
    };
    g_sdmmc_async.command = command;
    if(command == SDMMC_CMD_READ_MULT_BLOCK)
    {
        ++g_sdmmc_async_progress_diag.cmd18_count;
    }
    else if(command == SDMMC_CMD_STOP_TRANSMISSION)
    {
        ++g_sdmmc_async_progress_diag.cmd12_count;
    }
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_CMD_FLAGS);
    __HAL_SD_ENABLE_IT(&hsd1, SDMMC_ASYNC_COMMAND_INTERRUPTS);
    if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_CMD_START)
    {
        /* Timestamp immediately before SDMMC_SendCommand writes ARG/CMD and
         * enables CPSM.  A fast CMDREND IRQ may preempt the caller as soon as
         * CMD is written, so a post-call timestamp is not ordered reliably. */
        g_sdmmc_async.command_cycles = DWT->CYCCNT;
    }
    (void)SDMMC_SendCommand(hsd1.Instance, &config);
}

static uint32_t sdmmc_async_response_error(uint32_t status)
{
    if((status & SDMMC_FLAG_CTIMEOUT) != 0U)
    {
        return HAL_SD_ERROR_CMD_RSP_TIMEOUT;
    }
    if((status & SDMMC_FLAG_CCRCFAIL) != 0U)
    {
        return HAL_SD_ERROR_CMD_CRC_FAIL;
    }
    if((status & SDMMC_FLAG_CMDREND) == 0U)
    {
        return HAL_SD_ERROR_GENERAL_UNKNOWN_ERR;
    }
    if(SDMMC_GetCommandResponse(hsd1.Instance) != g_sdmmc_async.command)
    {
        return HAL_SD_ERROR_CMD_CRC_FAIL;
    }
    uint32_t r1_errors = SDMMC_GetResponse(hsd1.Instance, SDMMC_RESP1)
        & SDMMC_OCR_ERRORBITS;
    /* Match the HAL's end-of-media tolerance for STOP_TRANSMISSION. */
    if(g_sdmmc_async.command == SDMMC_CMD_STOP_TRANSMISSION)
    {
        r1_errors &= ~SDMMC_OCR_ADDR_OUT_OF_RANGE;
    }
    if(r1_errors != 0U)
    {
        return HAL_SD_ERROR_GENERAL_UNKNOWN_ERR;
    }
    return HAL_SD_ERROR_NONE;
}

static uint32_t sdmmc_async_data_error(uint32_t status)
{
    uint32_t error = HAL_SD_ERROR_NONE;
    if((status & SDMMC_FLAG_DCRCFAIL) != 0U)
    {
        error |= HAL_SD_ERROR_DATA_CRC_FAIL;
    }
    if((status & SDMMC_FLAG_DTIMEOUT) != 0U)
    {
        error |= HAL_SD_ERROR_DATA_TIMEOUT;
    }
    if((status & SDMMC_FLAG_TXUNDERR) != 0U)
    {
        error |= HAL_SD_ERROR_TX_UNDERRUN;
    }
    if((status & SDMMC_FLAG_RXOVERR) != 0U)
    {
        error |= HAL_SD_ERROR_RX_OVERRUN;
    }
    if((status & SDMMC_FLAG_IDMATE) != 0U)
    {
        error |= HAL_SD_ERROR_DMA;
    }
    return error;
}

static sdmmc_async_event_t sdmmc_async_fail(uint32_t error)
{
    sdmmc_async_quiesce_data_path();
    __SDMMC_CMDSTOP_DISABLE(hsd1.Instance);
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
    hsd1.ErrorCode |= (error != HAL_SD_ERROR_NONE)
        ? error : HAL_SD_ERROR_GENERAL_UNKNOWN_ERR;
    if(g_sdmmc_async_prepared.state == SDMMC_ASYNC_PREPARED_ACTIVE)
    {
        g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_ERROR;
    }
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_ERROR;
    __DMB();
    return SDMMC_ASYNC_EVENT_ERROR;
}

static sdmmc_async_event_t sdmmc_async_complete(void)
{
    const sdmmc_async_event_t event =
        (g_sdmmc_async.operation == SDMMC_ASYNC_OPERATION_READ)
            ? SDMMC_ASYNC_EVENT_READ_COMPLETE
            : SDMMC_ASYNC_EVENT_WRITE_COMPLETE;
    sdmmc_async_quiesce_data_path();
    __SDMMC_CMDSTOP_DISABLE(hsd1.Instance);
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State = HAL_SD_STATE_READY;
    hsd1.Context = SD_CONTEXT_NONE;
    if(g_sdmmc_async_prepared.state == SDMMC_ASYNC_PREPARED_ACTIVE)
    {
        g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_DONE;
    }
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_COMPLETE;
    __DMB();
    return event;
}

static void sdmmc_async_send_stop(void)
{
    __SDMMC_CMDSTOP_ENABLE(hsd1.Instance);
    __SDMMC_CMDTRANS_DISABLE(hsd1.Instance);
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_WAIT_CMD12;
    sdmmc_async_send_command(SDMMC_CMD_STOP_TRANSMISSION, 0U);
}

static uint8_t sdmmc_async_start(sdmmc_async_operation_t operation,
                                 const void *buffer,
                                 uint32_t lba,
                                 uint32_t sector_count,
                                 uint8_t progressive_requested)
{
    if((buffer == NULL) || (sector_count == 0U)
            || (g_sdmmc_async.state != SDMMC_ASYNC_STATE_IDLE)
            || (hsd1.State != HAL_SD_STATE_READY)
            || ((uint64_t)lba + sector_count > hsd1.SdCard.LogBlockNbr))
    {
        return 0U;
    }

    uint32_t address = lba;
    if(hsd1.SdCard.CardType != CARD_SDHC_SDXC)
    {
        address *= SDMMC_ASYNC_SECTOR_BYTES;
    }

    const SDMMC_DataInitTypeDef data = {
        .DataTimeOut = SDMMC_DATATIMEOUT,
        .DataLength = sector_count * SDMMC_ASYNC_SECTOR_BYTES,
        .DataBlockSize = SDMMC_DATABLOCK_SIZE_512B,
        .TransferDir = (operation == SDMMC_ASYNC_OPERATION_READ)
            ? SDMMC_TRANSFER_DIR_TO_SDMMC : SDMMC_TRANSFER_DIR_TO_CARD,
        .TransferMode = SDMMC_TRANSFER_MODE_BLOCK,
        .DPSM = SDMMC_DPSM_DISABLE,
    };

    sdmmc_async_disable_transport_interrupts();
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
    hsd1.Instance->IDMACTRL = SDMMC_DISABLE_IDMA;
    hsd1.Instance->DCTRL = 0U;
    (void)SDMMC_ConfigData(hsd1.Instance, &data);
    __SDMMC_CMDTRANS_ENABLE(hsd1.Instance);
    hsd1.Instance->IDMABASE0 = (uint32_t)(uintptr_t)buffer;

    const uint8_t progressive = (uint8_t)(
        (progressive_requested != 0U)
        && (operation == SDMMC_ASYNC_OPERATION_READ)
        && (sector_count == SDMMC_ASYNC_PROGRESS_SECTORS)
        && ((((uintptr_t)buffer) & 31U) == 0U));
    if(progressive != 0U)
    {
        hsd1.Instance->IDMABASE1 =
            (uint32_t)(uintptr_t)((const uint8_t *)buffer
                                  + SDMMC_ASYNC_PROGRESS_BYTES);
        hsd1.Instance->IDMABSIZE = SDMMC_ASYNC_PROGRESS_BYTES;
        hsd1.Instance->IDMACTRL = SDMMC_ENABLE_IDMA_DOUBLE_BUFF0;
        g_sdmmc_async_progress_diag.configured_idmabndt =
            (hsd1.Instance->IDMABSIZE & SDMMC_IDMABSIZE_IDMABNDT_Msk)
            >> SDMMC_IDMABSIZE_IDMABNDT_Pos;
        g_sdmmc_async_progress_diag.configured_chunk_bytes =
            g_sdmmc_async_progress_diag.configured_idmabndt * 32U;
        g_sdmmc_async_progress_diag.expected_idma_buffers =
            (g_sdmmc_async_progress_diag.configured_chunk_bytes != 0U)
                ? (sector_count * SDMMC_ASYNC_SECTOR_BYTES)
                    / g_sdmmc_async_progress_diag.configured_chunk_bytes
                : 0U;
    }
    else
    {
        hsd1.Instance->IDMABSIZE = 0U;
        hsd1.Instance->IDMACTRL = SDMMC_ENABLE_IDMA_SINGLE_BUFF;
    }

    g_sdmmc_async.operation = operation;
    g_sdmmc_async.sector_count = sector_count;
    g_sdmmc_async.multi_block = (sector_count > 1U) ? 1U : 0U;
    g_sdmmc_async.progressive = progressive;
    g_sdmmc_async.next_chunk = 0U;
    g_sdmmc_async.completed_chunk_pending = 0U;
    g_sdmmc_async.buffer = (uint8_t *)(uintptr_t)buffer;
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_CMD_START;
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State = HAL_SD_STATE_BUSY;
    if(operation == SDMMC_ASYNC_OPERATION_READ)
    {
        hsd1.pRxBuffPtr = (uint8_t *)(uintptr_t)buffer;
        hsd1.RxXferSize = sector_count * SDMMC_ASYNC_SECTOR_BYTES;
        hsd1.Context = ((sector_count > 1U)
            ? SD_CONTEXT_READ_MULTIPLE_BLOCK : SD_CONTEXT_READ_SINGLE_BLOCK)
            | SD_CONTEXT_DMA;
        sdmmc_async_send_command((sector_count > 1U)
            ? SDMMC_CMD_READ_MULT_BLOCK : SDMMC_CMD_READ_SINGLE_BLOCK,
            address);
    }
    else
    {
        hsd1.pTxBuffPtr = (const uint8_t *)(uintptr_t)buffer;
        hsd1.TxXferSize = sector_count * SDMMC_ASYNC_SECTOR_BYTES;
        hsd1.Context = ((sector_count > 1U)
            ? SD_CONTEXT_WRITE_MULTIPLE_BLOCK : SD_CONTEXT_WRITE_SINGLE_BLOCK)
            | SD_CONTEXT_DMA;
        sdmmc_async_send_command((sector_count > 1U)
            ? SDMMC_CMD_WRITE_MULT_BLOCK : SDMMC_CMD_WRITE_SINGLE_BLOCK,
            address);
    }
    return 1U;
}

void sdmmc_async_transport_init(void)
{
    sdmmc_async_disable_transport_interrupts();
    g_sdmmc_async = (sdmmc_async_context_t){0};
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_IDLE;
    g_sdmmc_async_prepared = (sdmmc_async_prepared_descriptor_t){0};
    g_sdmmc_async_progress_diag = (sdmmc_async_progress_diag_t){0};
}

uint8_t sdmmc_async_transport_start_read(void *dst,
                                         uint32_t lba,
                                         uint32_t sector_count)
{
    return sdmmc_async_start(SDMMC_ASYNC_OPERATION_READ,
                             dst, lba, sector_count, 0U);
}

uint8_t sdmmc_async_transport_start_read_progressive(void *dst,
                                                     uint32_t lba,
                                                     uint32_t sector_count)
{
    return sdmmc_async_start(SDMMC_ASYNC_OPERATION_READ,
                             dst, lba, sector_count, 1U);
}

uint8_t sdmmc_async_transport_start_write(const void *src,
                                          uint32_t lba,
                                          uint32_t sector_count)
{
    return sdmmc_async_start(SDMMC_ASYNC_OPERATION_WRITE,
                             src, lba, sector_count, 0U);
}

uint8_t sdmmc_async_transport_arm_next(
    const sdmmc_async_prepared_transfer_t *transfer)
{
    if((transfer == NULL) || (transfer->buffer == NULL)
            || (transfer->sector_count == 0U)
            || (transfer->token == 0U)
            || (g_sdmmc_async.state == SDMMC_ASYNC_STATE_IDLE)
            || (g_sdmmc_async.state == SDMMC_ASYNC_STATE_COMPLETE)
            || (g_sdmmc_async.state == SDMMC_ASYNC_STATE_ERROR)
            || (g_sdmmc_async_prepared.state != SDMMC_ASYNC_PREPARED_EMPTY))
    {
        return 0U;
    }
    g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_WRITING;
    g_sdmmc_async_prepared.lba = transfer->lba;
    g_sdmmc_async_prepared.sector_count = transfer->sector_count;
    g_sdmmc_async_prepared.buffer = transfer->buffer;
    g_sdmmc_async_prepared.token = transfer->token;
    g_sdmmc_async_prepared.owner_generation = transfer->owner_generation;
    g_sdmmc_async_prepared.media_epoch = transfer->media_epoch;
    g_sdmmc_async_prepared.owner = transfer->owner;
    g_sdmmc_async_prepared.operation = transfer->operation;
    g_sdmmc_async_prepared.flags = transfer->flags;
    __DMB();
    g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_READY;
    return 1U;
}

sdmmc_async_chain_result_t sdmmc_async_transport_chain_next(
    uint32_t *out_token)
{
    if((out_token == NULL)
            || (g_sdmmc_async.state != SDMMC_ASYNC_STATE_COMPLETE)
            || (g_sdmmc_async_prepared.state != SDMMC_ASYNC_PREPARED_READY))
    {
        return SDMMC_ASYNC_CHAIN_NONE;
    }
    __DMB();
    const uint32_t token = g_sdmmc_async_prepared.token;
    g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_ACTIVE;
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_IDLE;
    const sdmmc_async_operation_t operation =
        (g_sdmmc_async_prepared.operation == 0U)
            ? SDMMC_ASYNC_OPERATION_READ : SDMMC_ASYNC_OPERATION_WRITE;
    if(sdmmc_async_start(operation,
                         g_sdmmc_async_prepared.buffer,
                         g_sdmmc_async_prepared.lba,
                         g_sdmmc_async_prepared.sector_count, 0U) == 0U)
    {
        g_sdmmc_async_prepared.state = SDMMC_ASYNC_PREPARED_ERROR;
        hsd1.ErrorCode |= HAL_SD_ERROR_GENERAL_UNKNOWN_ERR;
        g_sdmmc_async.state = SDMMC_ASYNC_STATE_ERROR;
        *out_token = token;
        return SDMMC_ASYNC_CHAIN_ERROR;
    }
    *out_token = token;
    return SDMMC_ASYNC_CHAIN_STARTED;
}

uint8_t sdmmc_async_transport_invalidate_next(void)
{
    if((g_sdmmc_async_prepared.state != SDMMC_ASYNC_PREPARED_READY)
            && (g_sdmmc_async_prepared.state != SDMMC_ASYNC_PREPARED_WRITING))
    {
        return 0U;
    }
    g_sdmmc_async_prepared = (sdmmc_async_prepared_descriptor_t){0};
    __DMB();
    return 1U;
}

sdmmc_async_prepared_state_t sdmmc_async_transport_prepared_state(void)
{
    return g_sdmmc_async_prepared.state;
}

uint8_t sdmmc_async_transport_irq_owned(void)
{
    return (g_sdmmc_async.state != SDMMC_ASYNC_STATE_IDLE) ? 1U : 0U;
}

sdmmc_async_event_t sdmmc_async_transport_irq_handler(void)
{
    const uint32_t status = hsd1.Instance->STA;
    sdmmc_async_trace_first_progressive_irq(status);
    sdmmc_async_event_t event = SDMMC_ASYNC_EVENT_NONE;

    const uint32_t data_error = sdmmc_async_data_error(status);
    if(data_error != HAL_SD_ERROR_NONE)
    {
        event = sdmmc_async_fail(data_error);
    }
    else if((status & (SDMMC_FLAG_CTIMEOUT | SDMMC_FLAG_CCRCFAIL)) != 0U)
    {
        event = sdmmc_async_fail(sdmmc_async_response_error(status));
    }
    else if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_CMD_START)
    {
        if((status & SDMMC_FLAG_CMDREND) != 0U)
        {
            g_sdmmc_async.data_start_cycles = DWT->CYCCNT;
            const uint32_t error = sdmmc_async_response_error(status);
            __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_CMD_FLAGS);
            __HAL_SD_DISABLE_IT(&hsd1, SDMMC_ASYNC_COMMAND_INTERRUPTS);
            if(error != HAL_SD_ERROR_NONE)
            {
                event = sdmmc_async_fail(error);
            }
            else
            {
                g_sdmmc_async.state = SDMMC_ASYNC_STATE_DATA_ACTIVE;
                __HAL_SD_ENABLE_IT(&hsd1, SDMMC_ASYNC_DATA_INTERRUPTS
                    | ((g_sdmmc_async.progressive != 0U)
                        ? SDMMC_IT_IDMABTC : 0U));
            }
        }
    }
    else if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_DATA_ACTIVE)
    {
        if((status & SDMMC_FLAG_DATAEND) != 0U)
        {
            ++g_sdmmc_async_progress_diag.dataend_count;
            if((g_sdmmc_async.progressive != 0U)
                && (g_sdmmc_async.next_chunk
                    == (SDMMC_ASYNC_PROGRESS_CHUNKS - 1U)))
            {
                if((status & SDMMC_FLAG_IDMABTC) != 0U)
                {
                    ++g_sdmmc_async_progress_diag.idmabtc_count;
                    ++g_sdmmc_async_progress_diag.final_chunk_idmabtc_count;
                }
                g_sdmmc_async.completed_chunk_index =
                    g_sdmmc_async.next_chunk++;
                g_sdmmc_async.completed_chunk_pending = 1U;
                __DMB();
            }
            g_sdmmc_async.data_end_cycles = DWT->CYCCNT;
            __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_DATA_FLAGS);
            __HAL_SD_DISABLE_IT(&hsd1, SDMMC_ASYNC_DATA_INTERRUPTS);
            hsd1.Instance->IDMACTRL = SDMMC_DISABLE_IDMA;
            hsd1.Instance->DLEN = 0U;
            hsd1.Instance->DCTRL = 0U;
            if(g_sdmmc_async.multi_block != 0U)
            {
                sdmmc_async_send_stop();
                if(g_sdmmc_async.completed_chunk_pending != 0U)
                {
                    event = SDMMC_ASYNC_EVENT_READ_CHUNK;
                }
            }
            else
            {
                event = sdmmc_async_complete();
            }
        }
        else if((status & SDMMC_FLAG_IDMABTC) != 0U)
        {
            const uint32_t entry_cycles = DWT->CYCCNT;
            __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_FLAG_IDMABTC);
            ++g_sdmmc_async_progress_diag.idmabtc_count;
            const uint32_t chunk = g_sdmmc_async.next_chunk;
            if((g_sdmmc_async.progressive == 0U)
                || (chunk >= SDMMC_ASYNC_PROGRESS_CHUNKS))
            {
                event = sdmmc_async_fail(HAL_SD_ERROR_DMA);
            }
            else
            {
                const uint32_t active = hsd1.Instance->IDMACTRL
                    & SDMMC_IDMA_IDMABACT;
                const uint8_t active_buffer =
                    (active != 0U) ? 1U : 0U;
                const uint8_t completed_buffer = active_buffer ^ 1U;
                const uint8_t expected_buffer = (uint8_t)(chunk & 1U);
                if(completed_buffer != expected_buffer)
                {
                    ++g_sdmmc_async_progress_diag.rearm_deadline_miss_count;
                    event = sdmmc_async_fail(HAL_SD_ERROR_DMA);
                }
                else if(chunk < 2U)
                {
                    if(completed_buffer == 0U)
                    {
                        hsd1.Instance->IDMABASE0 = (uint32_t)(uintptr_t)(
                            g_sdmmc_async.buffer
                            + (2U * SDMMC_ASYNC_PROGRESS_BYTES));
                    }
                    else
                    {
                        hsd1.Instance->IDMABASE1 = (uint32_t)(uintptr_t)(
                            g_sdmmc_async.buffer
                            + (3U * SDMMC_ASYNC_PROGRESS_BYTES));
                    }
                    const uint32_t rearm_cycles = DWT->CYCCNT - entry_cycles;
                    ++g_sdmmc_async_progress_diag.rearm_count;
                    g_sdmmc_async_progress_diag.rearm_cycles_sum += rearm_cycles;
                    g_sdmmc_async_progress_diag.average_rearm_cycles =
                        g_sdmmc_async_progress_diag.rearm_cycles_sum
                        / g_sdmmc_async_progress_diag.rearm_count;
                    if(rearm_cycles
                        > g_sdmmc_async_progress_diag.max_rearm_cycles)
                    {
                        g_sdmmc_async_progress_diag.max_rearm_cycles =
                            rearm_cycles;
                        g_sdmmc_async_progress_diag.max_rearm_us =
                            (uint32_t)(((uint64_t)rearm_cycles * 1000000U
                                        + (SystemCoreClock / 2U))
                                       / SystemCoreClock);
                    }
                    if(rearm_cycles > (uint32_t)(
                        ((uint64_t)SystemCoreClock
                         * SDMMC_ASYNC_REARM_DEADLINE_US) / 1000000U))
                    {
                        ++g_sdmmc_async_progress_diag
                            .rearm_deadline_miss_count;
                    }
                }
                else if(chunk == (SDMMC_ASYNC_PROGRESS_CHUNKS - 1U))
                {
                    /* The H743 raises IDMABTC for the fourth/final buffer too.
                     * Stop further buffer-complete IRQs, but leave IDMA and
                     * DPSM running until DATAEND closes the 16 KiB DLEN. */
                    __HAL_SD_DISABLE_IT(&hsd1, SDMMC_IT_IDMABTC);
                    ++g_sdmmc_async_progress_diag.final_chunk_idmabtc_count;
                }
                if(event == SDMMC_ASYNC_EVENT_NONE)
                {
                    g_sdmmc_async.completed_chunk_index = chunk;
                    g_sdmmc_async.completed_chunk_pending = 1U;
                    g_sdmmc_async.next_chunk = (uint8_t)(chunk + 1U);
                    __DMB();
                    event = SDMMC_ASYNC_EVENT_READ_CHUNK;
                }
            }
        }
    }
    else if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_WAIT_CMD12)
    {
        if((status & SDMMC_FLAG_CMDREND) != 0U)
        {
            const uint32_t error = sdmmc_async_response_error(status);
            __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_CMD_FLAGS);
            __HAL_SD_DISABLE_IT(&hsd1, SDMMC_ASYNC_COMMAND_INTERRUPTS);
            __SDMMC_CMDSTOP_DISABLE(hsd1.Instance);
            if(error != HAL_SD_ERROR_NONE)
            {
                event = sdmmc_async_fail(error);
            }
            else
            {
                event = sdmmc_async_complete();
            }
        }
    }

    return event;
}

uint8_t sdmmc_async_transport_take_completed_chunk(uint32_t *out_chunk_index)
{
    if((out_chunk_index == NULL)
        || (g_sdmmc_async.completed_chunk_pending == 0U))
    {
        return 0U;
    }
    __DMB();
    *out_chunk_index = g_sdmmc_async.completed_chunk_index;
    g_sdmmc_async.completed_chunk_pending = 0U;
    return 1U;
}

uint8_t sdmmc_async_transport_release_complete(void)
{
    if(g_sdmmc_async.state != SDMMC_ASYNC_STATE_COMPLETE)
    {
        return 0U;
    }
    g_sdmmc_async = (sdmmc_async_context_t){0};
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_IDLE;
    if((g_sdmmc_async_prepared.state == SDMMC_ASYNC_PREPARED_DONE)
            || (g_sdmmc_async_prepared.state == SDMMC_ASYNC_PREPARED_ERROR))
    {
        g_sdmmc_async_prepared = (sdmmc_async_prepared_descriptor_t){0};
    }
    __DMB();
    return 1U;
}

uint8_t sdmmc_async_transport_abort(void)
{
    (void)sdmmc_async_transport_invalidate_next();
    if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_IDLE)
    {
        return 1U;
    }
    if(g_sdmmc_async.state == SDMMC_ASYNC_STATE_COMPLETE)
    {
        return sdmmc_async_transport_release_complete();
    }

    sdmmc_async_quiesce_data_path();
    __SDMMC_CMDSTOP_DISABLE(hsd1.Instance);
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
    const HAL_StatusTypeDef status = HAL_SD_Abort(&hsd1);
    sdmmc_async_quiesce_data_path();
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);
    hsd1.Context = SD_CONTEXT_NONE;
    if(status != HAL_OK)
    {
        g_sdmmc_async.state = SDMMC_ASYNC_STATE_ERROR;
        return 0U;
    }
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State = HAL_SD_STATE_READY;
    g_sdmmc_async = (sdmmc_async_context_t){0};
    g_sdmmc_async.state = SDMMC_ASYNC_STATE_IDLE;
    g_sdmmc_async_prepared = (sdmmc_async_prepared_descriptor_t){0};
    return 1U;
}

sdmmc_async_state_t sdmmc_async_transport_state(void)
{
    return g_sdmmc_async.state;
}

uint32_t sdmmc_async_transport_error(void)
{
    return hsd1.ErrorCode;
}

uint32_t sdmmc_async_transport_command_cycles(void)
{
    return g_sdmmc_async.command_cycles;
}

uint32_t sdmmc_async_transport_data_start_cycles(void)
{
    return g_sdmmc_async.data_start_cycles;
}

uint32_t sdmmc_async_transport_data_end_cycles(void)
{
    return g_sdmmc_async.data_end_cycles;
}
