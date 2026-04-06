#include "Storage/boot_context_flash.h"

#include <string.h>

#include "stm32h7xx_hal.h"

#define BOOT_CONTEXT_FLASH_VERSION 1U
#define BOOT_CONTEXT_FLASH_BANK    FLASH_BANK_2
#define BOOT_CONTEXT_FLASH_SECTOR  FLASH_SECTOR_7

extern const uint8_t __boot_context_flash_start__[];

static boot_context_flash_data_t g_boot_ctx_cache;
static uint8_t g_boot_ctx_cache_valid;

static uint32_t boot_context_flash_crc32(const boot_context_flash_data_t *ctx)
{
    if (ctx == 0)
    {
        return 0U;
    }

    uint32_t crc = 0xFFFFFFFFUL;
    const uint8_t payload[] = {
        ctx->version,
        ctx->valid,
        ctx->active_project_slot
    };

    for (uint32_t i = 0U; i < sizeof(payload); ++i)
    {
        crc ^= payload[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static uint8_t boot_context_flash_is_valid(const boot_context_flash_data_t *ctx)
{
    if (ctx == 0)
    {
        return 0U;
    }

    if ((ctx->version != BOOT_CONTEXT_FLASH_VERSION) || (ctx->valid == 0U))
    {
        return 0U;
    }

    return (ctx->crc == boot_context_flash_crc32(ctx)) ? 1U : 0U;
}

static uint8_t boot_context_flash_read_raw(boot_context_flash_data_t *out_ctx)
{
    if (out_ctx == 0)
    {
        return 0U;
    }

    memcpy(out_ctx, (const void *)__boot_context_flash_start__, sizeof(*out_ctx));
    return 1U;
}

static uint8_t boot_context_flash_write_raw(const boot_context_flash_data_t *ctx)
{
    if (ctx == 0)
    {
        return 0U;
    }

    FLASH_EraseInitTypeDef erase;
    uint32_t erase_error = 0U;
    HAL_StatusTypeDef st = HAL_ERROR;
    uint8_t flash_word[32];

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = BOOT_CONTEXT_FLASH_BANK;
    erase.Sector = BOOT_CONTEXT_FLASH_SECTOR;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    memset(flash_word, 0xFF, sizeof(flash_word));
    memcpy(flash_word, ctx, sizeof(*ctx));

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &erase_error) == HAL_OK)
    {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                               (uint32_t)(uintptr_t)__boot_context_flash_start__,
                               (uint32_t)(uintptr_t)flash_word);
    }
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 1U : 0U;
}

void boot_context_flash_init(void)
{
    boot_context_flash_data_t raw;
    memset(&g_boot_ctx_cache, 0, sizeof(g_boot_ctx_cache));
    g_boot_ctx_cache_valid = 0U;

    if (boot_context_flash_read_raw(&raw) == 0U)
    {
        return;
    }

    if (boot_context_flash_is_valid(&raw) != 0U)
    {
        memcpy(&g_boot_ctx_cache, &raw, sizeof(g_boot_ctx_cache));
        g_boot_ctx_cache_valid = 1U;
    }
}

uint8_t boot_context_flash_load(boot_context_flash_data_t *out_ctx)
{
    boot_context_flash_data_t raw;
    if ((out_ctx == 0) || (boot_context_flash_read_raw(&raw) == 0U))
    {
        return 0U;
    }

    if (boot_context_flash_is_valid(&raw) == 0U)
    {
        return 0U;
    }

    memcpy(out_ctx, &raw, sizeof(*out_ctx));
    memcpy(&g_boot_ctx_cache, &raw, sizeof(g_boot_ctx_cache));
    g_boot_ctx_cache_valid = 1U;
    return 1U;
}

uint8_t boot_context_flash_commit(uint8_t active_project_slot)
{
    boot_context_flash_data_t next_ctx;

    next_ctx.version = BOOT_CONTEXT_FLASH_VERSION;
    next_ctx.valid = 1U;
    next_ctx.active_project_slot = active_project_slot;
    next_ctx.crc = boot_context_flash_crc32(&next_ctx);

    if ((g_boot_ctx_cache_valid != 0U)
        && (memcmp(&g_boot_ctx_cache, &next_ctx, sizeof(next_ctx)) == 0))
    {
        return 1U;
    }

    if (boot_context_flash_write_raw(&next_ctx) == 0U)
    {
        return 0U;
    }

    memcpy(&g_boot_ctx_cache, &next_ctx, sizeof(g_boot_ctx_cache));
    g_boot_ctx_cache_valid = 1U;
    return 1U;
}

void boot_context_flash_clear(void)
{
    boot_context_flash_data_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.version = BOOT_CONTEXT_FLASH_VERSION;
    ctx.valid = 0U;
    ctx.active_project_slot = 0U;
    ctx.crc = boot_context_flash_crc32(&ctx);

    if (boot_context_flash_write_raw(&ctx) != 0U)
    {
        memset(&g_boot_ctx_cache, 0, sizeof(g_boot_ctx_cache));
        g_boot_ctx_cache_valid = 0U;
    }
}
