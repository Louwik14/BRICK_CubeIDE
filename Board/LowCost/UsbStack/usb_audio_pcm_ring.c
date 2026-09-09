#include "IPC/usb_audio_pcm_ring.h"

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    int32_t samples[USB_AUDIO_PCM_RING_CAPACITY_FRAMES *
                   USB_AUDIO_PCM_RING_CHANNELS];
    volatile uint32_t write_count;
    volatile uint32_t read_count;
} usb_audio_pcm_ring_t;

typedef struct
{
    usb_audio_pcm_ring_t pc_to_brick;
    usb_audio_pcm_ring_t brick_to_pc;
    volatile uint32_t pc_to_brick_overflow_frames;
    volatile uint32_t pc_to_brick_underflow_frames;
    volatile uint32_t brick_to_pc_overflow_frames;
    volatile uint32_t brick_to_pc_underflow_frames;
} usb_audio_pcm_rings_t;

D3_IPC static usb_audio_pcm_rings_t g_usb_audio_pcm_rings;

volatile uint32_t g_usb_audio_diag_pc_underrun_count;
volatile uint32_t g_usb_audio_diag_pc_overflow_count;
volatile uint32_t g_usb_audio_diag_pc_min_fill;
volatile uint32_t g_usb_audio_diag_pc_max_fill;
volatile uint32_t g_usb_audio_diag_pc_last_requested;
volatile uint32_t g_usb_audio_diag_pc_last_available;
volatile uint32_t g_usb_audio_diag_pc_max_deficit;
volatile uint32_t g_usb_audio_diag_pc_write_count;
volatile uint32_t g_usb_audio_diag_pc_write_frames_total;
volatile uint32_t g_usb_audio_diag_pc_write_min_frames;
volatile uint32_t g_usb_audio_diag_pc_write_max_frames;
volatile uint32_t g_usb_audio_diag_pc_write_last_tick;
volatile uint32_t g_usb_audio_diag_pc_write_max_gap_ticks;
volatile uint32_t g_usb_audio_diag_pc_fill_before_write_min;
volatile uint32_t g_usb_audio_diag_pc_fill_before_write_max;
volatile uint32_t g_usb_audio_diag_pc_fill_after_write_min;
volatile uint32_t g_usb_audio_diag_pc_fill_after_write_max;
volatile uint32_t g_usb_audio_diag_transport_call_count;
volatile uint32_t g_usb_audio_diag_transport_last_tick;
volatile uint32_t g_usb_audio_diag_transport_max_gap_ticks;
volatile uint32_t g_usb_audio_diag_rx_event_count;
volatile uint32_t g_usb_audio_diag_rx_last_tick;
volatile uint32_t g_usb_audio_diag_rx_max_gap_ticks;

_Static_assert(sizeof(g_usb_audio_pcm_rings.pc_to_brick.samples) == 2304U,
               "USB Audio PC-to-BRICK payload size changed");
_Static_assert(sizeof(g_usb_audio_pcm_rings.brick_to_pc.samples) == 2304U,
               "USB Audio BRICK-to-PC payload size changed");
_Static_assert(sizeof(g_usb_audio_pcm_rings) < 8192U,
               "USB Audio rings exceed the D3 IPC budget");

static uint32_t usb_audio_pcm_available(const usb_audio_pcm_ring_t *ring)
{
    const uint32_t write_count = ring->write_count;
    const uint32_t read_count = ring->read_count;

    __DMB();
    return write_count - read_count;
}

static uint32_t usb_audio_pcm_write(usb_audio_pcm_ring_t *ring,
                                    const int32_t *interleaved,
                                    uint32_t frames,
                                    volatile uint32_t *overflow_counter,
                                    uint8_t track_pc_diag)
{
    const uint32_t write_count = ring->write_count;
    const uint32_t read_count = ring->read_count;
    uint32_t available;
    uint32_t writable;

    __DMB();
    available = write_count - read_count;
    writable = (available < USB_AUDIO_PCM_RING_CAPACITY_FRAMES)
             ? USB_AUDIO_PCM_RING_CAPACITY_FRAMES - available : 0U;
    if (frames > writable) {
        *overflow_counter += frames - writable;
        frames = writable;
        if (track_pc_diag != 0U) {
            ++g_usb_audio_diag_pc_overflow_count;
        }
    }

    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (write_count + i) % USB_AUDIO_PCM_RING_CAPACITY_FRAMES;
        ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS] =
            interleaved[i * USB_AUDIO_PCM_RING_CHANNELS];
        ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS + 1U] =
            interleaved[i * USB_AUDIO_PCM_RING_CHANNELS + 1U];
    }
    __DMB();
    ring->write_count = write_count + frames;
    if ((track_pc_diag != 0U) && (frames != 0U)) {
        const uint32_t now = DWT->CYCCNT;
        const uint32_t fill_after = available + frames;

        if (g_usb_audio_diag_pc_write_count != 0U) {
            const uint32_t gap = now - g_usb_audio_diag_pc_write_last_tick;

            if (gap > g_usb_audio_diag_pc_write_max_gap_ticks) {
                g_usb_audio_diag_pc_write_max_gap_ticks = gap;
            }
        }
        g_usb_audio_diag_pc_write_last_tick = now;
        ++g_usb_audio_diag_pc_write_count;
        g_usb_audio_diag_pc_write_frames_total += frames;
        if (frames < g_usb_audio_diag_pc_write_min_frames) {
            g_usb_audio_diag_pc_write_min_frames = frames;
        }
        if (frames > g_usb_audio_diag_pc_write_max_frames) {
            g_usb_audio_diag_pc_write_max_frames = frames;
        }
        if (available < g_usb_audio_diag_pc_fill_before_write_min) {
            g_usb_audio_diag_pc_fill_before_write_min = available;
        }
        if (available > g_usb_audio_diag_pc_fill_before_write_max) {
            g_usb_audio_diag_pc_fill_before_write_max = available;
        }
        if (fill_after < g_usb_audio_diag_pc_fill_after_write_min) {
            g_usb_audio_diag_pc_fill_after_write_min = fill_after;
        }
        if (fill_after > g_usb_audio_diag_pc_fill_after_write_max) {
            g_usb_audio_diag_pc_fill_after_write_max = fill_after;
        }
    }
    return frames;
}

static uint32_t usb_audio_pcm_read(usb_audio_pcm_ring_t *ring,
                                   int32_t *interleaved,
                                   uint32_t frames,
                                   volatile uint32_t *underflow_counter,
                                   uint8_t track_pc_diag)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_pcm_available(ring);

    if (frames > available) {
        if (underflow_counter != NULL) {
            if (track_pc_diag != 0U) {
                const uint32_t deficit = frames - available;

                ++g_usb_audio_diag_pc_underrun_count;
                g_usb_audio_diag_pc_last_requested = frames;
                g_usb_audio_diag_pc_last_available = available;
                if (deficit > g_usb_audio_diag_pc_max_deficit) {
                    g_usb_audio_diag_pc_max_deficit = deficit;
                }
                if (available < g_usb_audio_diag_pc_min_fill) {
                    g_usb_audio_diag_pc_min_fill = available;
                }
            }
            *underflow_counter += frames - available;
            return 0U;
        }
        frames = available;
    }
    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (read_count + i) % USB_AUDIO_PCM_RING_CAPACITY_FRAMES;
        interleaved[i * USB_AUDIO_PCM_RING_CHANNELS] =
            ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS];
        interleaved[i * USB_AUDIO_PCM_RING_CHANNELS + 1U] =
            ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS + 1U];
    }
    __DMB();
    ring->read_count = read_count + frames;
    if (track_pc_diag != 0U) {
        const uint32_t fill = available - frames;

        if (fill < g_usb_audio_diag_pc_min_fill) {
            g_usb_audio_diag_pc_min_fill = fill;
        }
    }
    return frames;
}

static uint32_t usb_audio_pcm_peek(const usb_audio_pcm_ring_t *ring,
                                   int32_t *interleaved,
                                   uint32_t frames)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_pcm_available(ring);

    if (frames > available) {
        frames = available;
    }
    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (read_count + i) % USB_AUDIO_PCM_RING_CAPACITY_FRAMES;
        interleaved[i * USB_AUDIO_PCM_RING_CHANNELS] =
            ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS];
        interleaved[i * USB_AUDIO_PCM_RING_CHANNELS + 1U] =
            ring->samples[index * USB_AUDIO_PCM_RING_CHANNELS + 1U];
    }
    return frames;
}

static void usb_audio_pcm_discard(usb_audio_pcm_ring_t *ring, uint32_t frames)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_pcm_available(ring);

    if (frames > available) {
        frames = available;
    }
    __DMB();
    ring->read_count = read_count + frames;
}

uint32_t usb_audio_pcm_write_pc_to_brick(const int32_t *interleaved,
                                         uint32_t frames)
{
    uint32_t written;
    uint32_t fill;

    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    written = usb_audio_pcm_write(
        &g_usb_audio_pcm_rings.pc_to_brick, interleaved, frames,
        &g_usb_audio_pcm_rings.pc_to_brick_overflow_frames, 1U);
    fill = usb_audio_pcm_available(&g_usb_audio_pcm_rings.pc_to_brick);
    if (fill > g_usb_audio_diag_pc_max_fill) {
        g_usb_audio_diag_pc_max_fill = fill;
    }
    if (fill < g_usb_audio_diag_pc_min_fill) {
        g_usb_audio_diag_pc_min_fill = fill;
    }
    return written;
}

uint32_t usb_audio_pcm_read_pc_to_brick(int32_t *interleaved,
                                        uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_read(&g_usb_audio_pcm_rings.pc_to_brick,
                              interleaved, frames,
                              &g_usb_audio_pcm_rings.pc_to_brick_underflow_frames,
                              1U);
}

uint32_t usb_audio_pcm_write_brick_to_pc(const int32_t *interleaved,
                                         uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_write(&g_usb_audio_pcm_rings.brick_to_pc,
                               interleaved, frames,
                               &g_usb_audio_pcm_rings.brick_to_pc_overflow_frames,
                               0U);
}

uint32_t usb_audio_pcm_read_brick_to_pc(int32_t *interleaved,
                                        uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_read(&g_usb_audio_pcm_rings.brick_to_pc,
                              interleaved, frames, NULL, 0U);
}

uint32_t usb_audio_pcm_peek_brick_to_pc(int32_t *interleaved,
                                        uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_peek(&g_usb_audio_pcm_rings.brick_to_pc,
                              interleaved, frames);
}

void usb_audio_pcm_discard_brick_to_pc(uint32_t frames)
{
    if (frames != 0U) {
        usb_audio_pcm_discard(&g_usb_audio_pcm_rings.brick_to_pc, frames);
    }
}

uint32_t usb_audio_pcm_pc_to_brick_available(void)
{
    return usb_audio_pcm_available(&g_usb_audio_pcm_rings.pc_to_brick);
}

uint32_t usb_audio_pcm_brick_to_pc_available(void)
{
    return usb_audio_pcm_available(&g_usb_audio_pcm_rings.brick_to_pc);
}

void usb_audio_pcm_reset(void)
{
    g_usb_audio_pcm_rings.pc_to_brick.write_count = 0U;
    g_usb_audio_pcm_rings.pc_to_brick.read_count = 0U;
    g_usb_audio_pcm_rings.brick_to_pc.write_count = 0U;
    g_usb_audio_pcm_rings.brick_to_pc.read_count = 0U;
}

void usb_audio_pcm_diag_reset(void)
{
    g_usb_audio_diag_pc_underrun_count = 0U;
    g_usb_audio_diag_pc_overflow_count = 0U;
    g_usb_audio_diag_pc_min_fill = UINT32_MAX;
    g_usb_audio_diag_pc_max_fill = 0U;
    g_usb_audio_diag_pc_last_requested = 0U;
    g_usb_audio_diag_pc_last_available = 0U;
    g_usb_audio_diag_pc_max_deficit = 0U;
    g_usb_audio_diag_pc_write_count = 0U;
    g_usb_audio_diag_pc_write_frames_total = 0U;
    g_usb_audio_diag_pc_write_min_frames = UINT32_MAX;
    g_usb_audio_diag_pc_write_max_frames = 0U;
    g_usb_audio_diag_pc_write_last_tick = 0U;
    g_usb_audio_diag_pc_write_max_gap_ticks = 0U;
    g_usb_audio_diag_pc_fill_before_write_min = UINT32_MAX;
    g_usb_audio_diag_pc_fill_before_write_max = 0U;
    g_usb_audio_diag_pc_fill_after_write_min = UINT32_MAX;
    g_usb_audio_diag_pc_fill_after_write_max = 0U;
    g_usb_audio_diag_transport_call_count = 0U;
    g_usb_audio_diag_transport_last_tick = 0U;
    g_usb_audio_diag_transport_max_gap_ticks = 0U;
    g_usb_audio_diag_rx_event_count = 0U;
    g_usb_audio_diag_rx_last_tick = 0U;
    g_usb_audio_diag_rx_max_gap_ticks = 0U;
}
