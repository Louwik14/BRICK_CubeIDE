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
#define PRISM_DEBUG_TEST_SECONDS 6U
#define PRISM_DEBUG_TEST_FRAMES (PRISM_DEBUG_SAMPLE_RATE * PRISM_DEBUG_TEST_SECONDS)

void prism_debug_boot_init(void);
void prism_debug_boot_start_test(void);
void prism_debug_boot_service(void);
uint8_t prism_debug_boot_is_active(void);

/* Called only by the audio runtime/IRQ path; no storage or logging occurs here. */
void prism_debug_boot_audio_block_begin(uint32_t frames);
void prism_debug_boot_audio_prism_tracks(uint8_t tracks);
void prism_debug_boot_audio_half_complete(uint8_t half_index, uint32_t frames);

/* The encoder is consumed while the temporary page is active, but has no mode. */
uint8_t prism_debug_boot_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t prism_debug_boot_handle_event(const ui_event_t *event);
void prism_debug_boot_render(void);

#ifdef __cplusplus
}
#endif

#endif /* PRISM_DEBUG_BOOT_H */
