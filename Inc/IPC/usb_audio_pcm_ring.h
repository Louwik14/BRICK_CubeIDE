#ifndef USB_AUDIO_PCM_RING_H_
#define USB_AUDIO_PCM_RING_H_

#include <stdint.h>

#define USB_AUDIO_PCM_RING_CAPACITY_FRAMES 288U
#define USB_AUDIO_PCM_RING_CHANNELS         2U
#define USB_AUDIO_PCM_RING_BYTES_PER_FRAME \
    (USB_AUDIO_PCM_RING_CHANNELS * sizeof(int32_t))
#define USB_AUDIO_PCM_RING_TARGET_FRAMES    144U
#define USB_AUDIO_PCM_RING_START_FRAMES     144U

extern volatile uint32_t g_usb_audio_diag_pc_underrun_count;
extern volatile uint32_t g_usb_audio_diag_pc_overflow_count;
extern volatile uint32_t g_usb_audio_diag_pc_min_fill;
extern volatile uint32_t g_usb_audio_diag_pc_max_fill;
extern volatile uint32_t g_usb_audio_diag_pc_last_requested;
extern volatile uint32_t g_usb_audio_diag_pc_last_available;
extern volatile uint32_t g_usb_audio_diag_pc_max_deficit;
extern volatile uint32_t g_usb_audio_diag_pc_write_count;
extern volatile uint32_t g_usb_audio_diag_pc_write_frames_total;
extern volatile uint32_t g_usb_audio_diag_pc_write_min_frames;
extern volatile uint32_t g_usb_audio_diag_pc_write_max_frames;
extern volatile uint32_t g_usb_audio_diag_pc_write_last_tick;
extern volatile uint32_t g_usb_audio_diag_pc_write_max_gap_ticks;
extern volatile uint32_t g_usb_audio_diag_pc_fill_before_write_min;
extern volatile uint32_t g_usb_audio_diag_pc_fill_before_write_max;
extern volatile uint32_t g_usb_audio_diag_pc_fill_after_write_min;
extern volatile uint32_t g_usb_audio_diag_pc_fill_after_write_max;
extern volatile uint32_t g_usb_audio_diag_transport_call_count;
extern volatile uint32_t g_usb_audio_diag_transport_last_tick;
extern volatile uint32_t g_usb_audio_diag_transport_max_gap_ticks;
extern volatile uint32_t g_usb_audio_diag_rx_event_count;
extern volatile uint32_t g_usb_audio_diag_rx_last_tick;
extern volatile uint32_t g_usb_audio_diag_rx_max_gap_ticks;

/*
 * Pointer-free SPSC transport contract.  The PC->BRICK ring is written by
 * USB and read by AUDIO.  The BRICK->PC ring is written by AUDIO and read by
 * USB.  The backing object is placed in the existing non-cacheable D3 IPC
 * window; a future H747 image can map the same ABI on M4/M7.
 */
uint32_t usb_audio_pcm_write_pc_to_brick(const int32_t *interleaved,
                                         uint32_t frames);
uint32_t usb_audio_pcm_read_pc_to_brick(int32_t *interleaved,
                                        uint32_t frames);
uint32_t usb_audio_pcm_write_brick_to_pc(const int32_t *interleaved,
                                         uint32_t frames);
uint32_t usb_audio_pcm_read_brick_to_pc(int32_t *interleaved,
                                        uint32_t frames);
uint32_t usb_audio_pcm_peek_brick_to_pc(int32_t *interleaved,
                                        uint32_t frames);
void usb_audio_pcm_discard_brick_to_pc(uint32_t frames);

uint32_t usb_audio_pcm_pc_to_brick_available(void);
uint32_t usb_audio_pcm_brick_to_pc_available(void);

void usb_audio_pcm_reset(void);
void usb_audio_pcm_diag_reset(void);

#endif /* USB_AUDIO_PCM_RING_H_ */
