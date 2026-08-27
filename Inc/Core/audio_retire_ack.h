#pragma once

#include <stdint.h>

typedef enum
{
    AUDIO_RETIRE_ACK_MULTI = 1,
    AUDIO_RETIRE_ACK_RAM = 2,
    AUDIO_RETIRE_ACK_WAVE = 3
} audio_retire_ack_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t reserved;
    uint16_t slot;
    uint32_t generation;
} audio_retire_ack_t;

void audio_retire_ack_init(void);
uint8_t audio_retire_ack_publish(audio_retire_ack_kind_t kind,
                                 uint16_t slot,
                                 uint32_t generation);
uint8_t audio_retire_ack_drain(audio_retire_ack_t *out_ack,
                               uint8_t capacity);
