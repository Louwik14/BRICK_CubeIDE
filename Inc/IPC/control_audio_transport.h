#pragma once

#include <stdint.h>

void control_audio_transport_init(void);
void control_audio_transport_publish_changes(void);
void control_audio_transport_publish_changes_at(uint64_t sample);
