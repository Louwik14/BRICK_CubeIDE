#ifndef GROOVE_FLASH_BACKEND_H
#define GROOVE_FLASH_BACKEND_H

#include <stdint.h>

uint8_t groove_flash_backend_erase_all(void);
uint8_t groove_flash_backend_program(uint32_t address,
                                     const uint8_t data[32]);
void groove_flash_backend_publish_barrier(void);

#endif
