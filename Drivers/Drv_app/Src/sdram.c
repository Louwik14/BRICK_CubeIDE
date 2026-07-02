/**
 * @file sdram.c
 * @brief Initialisation et test de la SDRAM externe via FMC.
 *
 * Deux SDRAM W9825G6KH x16 identiques sont montees en parallele sur le FMC x32.
 * La profondeur d'adressage reste celle d'une puce, la capacite logique en
 * octets double avec la largeur de bus.
 */

#include "sdram.h"
#include "fmc.h"
#include "w9825g6kh_conf.h"

#include <stddef.h>

static FMC_SDRAM_CommandTypeDef sdram_command;
static uint32_t sdram_tx_buffer[SDRAM_BUFFER_SIZE];
static uint32_t sdram_rx_buffer[SDRAM_BUFFER_SIZE];

static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command);
static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset);
static int32_t SDRAM_Test_Window(uint32_t first_word, uint32_t word_count);
static int32_t SDRAM_Test_AddressLines(void);
static void SDRAM_CleanInvalidate_Range(uint32_t first_word, uint32_t word_count);

void SDRAM_Init(void)
{
    SDRAM_Initialization_Sequence(&hsdram1, &sdram_command);
}

int32_t SDRAM_Test(void)
{
    return SDRAM_Test_Quick();
}

int32_t SDRAM_Test_Quick(void)
{
    Fill_Buffer(sdram_tx_buffer, SDRAM_BUFFER_SIZE, 0xA244250FU);
    Fill_Buffer(sdram_rx_buffer, SDRAM_BUFFER_SIZE, 0xBBBBBBBBU);

    for (uint32_t index = 0U; index < SDRAM_BUFFER_SIZE; index++)
    {
        sdram_write32(index, sdram_tx_buffer[index]);
    }
    SDRAM_CleanInvalidate_Range(0U, SDRAM_BUFFER_SIZE);

    for (uint32_t index = 0U; index < SDRAM_BUFFER_SIZE; index++)
    {
        sdram_rx_buffer[index] = sdram_read32(index);
    }

    for (uint32_t index = 0U; index < SDRAM_BUFFER_SIZE; index++)
    {
        if (sdram_rx_buffer[index] != sdram_tx_buffer[index])
        {
            return SDRAM_TEST_FAIL;
        }
    }

    if (SDRAM_Test_Window(0U, 256U) != SDRAM_TEST_OK)
    {
        return SDRAM_TEST_FAIL;
    }
    if (SDRAM_Test_Window(SDRAM_WORD_COUNT / 4U, 256U) != SDRAM_TEST_OK)
    {
        return SDRAM_TEST_FAIL;
    }
    if (SDRAM_Test_Window(SDRAM_WORD_COUNT / 2U, 256U) != SDRAM_TEST_OK)
    {
        return SDRAM_TEST_FAIL;
    }
    if (SDRAM_Test_Window(SDRAM_WORD_COUNT - 256U, 256U) != SDRAM_TEST_OK)
    {
        return SDRAM_TEST_FAIL;
    }
    if (SDRAM_Test_AddressLines() != SDRAM_TEST_OK)
    {
        return SDRAM_TEST_FAIL;
    }

    return SDRAM_TEST_OK;
}

static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command)
{
    uint32_t tmpmrd = 0U;

    command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1U;
    command->ModeRegisterDefinition = 0U;
    (void)HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    HAL_Delay(1U);

    command->CommandMode = FMC_SDRAM_CMD_PALL;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1U;
    command->ModeRegisterDefinition = 0U;
    (void)HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 8U;
    command->ModeRegisterDefinition = 0U;
    (void)HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1U;
    command->ModeRegisterDefinition = tmpmrd;
    (void)HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    (void)HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset)
{
    for (uint32_t index = 0U; index < buffer_length; index++)
    {
        pBuffer[index] = index + offset;
    }
}

static int32_t SDRAM_Test_Window(uint32_t first_word, uint32_t word_count)
{
    static const uint32_t patterns[] = {
        0x00000000U,
        0xFFFFFFFFU,
        0xAAAAAAAAU,
        0x55555555U,
        0xA5A5A5A5U,
        0x5A5A5A5AU,
        0x01234567U,
        0x89ABCDEFU
    };

    if ((word_count == 0U) || (first_word >= SDRAM_WORD_COUNT)
        || (word_count > (SDRAM_WORD_COUNT - first_word)))
    {
        return SDRAM_TEST_FAIL;
    }

    for (uint32_t p = 0U; p < (sizeof(patterns) / sizeof(patterns[0])); ++p)
    {
        const uint32_t pattern = patterns[p];
        for (uint32_t i = 0U; i < word_count; ++i)
        {
            sdram_write32(first_word + i, pattern ^ (first_word + i));
        }
        SDRAM_CleanInvalidate_Range(first_word, word_count);
        for (uint32_t i = 0U; i < word_count; ++i)
        {
            const uint32_t expected = pattern ^ (first_word + i);
            if (sdram_read32(first_word + i) != expected)
            {
                return SDRAM_TEST_FAIL;
            }
        }
    }

    for (uint32_t bit = 0U; bit < 32U; ++bit)
    {
        const uint32_t pattern = (1UL << bit);
        sdram_write32(first_word, pattern);
        SDRAM_CleanInvalidate_Range(first_word, 1U);
        if (sdram_read32(first_word) != pattern)
        {
            return SDRAM_TEST_FAIL;
        }

        sdram_write32(first_word, ~pattern);
        SDRAM_CleanInvalidate_Range(first_word, 1U);
        if (sdram_read32(first_word) != ~pattern)
        {
            return SDRAM_TEST_FAIL;
        }
    }

    return SDRAM_TEST_OK;
}

static int32_t SDRAM_Test_AddressLines(void)
{
    const uint32_t base_pattern = 0x13579BDFU;

    sdram_write32(0U, base_pattern);
    for (uint32_t bit = 0U; bit < 24U; ++bit)
    {
        const uint32_t word_index = (1UL << bit);
        if (word_index >= SDRAM_WORD_COUNT)
        {
            break;
        }
        sdram_write32(word_index, base_pattern ^ word_index);
    }

    SDRAM_CleanInvalidate_Range(0U, 1U);
    if (sdram_read32(0U) != base_pattern)
    {
        return SDRAM_TEST_FAIL;
    }

    for (uint32_t bit = 0U; bit < 24U; ++bit)
    {
        const uint32_t word_index = (1UL << bit);
        if (word_index >= SDRAM_WORD_COUNT)
        {
            break;
        }
        SDRAM_CleanInvalidate_Range(word_index, 1U);
        if (sdram_read32(word_index) != (base_pattern ^ word_index))
        {
            return SDRAM_TEST_FAIL;
        }
    }

    return SDRAM_TEST_OK;
}

static void SDRAM_CleanInvalidate_Range(uint32_t first_word, uint32_t word_count)
{
    const uintptr_t addr = (uintptr_t)SDRAM_BANK_ADDR
                           + ((uintptr_t)first_word * sizeof(uint32_t));
    const uintptr_t len = (uintptr_t)word_count * sizeof(uint32_t);
    const uintptr_t aligned_addr = addr & ~(uintptr_t)31U;
    const uintptr_t end_addr = (addr + len + 31U) & ~(uintptr_t)31U;

    if (end_addr > aligned_addr)
    {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)aligned_addr,
                                          (int32_t)(end_addr - aligned_addr));
    }
}
