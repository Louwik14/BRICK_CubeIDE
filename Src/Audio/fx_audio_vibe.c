#include "Audio/fx_audio_vibe.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Storage/memory_layout.h"

#define VIBE_PI 3.14159265358979323846f
#define VIBE_TWO_PI (VIBE_PI * 2.0f)

typedef struct
{
    float channel[2][FX_AUDIO_VIBE_DELAY_SAMPLES];
} fx_audio_vibe_history_t;

_Static_assert(sizeof(fx_audio_vibe_history_t) == 2056U,
               "Vibe history instance size changed");
_Static_assert(sizeof(fx_audio_vibe_history_t) * BRICK_ENTITY_CAPACITY
                   == 32896U,
               "Vibe history bank size changed");

AUDIO_HISTORY_SDRAM static fx_audio_vibe_history_t
    g_audio_fx_vibe_history[BRICK_ENTITY_CAPACITY];

static float clamp01(float value)
{
    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint32_t vibe_seed(brick_entity_id_t entity_id)
{
    uint32_t seed = 0x9e3779b9UL
        ^ ((uint32_t)(entity_id + 1U) * 0x85ebca6bUL);
    if (seed < 16386U) seed += 16386U;
    return seed;
}

static uint32_t vibe_xorshift(uint32_t value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static float vibe_interpolate(const float delay[FX_AUDIO_VIBE_DELAY_SAMPLES],
                              uint16_t count,
                              float offset)
{
    const uint16_t offset_integer = (uint16_t)offset;
    const float fraction = offset - floorf(offset);
    uint16_t first = (uint16_t)(count + offset_integer);
    if (first > 256U) first = (uint16_t)(first - 257U);
    uint16_t second = (uint16_t)(first + 1U);
    if (second > 256U) second = 0U;
    return (delay[first] * (1.0f - fraction))
        + (delay[second] * fraction);
}

void fx_audio_vibe_reset(fx_audio_vibe_state_t *state,
                         brick_entity_id_t entity_id)
{
    if ((state == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY)) return;
    memset(&g_audio_fx_vibe_history[entity_id],
           0,
           sizeof(g_audio_fx_vibe_history[entity_id]));
    memset(state, 0, sizeof(*state));
    state->phase = 3.0f;
    state->old_speed = 429496.7295f;
    state->wet = 1.0f;
    state->rng = vibe_seed(entity_id);
    state->count = 1U;
}

void fx_audio_vibe_prepare(fx_audio_vibe_state_t *state,
                           float drift,
                           float wet)
{
    if (state == NULL) return;
    const float drift_control = clamp01(drift);
    state->drift = drift_control * drift_control * drift_control * 0.001f;
    state->wet = clamp01(wet);
}

void fx_audio_vibe_process_stereo_sample(fx_audio_vibe_state_t *state,
                                         brick_entity_id_t entity_id,
                                         float *left,
                                         float *right)
{
    if ((state == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY)
            || (left == NULL) || (right == NULL)) return;

    const float dry_left = *left;
    const float dry_right = *right;
    state->phase += state->old_speed * state->drift;
    if (state->phase > VIBE_TWO_PI)
    {
        state->phase = 0.0f;
        state->old_speed = 0.4294967295f
            + ((float)state->rng * 0.0000000000618f);
    }

    fx_audio_vibe_history_t *const history =
        &g_audio_fx_vibe_history[entity_id];
    history->channel[0][state->count] = dry_left;
    history->channel[1][state->count] = dry_right;
    state->count++;
    if (state->count > 256U) state->count = 0U;

    const float offset_left = (sinf(state->phase) + 1.0f) * 127.0f;
    const float offset_right =
        (sinf(state->phase + (VIBE_PI / 2.0f)) + 1.0f) * 127.0f;
    const float wet_left = vibe_interpolate(history->channel[0],
                                            state->count,
                                            offset_left);
    const float wet_right = vibe_interpolate(history->channel[1],
                                             state->count,
                                             offset_right);
    if (state->wet != 1.0f)
    {
        *left = (wet_left * state->wet) + (dry_left * (1.0f - state->wet));
        *right = (wet_right * state->wet) + (dry_right * (1.0f - state->wet));
    }
    else
    {
        *left = wet_left;
        *right = wet_right;
    }

    /* GalacticVibe obtains its next modulation speed from fpdL. Although
     * output dither is removed, advancing this generator once per frame is
     * constitutive and preserves the original modulation evolution. */
    state->rng = vibe_xorshift(state->rng);
}

void fx_audio_vibe_process_stereo(fx_audio_vibe_state_t *state,
                                  brick_entity_id_t entity_id,
                                  float *left,
                                  float *right,
                                  uint32_t frames)
{
    if ((state == NULL) || (left == NULL) || (right == NULL)) return;
    for (uint32_t i = 0U; i < frames; ++i)
        fx_audio_vibe_process_stereo_sample(state,
                                            entity_id,
                                            &left[i],
                                            &right[i]);
}
