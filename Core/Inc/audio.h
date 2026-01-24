#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);
void audio_start(void);
void audio_fill_buffer(int16_t *buffer, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
