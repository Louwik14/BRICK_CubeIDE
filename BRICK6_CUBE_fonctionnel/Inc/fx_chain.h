#ifndef FX_CHAIN_H
#define FX_CHAIN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  float volume[8];
  bool delay_enabled;
  uint32_t delay_frames;
  float delay_feedback;
  float delay_mix;
} fx_chain_t;

void fx_chain_init(fx_chain_t *fx);
bool fx_chain_init_delay(fx_chain_t *fx, uint32_t delay_frames);
void fx_chain_process(fx_chain_t *fx, int32_t *buffer, uint32_t frames, uint32_t channels);

#endif /* FX_CHAIN_H */
