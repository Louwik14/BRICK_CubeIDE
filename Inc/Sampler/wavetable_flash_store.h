#ifndef WAVETABLE_FLASH_STORE_H
#define WAVETABLE_FLASH_STORE_H

#include <stdint.h>

#include "Sampler/wavetable_pool.h"

#define WAVETABLE_FLASH_STORE_FORMAT_VERSION (1U)

void wavetable_flash_store_init(void);
uint8_t wavetable_flash_store_scan_slot(uint16_t index,
                                        wavetable_slot_t *out_slot);
uint8_t wavetable_flash_store_append(const wavetable_slot_t *prepared,
                                     uint32_t source_crc32,
                                     uint32_t base_crc32,
                                     uint32_t payload_crc32,
                                     wavetable_slot_t *out_persistent);
uint8_t wavetable_flash_store_delete(uint32_t flash_offset);
uint32_t wavetable_flash_store_used_bytes(void);
uint32_t wavetable_flash_store_free_bytes(void);

#endif
