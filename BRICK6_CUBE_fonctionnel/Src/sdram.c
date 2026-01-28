#include "sdram.h"
#include "fmc.h"
#include "usart.h"
#include "w9825g6kh_conf.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * Local buffers for test
 * ========================================================= */
static FMC_SDRAM_CommandTypeDef sdram_command;
static uint32_t sdram_tx_buffer[SDRAM_BUFFER_SIZE];
static uint32_t sdram_rx_buffer[SDRAM_BUFFER_SIZE];

/* =========================================================
 * Local prototypes
 * ========================================================= */
static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command);
static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset);

/* =========================================================
 * Public API
 * ========================================================= */

void SDRAM_Init(void)
{
    const char *msg = "Starting SDRAM init...\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 10);

    SDRAM_Initialization_Sequence(&hsdram1, &sdram_command);

    msg = "SDRAM init done\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 10);
}

void SDRAM_Test(void)
{
    uint32_t index = 0;
    uint32_t status = 0;
    uint32_t fail_index = 0;
    char log_buffer[128];

    HAL_UART_Transmit(&huart1, (uint8_t *)"Starting SDRAM test...\r\n", 25, 10);

    Fill_Buffer(sdram_tx_buffer, SDRAM_BUFFER_SIZE, 0xA244250FU);
    Fill_Buffer(sdram_rx_buffer, SDRAM_BUFFER_SIZE, 0xBBBBBBBBU);

    /* Write */
    for (index = 0; index < SDRAM_BUFFER_SIZE; index++)
    {
        sdram_write32(index, sdram_tx_buffer[index]);
    }

    HAL_UART_Transmit(&huart1, (uint8_t *)"SDRAM write done\r\n", 18, 10);

    /* Read */
    for (index = 0; index < SDRAM_BUFFER_SIZE; index++)
    {
        sdram_rx_buffer[index] = sdram_read32(index);
    }

    /* Compare */
    for (index = 0; index < SDRAM_BUFFER_SIZE; index++)
    {
        if (sdram_rx_buffer[index] != sdram_tx_buffer[index])
        {
            status = 1;
            fail_index = index;
            break;
        }
    }

    if (status != 0U)
    {
        snprintf(log_buffer, sizeof(log_buffer),
                 "SDRAM test FAILED at index %lu: got 0x%08lX expected 0x%08lX\r\n",
                 (unsigned long)fail_index,
                 (unsigned long)sdram_rx_buffer[fail_index],
                 (unsigned long)sdram_tx_buffer[fail_index]);
    }
    else
    {
        snprintf(log_buffer, sizeof(log_buffer),
                 "SDRAM test OK (%lu words)\r\n",
                 (unsigned long)SDRAM_BUFFER_SIZE);
    }

    HAL_UART_Transmit(&huart1, (uint8_t *)log_buffer, strlen(log_buffer), 10);
}

/* =========================================================
 * SDRAM Initialization Sequence (ST style)
 * ========================================================= */

static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command)
{
    __IO uint32_t tmpmrd = 0;

    /* Step 1: Clock enable */
    command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    HAL_Delay(1);

    /* Step 2: Precharge all */
    command->CommandMode = FMC_SDRAM_CMD_PALL;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 3: Auto-refresh */
    command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 8;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 4: Load mode register */
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 5: Set refresh rate */
    HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

/* =========================================================
 * Utils
 * ========================================================= */

static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset)
{
    for (uint32_t index = 0; index < buffer_length; index++)
    {
        pBuffer[index] = index + offset;
    }
}
