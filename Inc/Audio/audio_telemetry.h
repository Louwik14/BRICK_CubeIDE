#ifndef AUDIO_TELEMETRY_H
#define AUDIO_TELEMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t active;
    uint8_t reverse;
    uint16_t sample_id;
    uint16_t ram_slot;
    uint32_t ram_generation;
    uint32_t frame;
    uint32_t frame_count;
    uint32_t trigger_order;
} audio_telemetry_ram_playhead_t;

typedef struct
{
    uint32_t ram_playhead_published;
    uint32_t ram_playhead_overwritten;
} audio_telemetry_diag_t;

void audio_telemetry_init(void);
void audio_telemetry_publish_ram_playhead_from_audio(uint8_t track,
                                                     const audio_telemetry_ram_playhead_t *snapshot);
uint8_t audio_telemetry_get_ram_playhead(uint8_t track,
                                         uint16_t sample_id,
                                         audio_telemetry_ram_playhead_t *out_snapshot);
void audio_telemetry_diag_snapshot(audio_telemetry_diag_t *out_diag);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TELEMETRY_H */
