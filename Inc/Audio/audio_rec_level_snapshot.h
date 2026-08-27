#ifndef AUDIO_REC_LEVEL_SNAPSHOT_H
#define AUDIO_REC_LEVEL_SNAPSHOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t generation;
    uint32_t peak_abs_pcm24;
} audio_rec_level_snapshot_t;

_Static_assert(sizeof(audio_rec_level_snapshot_t) == 8U,
               "AUDIO REC level snapshot ABI changed");

void audio_rec_level_snapshot_audio_init(void);
void audio_rec_level_snapshot_audio_publish(uint32_t peak_abs_pcm24);
uint8_t audio_rec_level_snapshot_control_read(
    audio_rec_level_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_REC_LEVEL_SNAPSHOT_H */
