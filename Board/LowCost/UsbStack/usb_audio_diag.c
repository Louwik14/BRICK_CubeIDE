#include "Debug/usb_audio_diag.h"

#include <string.h>

#include "IPC/usb_audio_float_ring.h"
#include "Platform/brick_media_clock.h"
#include "Platform/memory_layout.h"

#define USB_AUDIO_DIAG_VERSION 1U
#define USB_AUDIO_DIAG_UNSET_MIN UINT32_MAX

/* The recorder arena is an existing shareable, non-cacheable SDRAM MPU
 * region.  Diagnostics therefore remain visible to a halted debugger without
 * cache maintenance and do not consume DTCM or audio DMA storage. */
AUDIO_STORAGE_SHARED_SDRAM volatile usb_audio_diag_t g_usb_audio_diag;
AUDIO_STORAGE_SHARED_SDRAM volatile usb_audio_diag_event_t
    g_usb_audio_diag_usb_trace[USB_AUDIO_DIAG_TRACE_CAPACITY];
AUDIO_STORAGE_SHARED_SDRAM volatile usb_audio_diag_event_t
    g_usb_audio_diag_audio_trace[USB_AUDIO_DIAG_TRACE_CAPACITY];

static void diag_max(volatile uint32_t *value, uint32_t candidate)
{
    if (candidate > *value) *value = candidate;
}

static void diag_min(volatile uint32_t *value, uint32_t candidate)
{
    if (candidate < *value) *value = candidate;
}

static void diag_ring_fill(uint32_t fill)
{
    g_usb_audio_diag.ring_fill_current = fill;
    diag_min(&g_usb_audio_diag.ring_fill_min, fill);
    diag_max(&g_usb_audio_diag.ring_fill_max, fill);
}

static void diag_trace(volatile usb_audio_diag_event_t *trace,
                       volatile uint32_t *write, uint32_t tick,
                       uint16_t event, uint16_t arg0,
                       uint32_t arg1, uint32_t arg2)
{
    const uint32_t sequence = *write;
    volatile usb_audio_diag_event_t *entry =
        &trace[sequence % USB_AUDIO_DIAG_TRACE_CAPACITY];
    entry->tick = tick;
    entry->event = event;
    entry->arg0 = arg0;
    entry->arg1 = arg1;
    entry->arg2 = arg2;
    *write = sequence + 1U;
}

void usb_audio_diag_reset(void)
{
    memset((void *)&g_usb_audio_diag, 0, sizeof(g_usb_audio_diag));
    memset((void *)g_usb_audio_diag_usb_trace, 0,
           sizeof(g_usb_audio_diag_usb_trace));
    memset((void *)g_usb_audio_diag_audio_trace, 0,
           sizeof(g_usb_audio_diag_audio_trace));
    g_usb_audio_diag.version = USB_AUDIO_DIAG_VERSION;
    g_usb_audio_diag.reset_tick = brick_media_clock_now_tick();
    g_usb_audio_diag.ring_fill_min = USB_AUDIO_DIAG_UNSET_MIN;
    g_usb_audio_diag.usb_rx_bytes_min = USB_AUDIO_DIAG_UNSET_MIN;
    g_usb_audio_diag.feedback_min = USB_AUDIO_DIAG_UNSET_MIN;
}

void usb_audio_diag_usb_rx(uint32_t tick, uint16_t bytes, uint32_t frames,
                           uint32_t written, uint32_t fill,
                           uint32_t write_count, uint32_t read_count,
                           uint8_t short_read, uint8_t unaligned)
{
    const uint32_t previous_tick = g_usb_audio_diag.usb_rx_last_tick;
    g_usb_audio_diag.usb_rx_packets++;
    g_usb_audio_diag.usb_rx_bytes += bytes;
    g_usb_audio_diag.usb_rx_frames += frames;
    g_usb_audio_diag.usb_rx_frames_written += written;
    g_usb_audio_diag.usb_rx_frames_refused += frames - written;
    if (written < frames) g_usb_audio_diag.usb_rx_overflow_count++;
    if (short_read != 0U) g_usb_audio_diag.usb_rx_short_read_count++;
    if (unaligned != 0U) g_usb_audio_diag.usb_rx_unaligned_count++;
    if (bytes == 376U) g_usb_audio_diag.usb_rx_bytes_376++;
    else if (bytes == 384U) g_usb_audio_diag.usb_rx_bytes_384++;
    else if (bytes == 392U) g_usb_audio_diag.usb_rx_bytes_392++;
    else g_usb_audio_diag.usb_rx_bytes_other++;
    diag_min(&g_usb_audio_diag.usb_rx_bytes_min, bytes);
    diag_max(&g_usb_audio_diag.usb_rx_bytes_max, bytes);
    if (previous_tick != 0U) diag_max(&g_usb_audio_diag.usb_rx_gap_max,
                                     tick - previous_tick);
    g_usb_audio_diag.usb_rx_last_tick = tick;
    g_usb_audio_diag.ring_write_count = write_count;
    g_usb_audio_diag.ring_read_count = read_count;
    diag_ring_fill(fill);
    diag_trace(g_usb_audio_diag_usb_trace, &g_usb_audio_diag.usb_trace_write,
               tick, USB_AUDIO_DIAG_EVENT_USB_RX, bytes,
               (frames << 16) | written, fill);
}

void usb_audio_diag_audio_read(uint32_t tick, uint32_t requested,
                               uint32_t available_before, uint32_t read,
                               uint32_t available_after, uint8_t ready_before,
                               uint8_t ready_after, uint8_t zero_block,
                               uint32_t write_count, uint32_t read_count)
{
    g_usb_audio_diag.audio_read_calls++;
    g_usb_audio_diag.audio_frames_requested += requested;
    g_usb_audio_diag.audio_frames_read += read;
    g_usb_audio_diag.audio_last_requested = requested;
    g_usb_audio_diag.audio_last_available_before = available_before;
    g_usb_audio_diag.audio_last_available_after = available_after;
    g_usb_audio_diag.audio_last_read = read;
    g_usb_audio_diag.audio_ready = ready_after;
    if ((ready_before != 0U) && (ready_after == 0U))
        g_usb_audio_diag.audio_ready_drop_count++;
    if (read < requested) g_usb_audio_diag.audio_underflow_count++;
    if (zero_block != 0U) g_usb_audio_diag.audio_zero_blocks++;
    if (available_before < USB_AUDIO_FLOAT_RING_START_FRAMES)
        g_usb_audio_diag.ring_below_start_count++;
    g_usb_audio_diag.ring_write_count = write_count;
    g_usb_audio_diag.ring_read_count = read_count;
    diag_ring_fill(available_after);
    diag_trace(g_usb_audio_diag_audio_trace,
               &g_usb_audio_diag.audio_trace_write, tick,
               USB_AUDIO_DIAG_EVENT_AUDIO_READ,
               (uint16_t)((ready_before != 0U ? 1U : 0U)
                          | (ready_after != 0U ? 2U : 0U)
                          | (zero_block != 0U ? 4U : 0U)),
               (requested << 16) | read,
               (available_before << 16) | available_after);
}

void usb_audio_diag_feedback(uint32_t fill, int32_t error, uint32_t feedback)
{
    g_usb_audio_diag.feedback_updates++;
    g_usb_audio_diag.feedback_current = feedback;
    g_usb_audio_diag.feedback_error_current = error;
    g_usb_audio_diag.feedback_fill_current = fill;
    diag_min(&g_usb_audio_diag.feedback_min, feedback);
    diag_max(&g_usb_audio_diag.feedback_max, feedback);
}

void usb_audio_diag_usb_service(uint32_t tick)
{
    const uint32_t previous_tick = g_usb_audio_diag.usb_service_last_tick;
    g_usb_audio_diag.usb_service_count++;
    if (previous_tick != 0U) diag_max(&g_usb_audio_diag.usb_service_gap_max,
                                     tick - previous_tick);
    g_usb_audio_diag.usb_service_last_tick = tick;
}

void usb_audio_diag_ring_reset(uint32_t tick)
{
    g_usb_audio_diag.ring_reset_count++;
    diag_trace(g_usb_audio_diag_usb_trace, &g_usb_audio_diag.usb_trace_write,
               tick, USB_AUDIO_DIAG_EVENT_RING_RESET, 0U, 0U, 0U);
}

void usb_audio_diag_set_interface(uint32_t tick, uint8_t interface_number,
                                  uint8_t alternate_setting)
{
    diag_trace(g_usb_audio_diag_usb_trace, &g_usb_audio_diag.usb_trace_write,
               tick, USB_AUDIO_DIAG_EVENT_SET_INTERFACE,
               (uint16_t)(((uint16_t)interface_number << 8)
                          | alternate_setting), 0U, 0U);
}

void usb_audio_diag_audio_callback(uint32_t tick, uint8_t half,
                                   uint8_t accepted, uint8_t recovering)
{
    const uint32_t previous_tick = g_usb_audio_diag.audio_block_last_tick;
    g_usb_audio_diag.audio_callback_count++;
    if (half != g_usb_audio_diag.audio_expected_half)
        g_usb_audio_diag.audio_half_mismatch_count++;
    if (accepted == 0U) {
        g_usb_audio_diag.audio_callback_rejected++;
        return;
    }
    g_usb_audio_diag.audio_block_count++;
    if (recovering != 0U) g_usb_audio_diag.audio_recovering_count++;
    if (previous_tick != 0U) diag_max(&g_usb_audio_diag.audio_block_gap_max,
                                     tick - previous_tick);
    g_usb_audio_diag.audio_block_last_tick = tick;
    g_usb_audio_diag.audio_last_half = half;
    g_usb_audio_diag.audio_expected_half = half ^ 1U;
    diag_trace(g_usb_audio_diag_audio_trace,
               &g_usb_audio_diag.audio_trace_write, tick,
               USB_AUDIO_DIAG_EVENT_AUDIO_BLOCK,
               (uint16_t)(half | (recovering != 0U ? 2U : 0U)), 0U, 0U);
}
