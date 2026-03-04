#pragma once

#include <stdbool.h>
#include <stdint.h>

bool sd_reader_open(const char *path, uint32_t data_offset, uint32_t data_size);
void sd_reader_close(void);
bool sd_reader_read_looping(uint8_t *dst, uint32_t requested_bytes, uint32_t *out_bytes);

