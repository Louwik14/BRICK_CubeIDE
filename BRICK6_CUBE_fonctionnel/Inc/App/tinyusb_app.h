#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint32_t speaker_overflow_bytes;
  uint32_t capture_overflow_bytes;
  uint32_t capture_drop_bytes;
  uint32_t playback_underrun_bytes;
} tinyusb_audio_debug_stats_t;

void tinyusb_app_init(void);
void tinyusb_app_task(void);

uint32_t speaker_ring_read(uint8_t *data, uint32_t len);
uint32_t speaker_ring_available_bytes(void);
uint32_t tinyusb_capture_write_stereo_i24(const int32_t *interleaved, uint32_t frames);
void tinyusb_app_note_playback_underrun(uint32_t missing_bytes);
void tinyusb_app_get_debug_stats(tinyusb_audio_debug_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
