#ifndef PRISM_DEBUG_BOOT_H
#define PRISM_DEBUG_BOOT_H

#include <stdint.h>

#include "ui_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_PRISM_DEBUG_BOOT 1U
#define PRISM_DEBUG_TRACK_COUNT 8U
#define PRISM_DEBUG_SAMPLE_RATE 48000U
#define PRISM_DEBUG_CAPTURE_SECONDS 2U
#define PRISM_DEBUG_RING_FRAMES (PRISM_DEBUG_SAMPLE_RATE * PRISM_DEBUG_CAPTURE_SECONDS)

typedef enum
{
    PRISM_DEBUG_PROBE_P6 = 0,
    PRISM_DEBUG_PROBE_P5,
    PRISM_DEBUG_PROBE_P4,
    PRISM_DEBUG_PROBE_P3,
    PRISM_DEBUG_PROBE_P2,
    PRISM_DEBUG_PROBE_COUNT
} prism_debug_probe_t;

void prism_debug_boot_init(void);
void prism_debug_boot_service(void);
uint8_t prism_debug_boot_is_active(void);

void prism_debug_boot_begin_block(uint32_t frames);
void prism_debug_boot_set_render_track(uint8_t track);
uint8_t prism_debug_boot_get_render_track(void);
void prism_debug_boot_capture_p6(uint8_t track, const float *mono, uint32_t frames);
void prism_debug_boot_capture_p5(uint8_t track, const float *mono, uint32_t frames);
void prism_debug_boot_capture_p4_sample(uint8_t track, uint32_t offset, float sample);
void prism_debug_boot_capture_p3_sample(uint8_t track, uint32_t offset, float sample);
void prism_debug_boot_capture_p2(uint8_t track,
                                 const int16_t *native,
                                 uint32_t offset,
                                 uint32_t frames);
void prism_debug_boot_end_block(uint32_t frames);

void prism_debug_boot_request_probe(prism_debug_probe_t probe);
prism_debug_probe_t prism_debug_boot_get_probe(void);
const char *prism_debug_boot_probe_name(prism_debug_probe_t probe);
const char *prism_debug_boot_probe_label(prism_debug_probe_t probe);
uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t prism_debug_boot_handle_event(const ui_event_t *event);
void prism_debug_boot_render(void);

#ifdef __cplusplus
}
#endif

#endif /* PRISM_DEBUG_BOOT_H */
