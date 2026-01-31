#ifndef AUDIO_IO_SD_H
#define AUDIO_IO_SD_H

#include "audio_buffer.h"
#include <stdint.h>

enum
{
  AUDIO_SD_CHANNELS = 2U
};

void audio_io_sd_init(audio_buffer_t *ring);
void audio_io_sd_task(void);
uint32_t audio_io_sd_pop(int32_t *dest, uint32_t frames);

#endif /* AUDIO_IO_SD_H */
