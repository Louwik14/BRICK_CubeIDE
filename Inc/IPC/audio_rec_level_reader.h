#pragma once

#include "IPC/audio_rec_level_contract.h"

typedef struct
{
    uint32_t sequence;
    uint32_t arm_epoch;
    uint32_t generation;
    uint32_t peak_abs_pcm24;
} audio_rec_trigger_event_t;

uint8_t audio_rec_level_reader_read(audio_rec_level_snapshot_t *out_snapshot);
void audio_rec_level_control_publish_trigger_config(
    uint8_t enabled,
    uint32_t arm_epoch,
    uint32_t threshold_peak_abs_pcm24);
uint8_t audio_rec_level_reader_read_trigger_event(
    audio_rec_trigger_event_t *out_event);
void audio_rec_level_reader_ack_trigger_event(uint32_t sequence);
