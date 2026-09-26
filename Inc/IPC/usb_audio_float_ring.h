#ifndef USB_AUDIO_FLOAT_RING_H_
#define USB_AUDIO_FLOAT_RING_H_

#include <stdint.h>

#define USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES 288U
#define USB_AUDIO_FLOAT_RING_CHANNELS         2U
#define USB_AUDIO_FLOAT_RING_BYTES_PER_FRAME \
    (USB_AUDIO_FLOAT_RING_CHANNELS * sizeof(float))
#define USB_AUDIO_FLOAT_RING_TARGET_FRAMES    144U
#define USB_AUDIO_FLOAT_RING_START_FRAMES     144U

/*
 * Pointer-free SPSC transport contract.  The PC->BRICK ring is written by
 * USB and read by AUDIO.  The BRICK->PC ring is written by AUDIO and read by
 * USB.  The backing object is placed in the existing non-cacheable D3 IPC
 * window; a future MCU port can preserve the same interleaved-float contract.
 * Finite host samples are preserved verbatim; NaN and infinities become 0.0f.
 */
uint32_t usb_audio_float_write_pc_to_brick(const float *interleaved,
                                           uint32_t frames);
uint32_t usb_audio_float_read_pc_to_brick(float *left,
                                          float *right,
                                          uint32_t frames);
uint32_t usb_audio_float_write_brick_to_pc(const float *left,
                                           const float *right,
                                           uint32_t frames);
uint32_t usb_audio_float_peek_brick_to_pc(float *interleaved,
                                          uint32_t frames);
void usb_audio_float_discard_brick_to_pc(uint32_t frames);

uint32_t usb_audio_float_pc_to_brick_available(void);
uint32_t usb_audio_float_brick_to_pc_available(void);

void usb_audio_float_reset(void);

#endif /* USB_AUDIO_FLOAT_RING_H_ */
