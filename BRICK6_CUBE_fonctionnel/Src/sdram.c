#include "sdram.h"
#include "fmc.h"
#include "usart.h"
#include "w9825g6kh_conf.h"

#include <stdio.h>
#include <string.h>

static FMC_SDRAM_CommandTypeDef sdram_command;
static uint32_t sdram_tx_buffer[SDRAM_BUFFER_SIZE];
static uint32_t sdram_rx_buffer[SDRAM_BUFFER_SIZE];

static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command);
static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset);

void SDRAM_Init(void)
{
  SDRAM_Initialization_Sequence(&hsdram1, &sdram_command);
}

void SDRAM_Test(void)
{
  uint32_t index = 0;
  uint32_t status = 0;
  uint32_t fail_index = 0;
  char log_buffer[96];

  Fill_Buffer(sdram_tx_buffer, SDRAM_BUFFER_SIZE, 0xA244250FU);
  Fill_Buffer(sdram_rx_buffer, SDRAM_BUFFER_SIZE, 0xBBBBBBBBU);

  for (index = 0; index < SDRAM_BUFFER_SIZE; index++)
  {
    sdram_write32(index, sdram_tx_buffer[index]);
  }

  for (index = 0; index < SDRAM_BUFFER_SIZE; index++)
  {
    sdram_rx_buffer[index] = sdram_read32(index);
  }

  for (index = 0; (index < SDRAM_BUFFER_SIZE) && (status == 0U); index++)
  {
    if (sdram_rx_buffer[index] != sdram_tx_buffer[index])
    {
      status++;
      fail_index = index;
    }
  }

  if (status != 0U)
  {
    (void)snprintf(log_buffer, sizeof(log_buffer),
                   "SDRAM test failed at index %lu\r\n",
                   (unsigned long)fail_index);
  }
  else
  {
    (void)snprintf(log_buffer, sizeof(log_buffer),
                   "SDRAM test OK (%lu words)\r\n",
                   (unsigned long)SDRAM_BUFFER_SIZE);
  }

  (void)HAL_UART_Transmit(&huart1, (uint8_t *)log_buffer,
                          (uint16_t)strlen(log_buffer), 10);
}

static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command)
{
  __IO uint32_t tmpmrd = 0;

  command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
  command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  command->AutoRefreshNumber = 1;
  command->ModeRegisterDefinition = 0;

  HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

  HAL_Delay(1);

  command->CommandMode = FMC_SDRAM_CMD_PALL;
  command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  command->AutoRefreshNumber = 1;
  command->ModeRegisterDefinition = 0;

  HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

  command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  command->AutoRefreshNumber = 8;
  command->ModeRegisterDefinition = 0;

  HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

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

  HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset)
{
  uint32_t index = 0;

  for (index = 0; index < buffer_length; index++)
  {
    pBuffer[index] = index + offset;
  }
}
