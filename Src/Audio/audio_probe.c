#include "Audio/audio_probe.h"

#include <string.h>
#include "Audio/audio_float.h"
#include "stm32h7xx.h"

#define AUDIO_PROBE_USB_CAPACITY 384U
#define AUDIO_PROBE_USB_PRIME 144U

/* A separate short SPSC queue is required because USB OUT and AUDIO run in
 * different interrupts. Only USB RX mode writes it. It never feeds the engine. */
static float g_usb_l[AUDIO_PROBE_USB_CAPACITY];
static float g_usb_r[AUDIO_PROBE_USB_CAPACITY];
static volatile uint32_t g_usb_write;
static volatile uint32_t g_usb_read;
static uint8_t g_usb_ready;
static audio_probe_mode_t g_last_output_mode;

static float g_tap_l[AUDIO_BLOCK_SIZE];
static float g_tap_r[AUDIO_BLOCK_SIZE];
static const float g_silence[AUDIO_BLOCK_SIZE];
static uint32_t g_tap_frames;
static audio_probe_mode_t g_tap_point;
static volatile uint8_t g_mode;

void audio_probe_set_mode(audio_probe_mode_t mode)
{
    if ((uint32_t)mode < AUDIO_PROBE_MODE_COUNT) g_mode = (uint8_t)mode;
}

audio_probe_mode_t audio_probe_get_mode(void)
{
    const uint8_t mode = g_mode;
    return (mode < AUDIO_PROBE_MODE_COUNT) ? (audio_probe_mode_t)mode
                                            : AUDIO_PROBE_OFF;
}

void audio_probe_usb_receive(const float *interleaved, uint32_t frames)
{
    if ((g_mode != AUDIO_PROBE_USB_RX) || (interleaved == 0)) return;
    const uint32_t write = g_usb_write;
    const uint32_t read = g_usb_read;
    if (frames > AUDIO_PROBE_USB_CAPACITY - (write - read)) return;
    for (uint32_t n = 0U; n < frames; ++n) {
        const uint32_t slot = (write + n) % AUDIO_PROBE_USB_CAPACITY;
        g_usb_l[slot] = interleaved[2U * n];
        g_usb_r[slot] = interleaved[2U * n + 1U];
    }
    __DMB();
    g_usb_write = write + frames;
}

void audio_probe_usb_reset(void)
{
    g_usb_write = 0U;
    g_usb_read = 0U;
    g_usb_ready = 0U;
}

void audio_probe_begin_segment(void)
{
    g_tap_frames = 0U;
    g_tap_point = AUDIO_PROBE_OFF;
}

void audio_probe_capture(audio_probe_mode_t point, const float *left,
                         const float *right, uint32_t frames)
{
    if ((g_mode != (uint8_t)point) || (frames > AUDIO_BLOCK_SIZE)
            || (left == 0) || (right == 0)) return;
    memcpy(g_tap_l, left, frames * sizeof(float));
    memcpy(g_tap_r, right, frames * sizeof(float));
    g_tap_point = point;
    g_tap_frames = frames;
}

static void audio_probe_usb_read(uint32_t frames)
{
    const uint32_t write = g_usb_write;
    const uint32_t read = g_usb_read;
    __DMB();
    const uint32_t available = write - read;
    if (g_usb_ready == 0U) {
        if (available < AUDIO_PROBE_USB_PRIME) {
            memcpy(g_tap_l, g_silence, frames * sizeof(float));
            memcpy(g_tap_r, g_silence, frames * sizeof(float));
            return;
        }
        g_usb_ready = 1U;
    }
    if (available < frames) {
        g_usb_ready = 0U;
        memcpy(g_tap_l, g_silence, frames * sizeof(float));
        memcpy(g_tap_r, g_silence, frames * sizeof(float));
        return;
    }
    for (uint32_t n = 0U; n < frames; ++n) {
        const uint32_t slot = (read + n) % AUDIO_PROBE_USB_CAPACITY;
        g_tap_l[n] = g_usb_l[slot];
        g_tap_r[n] = g_usb_r[slot];
    }
    __DMB();
    g_usb_read = read + frames;
}

void audio_probe_select_output(const float *main_l, const float *main_r,
                               const float *master_l, const float *master_r,
                               uint32_t frames, const float **out_l,
                               const float **out_r)
{
    const audio_probe_mode_t mode = audio_probe_get_mode();
    *out_l = master_l;
    *out_r = master_r;
    if (mode != g_last_output_mode) {
        if (mode == AUDIO_PROBE_USB_RX) {
            g_usb_read = g_usb_write;
            g_usb_ready = 0U;
        }
        g_last_output_mode = mode;
    }
    if (mode == AUDIO_PROBE_OFF || mode == AUDIO_PROBE_MASTER) return;
    if (mode == AUDIO_PROBE_MAIN) {
        *out_l = main_l;
        *out_r = main_r;
    } else if (mode == AUDIO_PROBE_USB_RX) {
        audio_probe_usb_read(frames);
        *out_l = g_tap_l;
        *out_r = g_tap_r;
    } else if ((g_tap_point == mode) && (g_tap_frames == frames)) {
        *out_l = g_tap_l;
        *out_r = g_tap_r;
    } else {
        *out_l = g_silence;
        *out_r = g_silence;
    }
}
