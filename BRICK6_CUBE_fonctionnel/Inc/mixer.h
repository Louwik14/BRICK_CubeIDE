#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

void mixer_clear(int32_t *dest, uint32_t samples);
void mixer_add(int32_t *dest, const int32_t *src, uint32_t samples, float gain);

#endif /* MIXER_H */
