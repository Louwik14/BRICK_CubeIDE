#include "IPC/usb_audio_float_ring.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    float samples[USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES *
                  USB_AUDIO_FLOAT_RING_CHANNELS];
    volatile uint32_t write_count;
    volatile uint32_t read_count;
} usb_audio_float_ring_t;

typedef struct
{
    usb_audio_float_ring_t pc_to_brick;
    usb_audio_float_ring_t brick_to_pc;
} usb_audio_float_rings_t;

D3_IPC static usb_audio_float_rings_t g_usb_audio_float_rings;

_Static_assert(sizeof(float) == 4U,
               "USB Audio float ring requires 32-bit float");
_Static_assert(sizeof(g_usb_audio_float_rings.pc_to_brick.samples) == 2304U,
               "USB Audio PC-to-BRICK payload size changed");
_Static_assert(sizeof(g_usb_audio_float_rings.brick_to_pc.samples) == 2304U,
               "USB Audio BRICK-to-PC payload size changed");
_Static_assert(sizeof(g_usb_audio_float_rings) < 8192U,
               "USB Audio rings exceed the D3 IPC budget");

static uint32_t usb_audio_float_available(const usb_audio_float_ring_t *ring)
{
    const uint32_t write_count = ring->write_count;
    const uint32_t read_count = ring->read_count;

    __DMB();
    return write_count - read_count;
}

static inline float usb_audio_float_sanitize(float sample)
{
    uint32_t bits;

    memcpy(&bits, &sample, sizeof(bits));
    return ((bits & 0x7F800000UL) != 0x7F800000UL) ? sample : 0.0f;
}

static uint32_t usb_audio_float_write_pc(usb_audio_float_ring_t *ring,
                                         const float *interleaved,
                                         uint32_t frames)
{
    const uint32_t write_count = ring->write_count;
    const uint32_t read_count = ring->read_count;
    uint32_t available;
    uint32_t writable;

    __DMB();
    available = write_count - read_count;
    writable = (available < USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES)
             ? USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES - available : 0U;
    if (frames > writable) {
        frames = writable;
    }

    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (write_count + i) % USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES;
        ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS] =
            usb_audio_float_sanitize(interleaved[i * USB_AUDIO_FLOAT_RING_CHANNELS]);
        ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS + 1U] =
            usb_audio_float_sanitize(interleaved[i * USB_AUDIO_FLOAT_RING_CHANNELS + 1U]);
    }
    __DMB();
    ring->write_count = write_count + frames;
    return frames;
}

static uint32_t usb_audio_float_read_pc(usb_audio_float_ring_t *ring,
                                        float *left,
                                        float *right,
                                        uint32_t frames)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_float_available(ring);

    if (frames > available) {
        return 0U;
    }
    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (read_count + i) % USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES;
        left[i] = ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS];
        right[i] = ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS + 1U];
    }
    __DMB();
    ring->read_count = read_count + frames;
    return frames;
}

static uint32_t usb_audio_float_write_brick(usb_audio_float_ring_t *ring,
                                            const float *left,
                                            const float *right,
                                            uint32_t frames)
{
    const uint32_t write_count = ring->write_count;
    const uint32_t read_count = ring->read_count;
    uint32_t available;
    uint32_t writable;

    __DMB();
    available = write_count - read_count;
    writable = (available < USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES)
             ? USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES - available : 0U;
    if (frames > writable) {
        frames = writable;
    }

    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (write_count + i) % USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES;
        ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS] = left[i];
        ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS + 1U] = right[i];
    }
    __DMB();
    ring->write_count = write_count + frames;
    return frames;
}

static uint32_t usb_audio_float_peek(const usb_audio_float_ring_t *ring,
                                     float *interleaved,
                                     uint32_t frames)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_float_available(ring);

    if (frames > available) {
        frames = available;
    }
    for (uint32_t i = 0U; i < frames; ++i) {
        const uint32_t index = (read_count + i) % USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES;
        interleaved[i * USB_AUDIO_FLOAT_RING_CHANNELS] =
            ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS];
        interleaved[i * USB_AUDIO_FLOAT_RING_CHANNELS + 1U] =
            ring->samples[index * USB_AUDIO_FLOAT_RING_CHANNELS + 1U];
    }
    return frames;
}

static void usb_audio_float_discard(usb_audio_float_ring_t *ring,
                                    uint32_t frames)
{
    const uint32_t read_count = ring->read_count;
    const uint32_t available = usb_audio_float_available(ring);

    if (frames > available) {
        frames = available;
    }
    __DMB();
    ring->read_count = read_count + frames;
}

uint32_t usb_audio_float_write_pc_to_brick(const float *interleaved,
                                           uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_float_write_pc(&g_usb_audio_float_rings.pc_to_brick,
                                    interleaved, frames);
}

uint32_t usb_audio_float_read_pc_to_brick(float *left,
                                          float *right,
                                          uint32_t frames)
{
    if ((left == NULL) || (right == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_float_read_pc(&g_usb_audio_float_rings.pc_to_brick,
                                   left, right, frames);
}

uint32_t usb_audio_float_write_brick_to_pc(const float *left,
                                           const float *right,
                                           uint32_t frames)
{
    if ((left == NULL) || (right == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_float_write_brick(&g_usb_audio_float_rings.brick_to_pc,
                                       left, right, frames);
}

uint32_t usb_audio_float_peek_brick_to_pc(float *interleaved,
                                          uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)) {
        return 0U;
    }
    return usb_audio_float_peek(&g_usb_audio_float_rings.brick_to_pc,
                                interleaved, frames);
}

void usb_audio_float_discard_brick_to_pc(uint32_t frames)
{
    if (frames != 0U) {
        usb_audio_float_discard(&g_usb_audio_float_rings.brick_to_pc, frames);
    }
}

uint32_t usb_audio_float_pc_to_brick_available(void)
{
    return usb_audio_float_available(&g_usb_audio_float_rings.pc_to_brick);
}

uint32_t usb_audio_float_brick_to_pc_available(void)
{
    return usb_audio_float_available(&g_usb_audio_float_rings.brick_to_pc);
}

void usb_audio_float_reset(void)
{
    g_usb_audio_float_rings.pc_to_brick.write_count = 0U;
    g_usb_audio_float_rings.pc_to_brick.read_count = 0U;
    g_usb_audio_float_rings.brick_to_pc.write_count = 0U;
    g_usb_audio_float_rings.brick_to_pc.read_count = 0U;
}
