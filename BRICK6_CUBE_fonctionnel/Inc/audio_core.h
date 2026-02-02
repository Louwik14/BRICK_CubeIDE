#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <stdint.h>


#define AUDIO_CORE_SAMPLE_RATE 48000U
#define AUDIO_CORE_FRAMES_PER_BLOCK 256U
#define AUDIO_CORE_CHANNELS 8U

typedef struct {
  int32_t samples[AUDIO_CORE_FRAMES_PER_BLOCK][AUDIO_CORE_CHANNELS];
} audio_block_t;

void audio_core_init(void);
void audio_core_process_block(int32_t *out, uint32_t frames);
void audio_core_on_input_block(const int32_t *data, uint32_t frames);
uint32_t audio_core_get_process_count(void);
uint32_t audio_core_get_usb_block_used_count(void);
uint32_t audio_core_get_usb_block_missed_count(void);
uint32_t audio_core_get_fallback_count(void);
uint32_t audio_core_get_frames_requested_total(void);
uint32_t audio_core_get_frames_provided_total(void);
uint32_t audio_core_get_last_usb_available(void);
uint32_t audio_core_get_last_usb_samples(void);
uint32_t audio_core_get_last_frames(void);
uint32_t audio_core_get_last_usb_available_frames(void);
uint32_t audio_core_get_last_usb_need_frames(void);
uint8_t audio_core_get_last_source(void);

#endif /* AUDIO_CORE_H */
