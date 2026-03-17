#include "App/Hall/hall_calibration.h"
#include "stm32h7xx_hal.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_adc.h"

#define CALIBRATION_HITS_REQUIRED 3U
#define CALIBRATION_PRESS_ADC     50000U
#define CALIBRATION_RELEASE_ADC   48000U

#define HALL_CAL_FLASH_ADDRESS    0x081E0000U
#define HALL_CAL_FLASH_BANK       FLASH_BANK_2
#define HALL_CAL_FLASH_SECTOR     FLASH_SECTOR_7

#define HALL_CAL_FLASH_CHUNKS     (sizeof(hall_calibration_blob_t) / 32U)

typedef enum
{
    KEY_STATE_RELEASED = 0U,
    KEY_STATE_PRESSED
} hall_key_state_t;

static hall_calibration_blob_t g_cal_blob;
static uint8_t g_press_count[HALL_KEY_COUNT];
static uint8_t g_key_done[HALL_KEY_COUNT];
static hall_key_state_t g_key_state[HALL_KEY_COUNT];
static uint8_t g_calibration_done = 0U;

static uint8_t hall_calibration_blob_is_valid(const hall_calibration_blob_t *blob)
{
    uint8_t any_written = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        if ((blob->min[i] != 0xFFFFU) || (blob->max[i] != 0xFFFFU))
        {
            any_written = 1U;
        }

        if (blob->min[i] >= blob->max[i])
        {
            return 0U;
        }
    }

    return any_written;
}

void hall_calibration_start(void)
{
    g_calibration_done = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        g_cal_blob.min[i] = 0xFFFFU;
        g_cal_blob.max[i] = 0U;
        g_press_count[i] = 0U;
        g_key_done[i] = 0U;
        g_key_state[i] = KEY_STATE_RELEASED;
    }
}

void hall_calibration_process(void)
{
    if (g_calibration_done != 0U)
    {
        return;
    }

    uint8_t done_count = 0U;

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        if (g_key_done[i] == 0U)
        {
        	const uint16_t v = hall_adc_get_raw(i);

            if (v < g_cal_blob.min[i])
            {
                g_cal_blob.min[i] = v;
            }

            if (v > g_cal_blob.max[i])
            {
                g_cal_blob.max[i] = v;
            }

            if ((g_key_state[i] == KEY_STATE_RELEASED) && (v > CALIBRATION_PRESS_ADC))
            {
                g_key_state[i] = KEY_STATE_PRESSED;

                if (g_press_count[i] < CALIBRATION_HITS_REQUIRED)
                {
                    g_press_count[i]++;
                }

                if (g_press_count[i] >= CALIBRATION_HITS_REQUIRED)
                {
                    g_key_done[i] = 1U;
                }
            }
            else if ((g_key_state[i] == KEY_STATE_PRESSED) && (v < CALIBRATION_RELEASE_ADC))
            {
                g_key_state[i] = KEY_STATE_RELEASED;
            }
        }

        if (g_key_done[i] != 0U)
        {
            done_count++;
        }
    }

    if (done_count == HALL_KEY_COUNT)
    {
        g_calibration_done = 1U;
        hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);
    }
}

uint8_t hall_calibration_get_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_press_count[key];
}

uint8_t hall_calibration_is_done(void)
{
    return g_calibration_done;
}

uint16_t hall_calibration_get_min(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_cal_blob.min[key];
}

uint16_t hall_calibration_get_max(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_cal_blob.max[key];
}

uint8_t hall_calibration_load(void)
{
    const hall_calibration_blob_t *stored = (const hall_calibration_blob_t *)HALL_CAL_FLASH_ADDRESS;

    if (hall_calibration_blob_is_valid(stored) == 0U)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        g_cal_blob.min[i] = stored->min[i];
        g_cal_blob.max[i] = stored->max[i];
    }

    hall_engine_set_calibration(g_cal_blob.min, g_cal_blob.max);

    return 1U;
}

void hall_calibration_save(void)
{
    if (g_calibration_done == 0U)
    {
        return;
    }

    if (hall_calibration_blob_is_valid(&g_cal_blob) == 0U)
    {
        return;
    }

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Banks = HALL_CAL_FLASH_BANK;
    erase.Sector = HALL_CAL_FLASH_SECTOR;
    erase.NbSectors = 1U;

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return;
    }

    const uint8_t *src_bytes = (const uint8_t *)&g_cal_blob;
    uint32_t dst_address = HALL_CAL_FLASH_ADDRESS;

    for (uint32_t i = 0U; i < HALL_CAL_FLASH_CHUNKS; i++)
    {
        const uint32_t src_address = (uint32_t)(uintptr_t)&src_bytes[i * 32U];

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, dst_address, src_address) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return;
        }

        dst_address += 32U;
    }

    HAL_FLASH_Lock();
}
