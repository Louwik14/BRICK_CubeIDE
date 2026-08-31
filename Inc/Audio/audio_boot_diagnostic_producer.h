#pragma once

#include "IPC/audio_boot_diagnostic.h"

void audio_boot_diag_producer_init(void);
void audio_boot_diag_producer_publish_state(audio_init_state_t state,
                                            board_audio_boot_error_t error);
void audio_boot_diag_producer_publish_cpu(uint8_t valid, uint32_t avg_permille);
