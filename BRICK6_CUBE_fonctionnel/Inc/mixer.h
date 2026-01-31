#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

void mixer_mix_2_to_1(const int32_t *in_a,
                      const int32_t *in_b,
                      int32_t *out,
                      uint32_t samples);

#endif /* MIXER_H */
