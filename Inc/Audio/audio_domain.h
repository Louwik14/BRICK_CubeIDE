#pragma once

#include <stdint.h>

#include "Audio/brick6_audio_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_domain_init(const brick6_audio_boot_intent_t *boot_intent);
void audio_domain_background_poll(uint32_t byte_budget);
void audio_domain_background_task_process(void);

#ifdef __cplusplus
}
#endif
