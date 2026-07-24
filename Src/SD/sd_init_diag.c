#include "SD/sd_init_diag.h"

#if defined(BRICK6_VARIANT_LOWCOST)
volatile sd_init_diag_t g_sd_init_diag;

static void sd_init_diag_capture_regs(SDMMC_TypeDef *instance)
{
    if (instance == 0)
    {
        return;
    }

    g_sd_init_diag.sta = instance->STA;
    g_sd_init_diag.resp1 = instance->RESP1;
    g_sd_init_diag.clkcr = instance->CLKCR;
    g_sd_init_diag.power = instance->POWER;
    g_sd_init_diag.cmd = instance->CMD;
    g_sd_init_diag.arg = instance->ARG;
    g_sd_init_diag.dctrl = instance->DCTRL;
    g_sd_init_diag.dcount = instance->DCOUNT;
}

void sd_init_diag_reset(SD_HandleTypeDef *hsd)
{
    g_sd_init_diag.boot_count++;
    g_sd_init_diag.stage = SD_INIT_DIAG_STAGE_MX_SDMMC1_ENTRY;
    g_sd_init_diag.failure_stage = SD_INIT_DIAG_STAGE_IDLE;
    g_sd_init_diag.last_command = 0xFFFFFFFFU;
    g_sd_init_diag.last_argument = 0U;
    g_sd_init_diag.last_sdmmc_error = 0U;
    g_sd_init_diag.cmd55_attempts = 0U;
    g_sd_init_diag.acmd41_attempts = 0U;
    g_sd_init_diag.hal_status = HAL_OK;
    g_sd_init_diag.hal_error_code = 0U;
    g_sd_init_diag.rcc_sdmmc_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    sd_init_diag_capture_regs((hsd != 0) ? hsd->Instance : 0);
}

void sd_init_diag_stage(uint32_t stage, SD_HandleTypeDef *hsd)
{
    g_sd_init_diag.stage = stage;
    g_sd_init_diag.rcc_sdmmc_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    if (hsd != 0)
    {
        g_sd_init_diag.hal_error_code = hsd->ErrorCode;
        sd_init_diag_capture_regs(hsd->Instance);
    }
}

void sd_init_diag_command_sent(uint32_t command, uint32_t argument, SDMMC_TypeDef *instance)
{
    g_sd_init_diag.last_command = command;
    g_sd_init_diag.last_argument = argument;
    if (command == 55U)
    {
        g_sd_init_diag.cmd55_attempts++;
    }
    else if (command == 41U)
    {
        g_sd_init_diag.acmd41_attempts++;
    }
    sd_init_diag_capture_regs(instance);
}

void sd_init_diag_command_result(uint32_t command, uint32_t error, SDMMC_TypeDef *instance)
{
    g_sd_init_diag.last_command = command;
    g_sd_init_diag.last_sdmmc_error = error;
    if (error != 0U)
    {
        g_sd_init_diag.failure_stage = g_sd_init_diag.stage;
    }
    sd_init_diag_capture_regs(instance);
}

void sd_init_diag_hal_result(uint32_t stage, HAL_StatusTypeDef status, SD_HandleTypeDef *hsd)
{
    g_sd_init_diag.stage = stage;
    g_sd_init_diag.hal_status = (uint32_t)status;
    if (status != HAL_OK)
    {
        g_sd_init_diag.failure_stage = stage;
    }
    if (hsd != 0)
    {
        g_sd_init_diag.hal_error_code = hsd->ErrorCode;
        sd_init_diag_capture_regs(hsd->Instance);
    }
}
#endif
