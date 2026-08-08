#ifndef SD_INIT_DIAG_H
#define SD_INIT_DIAG_H

#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef enum
{
    SD_INIT_DIAG_STAGE_IDLE = 0,
    SD_INIT_DIAG_STAGE_MX_SDMMC1_ENTRY,
    SD_INIT_DIAG_STAGE_HAL_SD_INIT,
    SD_INIT_DIAG_STAGE_HAL_SD_INIT_CARD,
    SD_INIT_DIAG_STAGE_SD_POWER_ON,
    SD_INIT_DIAG_STAGE_SD_IDENT,
    SD_INIT_DIAG_STAGE_CARD_STATUS,
    SD_INIT_DIAG_STAGE_CONFIG_WIDE,
    SD_INIT_DIAG_STAGE_TRANSFER_READY_WAIT,
    SD_INIT_DIAG_STAGE_ERROR_HANDLER
} sd_init_diag_stage_t;

typedef struct
{
    volatile uint32_t boot_count;
    volatile uint32_t stage;
    volatile uint32_t failure_stage;
    volatile uint32_t last_command;
    volatile uint32_t last_argument;
    volatile uint32_t last_sdmmc_error;
    volatile uint32_t cmd55_attempts;
    volatile uint32_t acmd41_attempts;
    volatile uint32_t hal_status;
    volatile uint32_t hal_error_code;
    volatile uint32_t sta;
    volatile uint32_t resp1;
    volatile uint32_t clkcr;
    volatile uint32_t power;
    volatile uint32_t cmd;
    volatile uint32_t arg;
    volatile uint32_t dctrl;
    volatile uint32_t dcount;
    volatile uint32_t rcc_sdmmc_hz;
} sd_init_diag_t;

#if defined(BRICK6_VARIANT_LOWCOST) && defined(BRICK6_SD_INIT_DIAG)
extern volatile sd_init_diag_t g_sd_init_diag;

void sd_init_diag_reset(SD_HandleTypeDef *hsd);
void sd_init_diag_stage(uint32_t stage, SD_HandleTypeDef *hsd);
void sd_init_diag_command_sent(uint32_t command, uint32_t argument, SDMMC_TypeDef *instance);
void sd_init_diag_command_result(uint32_t command, uint32_t error, SDMMC_TypeDef *instance);
void sd_init_diag_hal_result(uint32_t stage, HAL_StatusTypeDef status, SD_HandleTypeDef *hsd);
#else
static inline void sd_init_diag_reset(SD_HandleTypeDef *hsd) { (void)hsd; }
static inline void sd_init_diag_stage(uint32_t stage, SD_HandleTypeDef *hsd) { (void)stage; (void)hsd; }
static inline void sd_init_diag_command_sent(uint32_t command, uint32_t argument, SDMMC_TypeDef *instance)
{
    (void)command;
    (void)argument;
    (void)instance;
}
static inline void sd_init_diag_command_result(uint32_t command, uint32_t error, SDMMC_TypeDef *instance)
{
    (void)command;
    (void)error;
    (void)instance;
}
static inline void sd_init_diag_hal_result(uint32_t stage, HAL_StatusTypeDef status, SD_HandleTypeDef *hsd)
{
    (void)stage;
    (void)status;
    (void)hsd;
}
#endif

#endif
