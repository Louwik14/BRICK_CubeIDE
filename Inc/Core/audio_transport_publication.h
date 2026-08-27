#ifndef AUDIO_TRANSPORT_PUBLICATION_H
#define AUDIO_TRANSPORT_PUBLICATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t running;
    uint8_t start_pending;
    uint16_t reserved;
    uint32_t tempo_effective_bpm_milli;
    uint32_t samples_per_step_q16;
    uint32_t transport_epoch;
} audio_transport_publication_t;

void audio_transport_publication_init(void);
void audio_transport_publication_refresh(void);
uint8_t audio_transport_publication_read(
    audio_transport_publication_t *out_publication);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TRANSPORT_PUBLICATION_H */
