#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB audio FIFO for 48 kHz, 24-bit stereo.
 * Samples are stored as signed 24-bit left-aligned in int32_t.
 */

enum
{
  USB_AUDIO_FIFO_FRAMES = 1024U
};

bool USBAudio_PopFrame(int32_t *left, int32_t *right);
void USBAudio_PushFrames(const int32_t *interleaved_stereo, uint32_t frames);

#endif /* USB_AUDIO_H */
