#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_AUDIO_BOOT_FX_SLOT_COUNT 2U

typedef enum
{
    BRICK6_AUDIO_BOOT_FX_EQ3 = 0U,
    BRICK6_AUDIO_BOOT_FX_COMP_LAB
} brick6_audio_boot_fx_type_t;

typedef struct
{
    uint8_t slot;
    uint8_t type;
} brick6_audio_boot_fx_slot_t;

typedef struct
{
    float sample_rate_hz;
    float master_gain;
    float postgain;
    float output_compensation;
    uint8_t fx_slot_count;
    brick6_audio_boot_fx_slot_t fx_slots[BRICK6_AUDIO_BOOT_FX_SLOT_COUNT];
} brick6_audio_boot_intent_t;

_Static_assert(sizeof(brick6_audio_boot_intent_t) == 24U,
               "Audio boot intent ABI changed");

uint8_t brick6_audio_boot_apply_early(const brick6_audio_boot_intent_t *intent);
uint8_t brick6_audio_boot_apply_output_tracks(const brick6_audio_boot_intent_t *intent);
uint8_t brick6_audio_boot_apply_drum(const brick6_audio_boot_intent_t *intent);
uint8_t brick6_audio_boot_apply_engines(const brick6_audio_boot_intent_t *intent);
void brick6_audio_boot_apply_binding_io(void);

#ifdef __cplusplus
}
#endif
