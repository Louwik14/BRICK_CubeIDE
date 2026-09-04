#pragma once

#include <stdint.h>

typedef uint8_t brick_entity_id_t;

#define BRICK_ENTITY_TOP_LEVEL_COUNT 8U
#define BRICK_ENTITY_GROUP_CHILD_COUNT 8U
#define BRICK_ENTITY_CAPACITY \
    (BRICK_ENTITY_TOP_LEVEL_COUNT + BRICK_ENTITY_GROUP_CHILD_COUNT)
#define BRICK_ENTITY_INVALID_ID UINT8_MAX
#define BRICK_ENTITY_GROUP_MASTER_ID (BRICK_ENTITY_TOP_LEVEL_COUNT - 1U)
#define BRICK_ENTITY_FIRST_GROUP_CHILD_ID BRICK_ENTITY_TOP_LEVEL_COUNT
#define BRICK_ENTITY_LAST_GROUP_CHILD_ID (BRICK_ENTITY_CAPACITY - 1U)

#define ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U
#define ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT   3U

typedef enum
{
    ENTITY_AUDIO_SOURCE_LINE = 0U,
    ENTITY_AUDIO_SOURCE_MIC,
    ENTITY_AUDIO_SOURCE_USB,
    ENTITY_AUDIO_SOURCE_COUNT
} entity_audio_source_t;

#ifdef __cplusplus
static_assert(ENTITY_AUDIO_SOURCE_COUNT == ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT,
              "audio source count must match the shared source contract");
#else
_Static_assert(ENTITY_AUDIO_SOURCE_COUNT == ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT,
               "audio source count must match the shared source contract");
#endif

typedef enum
{
    TRACK_CAPABILITY_NOTES = (1U << 0),
    TRACK_CAPABILITY_AUDIO = (1U << 1),
    TRACK_CAPABILITY_MIDI = (1U << 2),
    TRACK_CAPABILITY_KEYBOARD = (1U << 3),
    TRACK_CAPABILITY_MIDI_FX = (1U << 4),
    TRACK_CAPABILITY_AUTOMATION = (1U << 5),
    TRACK_CAPABILITY_MUTE = (1U << 6),
    TRACK_CAPABILITY_INPUT_RESERVATION = (1U << 7)
} track_capability_t;

#ifdef __cplusplus
static_assert(BRICK_ENTITY_CAPACITY <= UINT8_MAX,
              "entity IDs must fit the shared ABI");
#else
_Static_assert(BRICK_ENTITY_CAPACITY <= UINT8_MAX,
               "entity IDs must fit the shared ABI");
#endif
