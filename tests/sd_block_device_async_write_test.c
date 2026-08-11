#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "SD/sd_block_device.h"
#include "SD/bsp_driver_sd.h"
#include "Storage/sd_access_gate.h"
#include "stm32h7xx_hal.h"

#define TEST_SECTOR_BYTES 512U
#define TEST_DISK_SECTORS 512U
#define TEST_MAX_SECTORS 64U

#define TEST_ALIGNED __attribute__((aligned(32)))

typedef struct
{
    sd_block_device_operation_t operation;
    uint8_t *buffer;
    uint32_t lba;
    uint32_t sectors;
} test_hw_transfer_t;

static TEST_ALIGNED uint8_t g_disk[TEST_DISK_SECTORS * TEST_SECTOR_BYTES];
static TEST_ALIGNED uint8_t g_write_buffer[TEST_MAX_SECTORS * TEST_SECTOR_BYTES];
static TEST_ALIGNED uint8_t g_read_buffer[TEST_MAX_SECTORS * TEST_SECTOR_BYTES];
static test_hw_transfer_t g_hw;
static uint32_t g_tick;
static uint32_t g_epoch = 1U;
static uint32_t g_clean_calls;
static uint32_t g_invalidate_calls;
static uint32_t g_media_faults;
static uint32_t g_abort_requests;
static uint32_t g_max_hw_active;
static uint32_t g_superloop_iterations_during_write;
static uint8_t g_cache_contract_fail;
static uint8_t g_card_ready = 1U;
static uint8_t g_card_present = 1U;
static uint8_t g_start_fail;
static uint8_t g_abort_fail;
static uint8_t g_hw_active;

SD_HandleTypeDef hsd1;

#define CHECK(expr) do { \
    if(!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 0; \
    } \
} while(0)

uint32_t HAL_GetTick(void)
{
    return g_tick;
}

uint32_t test_get_ipsr(void)
{
    return 0U;
}

HAL_StatusTypeDef HAL_SD_Abort_IT(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    g_abort_requests++;
    return g_abort_fail ? HAL_ERROR : HAL_OK;
}

sd_access_client_t sd_access_gate_current_owner(void)
{
    return SD_ACCESS_CLIENT_RECORDER;
}

uint32_t sd_access_media_epoch(void)
{
    return g_epoch;
}

uint8_t BSP_SD_GetCardState(void)
{
    return g_card_ready ? SD_TRANSFER_OK : SD_TRANSFER_BUSY;
}

uint8_t brick_sd_is_detected(void)
{
    return g_card_present ? SD_PRESENT : SD_NOT_PRESENT;
}

void brick_sd_media_fault(void)
{
    g_media_faults++;
}

void test_cache_clean(const void *addr, size_t size)
{
    if((addr != g_write_buffer) || ((size % TEST_SECTOR_BYTES) != 0U))
    {
        g_cache_contract_fail = 1U;
    }
    g_clean_calls++;
}

void test_cache_invalidate(const void *addr, size_t size)
{
    if((addr != g_read_buffer) || ((size % TEST_SECTOR_BYTES) != 0U))
    {
        g_cache_contract_fail = 1U;
    }
    g_invalidate_calls++;
}

static uint8_t test_hw_begin(sd_block_device_operation_t operation,
                             uint8_t *buffer,
                             uint32_t lba,
                             uint32_t sectors)
{
    if(g_start_fail)
    {
        return MSD_ERROR;
    }
    if(g_hw_active)
    {
        g_max_hw_active = 2U;
        return MSD_ERROR;
    }
    g_hw.operation = operation;
    g_hw.buffer = buffer;
    g_hw.lba = lba;
    g_hw.sectors = sectors;
    g_hw_active = 1U;
    if(g_max_hw_active == 0U) g_max_hw_active = 1U;
    g_card_ready = 0U;
    return MSD_OK;
}

uint8_t brick_sd_read_blocks_dma(uint32_t *data,
                                 uint32_t block_idx,
                                 uint32_t blocks_nbr)
{
    return test_hw_begin(SD_BLOCK_DEVICE_OPERATION_READ,
                         (uint8_t *)data, block_idx, blocks_nbr);
}

uint8_t brick_sd_write_blocks_dma(const uint32_t *data,
                                  uint32_t block_idx,
                                  uint32_t blocks_nbr)
{
    return test_hw_begin(SD_BLOCK_DEVICE_OPERATION_WRITE,
                         (uint8_t *)(uintptr_t)data, block_idx, blocks_nbr);
}

static void test_complete_dma(void)
{
    const size_t bytes = (size_t)g_hw.sectors * TEST_SECTOR_BYTES;
    uint8_t *const media = &g_disk[(size_t)g_hw.lba * TEST_SECTOR_BYTES];
    if(g_hw.operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
    {
        memcpy(media, g_hw.buffer, bytes);
        sd_block_device_async_write_complete_isr();
    }
    else
    {
        memcpy(g_hw.buffer, media, bytes);
        sd_block_device_async_read_complete_isr();
    }
    g_hw_active = 0U;
}

static void test_complete_abort(void)
{
    g_hw_active = 0U;
    g_card_ready = 1U;
    sd_block_device_async_abort_complete_isr();
}

static void fill_pattern(uint8_t *buffer, size_t bytes, unsigned pattern)
{
    if(pattern == 0U)
    {
        memset(buffer, 0x00, bytes);
    }
    else if(pattern == 1U)
    {
        memset(buffer, 0xFF, bytes);
    }
    else if(pattern == 2U)
    {
        for(size_t i = 0U; i < bytes / sizeof(uint32_t); ++i)
        {
            ((uint32_t *)buffer)[i] = (uint32_t)i;
        }
    }
    else
    {
        uint32_t value = 0x13579BDFU;
        for(size_t i = 0U; i < bytes; ++i)
        {
            value = value * 1664525U + 1013904223U;
            buffer[i] = (uint8_t)(value >> 24);
        }
    }
}

static int take_expected(sd_block_device_operation_t operation,
                         sd_block_device_result_t result,
                         uint32_t generation,
                         const void *buffer)
{
    sd_block_device_async_completion_t completion;
    CHECK(sd_block_device_async_take_completion(&completion) == 1U);
    CHECK(completion.operation == operation);
    CHECK(completion.result == result);
    CHECK(completion.owner_generation == generation);
    CHECK(completion.media_epoch == g_epoch);
    if(operation == SD_BLOCK_DEVICE_OPERATION_WRITE)
    {
        CHECK(completion.src == buffer);
        CHECK(completion.dst == NULL);
    }
    else
    {
        CHECK(completion.dst == buffer);
        CHECK(completion.src == NULL);
    }
    return 1;
}

static int run_data_contract(void)
{
    static const uint32_t counts[] = {1U, 8U, 16U, 32U, 64U};
    uint32_t lba = 8U;

    for(unsigned test = 0U; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        const uint32_t sectors = counts[test];
        const size_t bytes = (size_t)sectors * TEST_SECTOR_BYTES;
        const uint32_t generation = 100U + test;
        fill_pattern(g_write_buffer, bytes, test % 4U);
        memset(g_read_buffer, 0x5A, bytes);

        CHECK(sd_block_device_async_write_submit(lba, sectors,
                                                 g_write_buffer, generation)
              == SD_BLOCK_DEVICE_OK);
        CHECK(g_hw_active != 0U);
        CHECK(sd_block_device_async_hardware_state()
              == SD_BLOCK_DEVICE_HW_WRITE_DMA);
        CHECK(sd_block_device_async_write_buffer_locked(g_write_buffer) != 0U);
        CHECK(sd_block_device_async_write_submit(lba, 1U,
                                                 g_write_buffer, generation)
              == SD_BLOCK_DEVICE_BUSY);
        const uint8_t read_queued_behind_write = (test == 0U) ? 1U : 0U;
        if(read_queued_behind_write != 0U)
        {
            CHECK(sd_block_device_async_enqueue(lba, sectors, g_read_buffer)
                  == SD_BLOCK_DEVICE_OK);
            CHECK(g_hw.operation == SD_BLOCK_DEVICE_OPERATION_WRITE);
            CHECK(g_max_hw_active == 1U);
        }
        for(unsigned i = 0U; i < 5U; ++i)
        {
            g_tick++;
            g_superloop_iterations_during_write++;
            sd_block_device_async_poll();
            CHECK(sd_block_device_async_take_completion(
                      &(sd_block_device_async_completion_t){0}) == 0U);
        }
        CHECK(g_superloop_iterations_during_write > 0U);
        test_complete_dma();
        for(unsigned i = 0U; i < 3U; ++i)
        {
            g_tick++;
            sd_block_device_async_poll();
            CHECK(sd_block_device_async_hardware_state()
                  == SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY);
        }
        g_card_ready = 1U;
        sd_block_device_async_poll();
        CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                            SD_BLOCK_DEVICE_OK, generation, g_write_buffer));
        CHECK(sd_block_device_async_write_buffer_locked(g_write_buffer) == 0U);
        CHECK(memcmp(&g_disk[(size_t)lba * TEST_SECTOR_BYTES],
                     g_write_buffer, bytes) == 0);

        if(read_queued_behind_write == 0U)
        {
            CHECK(sd_block_device_async_enqueue(lba, sectors, g_read_buffer)
                  == SD_BLOCK_DEVICE_OK);
        }
        CHECK(g_hw.operation == SD_BLOCK_DEVICE_OPERATION_READ);
        g_tick++;
        test_complete_dma();
        g_tick += 2U;
        g_card_ready = 1U;
        sd_block_device_async_poll();
        CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_READ,
                            SD_BLOCK_DEVICE_OK, 0U, g_read_buffer));
        CHECK(memcmp(g_read_buffer, g_write_buffer, bytes) == 0);
        lba += sectors + 1U;
    }
    CHECK(g_max_hw_active == 1U);
    CHECK(g_clean_calls == 5U);
    CHECK(g_invalidate_calls == 10U);
    CHECK(g_cache_contract_fail == 0U);
    return 1;
}

static void reset_error_fixture(void)
{
    g_card_ready = 1U;
    g_card_present = 1U;
    g_start_fail = 0U;
    g_abort_fail = 0U;
    g_hw_active = 0U;
}

static int run_error_contract(void)
{
    sd_block_device_async_completion_t completion;

    reset_error_fixture();
    g_start_fail = 1U;
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 200U)
          == SD_BLOCK_DEVICE_OK);
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_DMA_START_FAIL, 200U, g_write_buffer));

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 201U)
          == SD_BLOCK_DEVICE_OK);
    g_hw_active = 0U;
    sd_block_device_async_error_isr();
    sd_block_device_async_poll();
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_WRITE_FAIL, 201U, g_write_buffer));

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 202U)
          == SD_BLOCK_DEVICE_OK);
    g_tick += 21U;
    sd_block_device_async_poll();
    CHECK(sd_block_device_async_hardware_state() == SD_BLOCK_DEVICE_HW_ABORTING);
    CHECK(sd_block_device_async_write_buffer_locked(g_write_buffer) != 0U);
    test_complete_abort();
    sd_block_device_async_poll();
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_TIMEOUT, 202U, g_write_buffer));

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 203U)
          == SD_BLOCK_DEVICE_OK);
    g_epoch++;
    sd_block_device_async_poll();
    test_complete_abort();
    sd_block_device_async_poll();
    CHECK(sd_block_device_async_take_completion(&completion) == 1U);
    CHECK(completion.result == SD_BLOCK_DEVICE_MEDIA_CHANGED);
    CHECK(completion.media_epoch == g_epoch - 1U);

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 204U)
          == SD_BLOCK_DEVICE_OK);
    g_card_present = 0U;
    sd_block_device_async_poll();
    test_complete_abort();
    sd_block_device_async_poll();
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_CARD_REMOVED, 204U, g_write_buffer));

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 205U)
          == SD_BLOCK_DEVICE_OK);
    CHECK(sd_block_device_async_abort_active() == SD_BLOCK_DEVICE_OK);
    CHECK(sd_block_device_async_write_buffer_locked(g_write_buffer) != 0U);
    test_complete_abort();
    sd_block_device_async_poll();
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_ABORTED, 205U, g_write_buffer));

    reset_error_fixture();
    CHECK(sd_block_device_async_write_submit(2U, 1U, g_write_buffer, 206U)
          == SD_BLOCK_DEVICE_OK);
    test_complete_dma();
    sd_block_device_async_poll();
    CHECK(sd_block_device_async_hardware_state()
          == SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY);
    g_tick += 21U;
    sd_block_device_async_poll();
    CHECK(sd_block_device_async_hardware_state() == SD_BLOCK_DEVICE_HW_ABORTING);
    test_complete_abort();
    sd_block_device_async_poll();
    CHECK(take_expected(SD_BLOCK_DEVICE_OPERATION_WRITE,
                        SD_BLOCK_DEVICE_TIMEOUT, 206U, g_write_buffer));
    return 1;
}

int main(void)
{
    sd_block_device_async_metrics_t metrics;
    sd_block_device_async_init();
    CHECK(sd_block_device_async_write_submit(0U, 1U,
                                             &g_write_buffer[1], 1U)
          == SD_BLOCK_DEVICE_INVALID_ARG);
    CHECK(run_data_contract());
    CHECK(run_error_contract());
    sd_block_device_async_metrics_get(&metrics);
    CHECK(metrics.write_submitted == 12U);
    CHECK(metrics.write_completed == 5U);
    CHECK(metrics.write_failed == 7U);
    CHECK(metrics.busy_rejects == 5U);
    CHECK(metrics.read_to_write_switches >= 4U);
    CHECK(metrics.write_to_read_switches >= 5U);
    CHECK(metrics.abort_count == 5U);
    CHECK(metrics.max_card_ready_latency_ms >= 3U);
    printf("PASS async WRITE: submitted=%lu completed=%lu failed=%lu "
           "busy=%lu max_ms=%lu ready_latency_ms=%lu superloop=%lu R2W=%lu W2R=%lu\n",
           (unsigned long)metrics.write_submitted,
           (unsigned long)metrics.write_completed,
           (unsigned long)metrics.write_failed,
           (unsigned long)metrics.busy_rejects,
           (unsigned long)metrics.max_transaction_duration_ms,
           (unsigned long)metrics.max_card_ready_latency_ms,
           (unsigned long)g_superloop_iterations_during_write,
           (unsigned long)metrics.read_to_write_switches,
           (unsigned long)metrics.write_to_read_switches);
    return 0;
}
