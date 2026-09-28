#pragma once

#include <stdint.h>

typedef enum {
    AUDIO_PROBE_OFF = 0,
    AUDIO_PROBE_USB_RX,
    AUDIO_PROBE_RING_OUT,
    AUDIO_PROBE_MIX_INPUT,
    AUDIO_PROBE_MAIN,
    AUDIO_PROBE_MASTER,
    AUDIO_PROBE_MODE_COUNT
} audio_probe_mode_t;

/* One-byte UI -> AUDIO selection. The normal render always runs to completion. */
void audio_probe_set_mode(audio_probe_mode_t mode);
audio_probe_mode_t audio_probe_get_mode(void);

/* USB IRQ: tap the converted packet before the normal ring write. */
void audio_probe_usb_receive(const float *interleaved, uint32_t frames);
void audio_probe_usb_reset(void);

/* AUDIO IRQ: one selected tap per render segment, then the final output mux. */
void audio_probe_begin_segment(void);
void audio_probe_capture(audio_probe_mode_t point, const float *left,
                         const float *right, uint32_t frames);
void audio_probe_select_output(const float *main_l, const float *main_r,
                               const float *master_l, const float *master_r,
                               uint32_t frames, const float **out_l,
                               const float **out_r);
