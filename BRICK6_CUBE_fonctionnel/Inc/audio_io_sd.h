#ifndef AUDIO_IO_SD_H
#define AUDIO_IO_SD_H

#include <stdbool.h>
#include <stdint.h>

void audio_io_sd_init(void);
bool audio_io_sd_has_block(void);
uint32_t audio_io_sd_read_block(int32_t *dst, uint32_t max_samples);

#endif /* AUDIO_IO_SD_H */
