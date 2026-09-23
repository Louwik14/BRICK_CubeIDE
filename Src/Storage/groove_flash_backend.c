#include "Storage/groove_flash_backend.h"

#include <string.h>

#include "Platform/groove_flash_layout.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

#define GROOVE_FLASH_WAIT_LIMIT UINT32_C(100000000)
#define GROOVE_FLASH_ERROR_MASK (FLASH_SR_WRPERR | FLASH_SR_PGSERR \
    | FLASH_SR_STRBERR | FLASH_SR_INCERR | FLASH_SR_OPERR \
    | FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_SNECCERR \
    | FLASH_SR_DBECCERR)

AUDIO_HOT ALIGN32 static uint32_t g_groove_flashword[8];

static ITCM_TEXT uint8_t groove_flash_wait_bank2(void)
{
    uint32_t remaining = GROOVE_FLASH_WAIT_LIMIT;
    while ((FLASH->SR2 & FLASH_SR_QW) != 0U)
    {
        if (--remaining == 0U) return 0U;
    }
    const uint32_t errors = FLASH->SR2 & GROOVE_FLASH_ERROR_MASK;
    if (errors != 0U)
    {
        FLASH->CCR2 = errors;
        return 0U;
    }
    if ((FLASH->SR2 & FLASH_SR_EOP) != 0U) FLASH->CCR2 = FLASH_CCR_CLR_EOP;
    return 1U;
}

static ITCM_TEXT uint8_t groove_flash_erase_sector_itcm(uint32_t sector)
{
    if (groove_flash_wait_bank2() == 0U) return 0U;
    FLASH->CR2 &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR2 |= FLASH_CR_SER | FLASH_PSIZE_WORD
        | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_START;
    __DSB();
    __ISB();
    const uint8_t ok = groove_flash_wait_bank2();
    FLASH->CR2 &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    __DSB();
    __ISB();
    return ok;
}

static ITCM_TEXT uint8_t groove_flash_program_itcm(uint32_t address,
                                                   const uint32_t *source)
{
    if (groove_flash_wait_bank2() == 0U) return 0U;
    FLASH->CR2 |= FLASH_CR_PG;
    __DSB();
    __ISB();
    volatile uint32_t *destination = (volatile uint32_t *)address;
    for (uint32_t i = 0U; i < 8U; ++i) destination[i] = source[i];
    __DSB();
    __ISB();
    const uint8_t ok = groove_flash_wait_bank2();
    FLASH->CR2 &= ~FLASH_CR_PG;
    __DSB();
    __ISB();
    return ok;
}

uint8_t groove_flash_backend_erase_all(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint8_t ok6 = groove_flash_erase_sector_itcm(FLASH_SECTOR_6);
    const uint8_t ok7 = (ok6 != 0U)
        ? groove_flash_erase_sector_itcm(FLASH_SECTOR_7) : 0U;
    __set_PRIMASK(primask);
    (void)HAL_FLASH_Lock();
    SCB_InvalidateDCache_by_Addr((uint32_t *)GROOVE_FLASH_BASE,
                                 (int32_t)GROOVE_FLASH_SIZE);
    return (uint8_t)(ok6 && ok7);
}

uint8_t groove_flash_backend_program(uint32_t address,
                                     const uint8_t data[32])
{
    if ((data == 0) || ((address & 31U) != 0U)
            || (address < GROOVE_FLASH_BASE)
            || (address > GROOVE_FLASH_END - 32U)) return 0U;
    memcpy(g_groove_flashword, data, 32U);
    if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint8_t ok = groove_flash_program_itcm(address,
                                                  g_groove_flashword);
    __set_PRIMASK(primask);
    (void)HAL_FLASH_Lock();
    SCB_InvalidateDCache_by_Addr((uint32_t *)address, 32);
    if ((ok != 0U)
            && (memcmp((const void *)address, g_groove_flashword, 32U) != 0))
        return 0U;
    return ok;
}

void groove_flash_backend_publish_barrier(void)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)GROOVE_FLASH_BASE,
                                 (int32_t)GROOVE_FLASH_SIZE);
    __DSB();
    __ISB();
}
