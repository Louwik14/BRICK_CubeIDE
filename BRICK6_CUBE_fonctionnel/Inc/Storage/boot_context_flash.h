#ifndef BOOT_CONTEXT_FLASH_H
#define BOOT_CONTEXT_FLASH_H

#include <stdint.h>

typedef struct __attribute__((packed))
{
    uint8_t version;
    uint8_t valid;
    uint32_t crc;
    uint8_t active_project_slot;
    uint8_t active_pattern_index;
} boot_context_flash_data_t;

void boot_context_flash_init(void);
uint8_t boot_context_flash_load(boot_context_flash_data_t *out_ctx);
uint8_t boot_context_flash_commit(uint8_t active_project_slot, uint8_t active_pattern_index);
void boot_context_flash_clear(void);

#endif
