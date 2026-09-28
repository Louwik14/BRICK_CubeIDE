#ifndef BRICK6_USB_AUDIO_H_
#define BRICK6_USB_AUDIO_H_

#include <stdint.h>

#define USB_AUDIO_DIAG_TRACE_CAPACITY 64U

enum {
    USB_AUDIO_DIAG_RX = 1U,
    USB_AUDIO_DIAG_DROP = 2U,
    USB_AUDIO_DIAG_UNDERFLOW = 3U,
    USB_AUDIO_DIAG_FEEDBACK = 4U,
    USB_AUDIO_DIAG_RING_RESET = 5U
};

typedef struct {
    uint32_t tick_ms;
    uint32_t event;
    uint32_t fill_frames;
    uint32_t value;
} usb_audio_diag_event_t;

typedef struct {
    uint32_t usb_rx_packets;
    uint32_t usb_rx_frames;
    uint32_t usb_rx_gap_max; /* ms */
    uint32_t ring_fill_current; /* frames */
    uint32_t ring_fill_min; /* frames */
    uint32_t ring_fill_max; /* frames */
    uint32_t ring_write_dropped_frames;
    uint32_t audio_read_underflow_count;
    uint32_t audio_zero_block_count;
    uint32_t feedback_current; /* 16.16 frames/ms */
    uint32_t feedback_min;
    uint32_t feedback_max;
    uint32_t feedback_update_count;
    uint32_t ring_reset_count;
    uint32_t usb_service_gap_max; /* ms */
    uint32_t usb_rx_last_tick_ms;
    uint32_t usb_service_last_tick_ms;
    uint32_t ring_fill_seen;
    uint32_t feedback_seen;
    uint32_t usb_rx_seen;
    uint32_t usb_service_seen;
    uint32_t usb_rx_fifo_failed_packets;
    uint32_t usb_rx_fifo_failed_frames;
    uint32_t feedback_last_traced;
    uint32_t feedback_last_trace_tick_ms;
    uint32_t trace_head; /* monotonic; next slot = trace_head % capacity */
    uint32_t trace_count; /* saturates at capacity */
    usb_audio_diag_event_t trace[USB_AUDIO_DIAG_TRACE_CAPACITY];
} usb_audio_diag_t;

extern volatile usb_audio_diag_t g_usb_audio_diag;
void usb_audio_diag_reset(void);

void usb_audio_transport_reset(void);
void usb_audio_transport_process(void);
void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting);
void usb_audio_transport_close_interface(uint8_t interface_number);

uint32_t usb_audio_audio_read(float *left, float *right, uint32_t frames);
uint32_t usb_audio_audio_write(const float *left,
                               const float *right,
                               uint32_t frames);

#endif /* BRICK6_USB_AUDIO_H */
