#ifndef USB_AUDIO_DIAG_H_
#define USB_AUDIO_DIAG_H_

#include <stdint.h>

#define USB_AUDIO_DIAG_TRACE_CAPACITY 1024U

typedef enum {
    USB_AUDIO_DIAG_EVENT_USB_RX = 1,
    USB_AUDIO_DIAG_EVENT_AUDIO_READ = 2,
    USB_AUDIO_DIAG_EVENT_AUDIO_BLOCK = 3,
    USB_AUDIO_DIAG_EVENT_RING_RESET = 4,
    USB_AUDIO_DIAG_EVENT_SET_INTERFACE = 5
} usb_audio_diag_event_id_t;

typedef struct {
    uint32_t tick;
    uint16_t event;
    uint16_t arg0;
    uint32_t arg1;
    uint32_t arg2;
} usb_audio_diag_event_t;

typedef struct {
    uint32_t version;
    uint32_t reset_tick;
    uint32_t usb_rx_packets;
    uint32_t usb_rx_bytes;
    uint32_t usb_rx_frames;
    uint32_t usb_rx_frames_written;
    uint32_t usb_rx_frames_refused;
    uint32_t usb_rx_overflow_count;
    uint32_t usb_rx_unaligned_count;
    uint32_t usb_rx_short_read_count;
    uint32_t usb_rx_bytes_376;
    uint32_t usb_rx_bytes_384;
    uint32_t usb_rx_bytes_392;
    uint32_t usb_rx_bytes_other;
    uint32_t usb_rx_bytes_min;
    uint32_t usb_rx_bytes_max;
    uint32_t usb_rx_last_tick;
    uint32_t usb_rx_gap_max;
    uint32_t ring_write_count;
    uint32_t ring_read_count;
    uint32_t ring_fill_current;
    uint32_t ring_fill_min;
    uint32_t ring_fill_max;
    uint32_t ring_below_start_count;
    uint32_t ring_reset_count;
    uint32_t audio_read_calls;
    uint32_t audio_frames_requested;
    uint32_t audio_frames_read;
    uint32_t audio_last_requested;
    uint32_t audio_last_available_before;
    uint32_t audio_last_available_after;
    uint32_t audio_last_read;
    uint32_t audio_ready;
    uint32_t audio_ready_drop_count;
    uint32_t audio_underflow_count;
    uint32_t audio_zero_blocks;
    uint32_t feedback_updates;
    uint32_t feedback_current;
    uint32_t feedback_min;
    uint32_t feedback_max;
    int32_t feedback_error_current;
    uint32_t feedback_fill_current;
    uint32_t usb_service_count;
    uint32_t usb_service_last_tick;
    uint32_t usb_service_gap_max;
    uint32_t audio_callback_count;
    uint32_t audio_block_count;
    uint32_t audio_callback_rejected;
    uint32_t audio_half_mismatch_count;
    uint32_t audio_recovering_count;
    uint32_t audio_last_half;
    uint32_t audio_expected_half;
    uint32_t audio_block_last_tick;
    uint32_t audio_block_gap_max;
    uint32_t usb_trace_write;
    uint32_t audio_trace_write;
} usb_audio_diag_t;

extern volatile usb_audio_diag_t g_usb_audio_diag;
extern volatile usb_audio_diag_event_t
    g_usb_audio_diag_usb_trace[USB_AUDIO_DIAG_TRACE_CAPACITY];
extern volatile usb_audio_diag_event_t
    g_usb_audio_diag_audio_trace[USB_AUDIO_DIAG_TRACE_CAPACITY];

void usb_audio_diag_reset(void);
void usb_audio_diag_usb_rx(uint32_t tick, uint16_t bytes, uint32_t frames,
                           uint32_t written, uint32_t fill,
                           uint32_t write_count, uint32_t read_count,
                           uint8_t short_read, uint8_t unaligned);
void usb_audio_diag_audio_read(uint32_t tick, uint32_t requested,
                               uint32_t available_before, uint32_t read,
                               uint32_t available_after, uint8_t ready_before,
                               uint8_t ready_after, uint8_t zero_block,
                               uint32_t write_count, uint32_t read_count);
void usb_audio_diag_feedback(uint32_t fill, int32_t error, uint32_t feedback);
void usb_audio_diag_usb_service(uint32_t tick);
void usb_audio_diag_ring_reset(uint32_t tick);
void usb_audio_diag_set_interface(uint32_t tick, uint8_t interface_number,
                                  uint8_t alternate_setting);
void usb_audio_diag_audio_callback(uint32_t tick, uint8_t half,
                                   uint8_t accepted, uint8_t recovering);

#endif
