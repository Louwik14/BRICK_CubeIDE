#pragma once

#include <stdint.h>

typedef enum
{
    TRACK_MUTE_KIND_NONE = 0,
    TRACK_MUTE_KIND_AUDIO,
    TRACK_MUTE_KIND_MIDI,
    TRACK_MUTE_KIND_EXTERNAL,
    TRACK_MUTE_KIND_LOOPER,
    TRACK_MUTE_KIND_INPUT,
    TRACK_MUTE_KIND_FX
} track_mute_kind_t;

void track_mute_init(void);
track_mute_kind_t track_mute_get_kind(uint8_t track);
uint8_t track_mute_is_available(uint8_t track);
uint8_t track_mute_get(uint8_t track);
uint8_t track_mute_is_effectively_muted(uint8_t track);
uint8_t track_mute_set(uint8_t track, uint8_t muted);
uint8_t track_mute_should_suppress_note_on(uint8_t track);
