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
                                    volatile uint32_t *overflow_counter)
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
    return frames;
}

static uint32_t usb_audio_pcm_read(usb_audio_pcm_ring_t *ring,
                                   int32_t *interleaved,
                                   uint32_t frames,
                                   volatile uint32_t *underflow_counter)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_pcm_available(ring);

    if (frames > available) {
        if (underflow_counter != NULL) {
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
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_write(&g_usb_audio_pcm_rings.pc_to_brick,
                               interleaved, frames,
                               &g_usb_audio_pcm_rings.pc_to_brick_overflow_frames);
}

uint32_t usb_audio_pcm_read_pc_to_brick(int32_t *interleaved,
                                        uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_read(&g_usb_audio_pcm_rings.pc_to_brick,
                              interleaved, frames,
                              &g_usb_audio_pcm_rings.pc_to_brick_underflow_frames);
}

uint32_t usb_audio_pcm_write_brick_to_pc(const int32_t *interleaved,
                                         uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_write(&g_usb_audio_pcm_rings.brick_to_pc,
                               interleaved, frames,
                               &g_usb_audio_pcm_rings.brick_to_pc_overflow_frames);
}

uint32_t usb_audio_pcm_read_brick_to_pc(int32_t *interleaved,
                                        uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_read(&g_usb_audio_pcm_rings.brick_to_pc,
                              interleaved, frames, NULL);
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
