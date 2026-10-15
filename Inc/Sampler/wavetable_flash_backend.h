#ifndef WAVETABLE_FLASH_BACKEND_H
#define WAVETABLE_FLASH_BACKEND_H

#include <stdint.h>

typedef struct
{
    uint32_t base;
    uint32_t size;
    uint32_t erase_bytes;
    uint32_t program_bytes;
} wavetable_flash_geometry_t;

void wavetable_flash_backend_geometry(wavetable_flash_geometry_t *out);
const void *wavetable_flash_backend_map(uint32_t offset, uint32_t bytes);
uint8_t wavetable_flash_backend_is_erased(uint32_t offset, uint32_t bytes);
uint8_t wavetable_flash_backend_erase_all(void);
uint8_t wavetable_flash_backend_program(uint32_t offset,
                                        const uint8_t data[32]);
void wavetable_flash_backend_publish_barrier(void);

#endif
