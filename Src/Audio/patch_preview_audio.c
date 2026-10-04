#include "Audio/patch_preview_audio.h"

#include <string.h>

#include "Audio/Engines/fm_engine.h"
#include "Audio/mixer.h"
#include "Platform/memory_layout.h"

typedef enum
{
    PATCH_PREVIEW_AUDIO_EMPTY = 0U,
    PATCH_PREVIEW_AUDIO_PREPARED,
    PATCH_PREVIEW_AUDIO_ACTIVE
} patch_preview_audio_state_t;

typedef struct
{
    patch_preview_audio_state_t state;
    uint8_t note;
} patch_preview_audio_runtime_t;

static patch_preview_audio_runtime_t g_patch_preview_audio;
static AUDIO_HOT float g_patch_preview_render[BRICK6_FM_RENDER_BLOCK];

void patch_preview_audio_init(void)
{
    memset(&g_patch_preview_audio, 0, sizeof(g_patch_preview_audio));
    brick6_fm_preview_reset();
}

uint8_t patch_preview_audio_prepare_fm(
    const patch_preview_fm_publication_t *publication)
{
    if (publication == NULL) return 0U;
    brick6_fm_preview_reset();
    if (brick6_fm_preview_prepare(&publication->base,
                                  &publication->macros) == 0U)
        return 0U;
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_PREPARED;
    g_patch_preview_audio.note = 0U;
    return 1U;
}

uint8_t patch_preview_audio_note_on(uint8_t note, uint8_t velocity)
{
    if ((g_patch_preview_audio.state == PATCH_PREVIEW_AUDIO_EMPTY)
            || (note > 127U) || (velocity == 0U) || (velocity > 127U))
        return 0U;
    brick6_fm_preview_note_on(note, velocity);
    g_patch_preview_audio.note = note;
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_ACTIVE;
    return 1U;
}

uint8_t patch_preview_audio_note_off(uint8_t note)
{
    if (g_patch_preview_audio.state == PATCH_PREVIEW_AUDIO_EMPTY) return 1U;
    brick6_fm_preview_note_off(note);
    return 1U;
}

uint8_t patch_preview_audio_stop(void)
{
    brick6_fm_preview_reset();
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_EMPTY;
    g_patch_preview_audio.note = 0U;
    return 1U;
}

uint8_t patch_preview_audio_render_main(float *out_main_l, float *out_main_r,
                                        uint32_t frames)
{
    if ((g_patch_preview_audio.state != PATCH_PREVIEW_AUDIO_ACTIVE)
            || (out_main_l == NULL) || (out_main_r == NULL)
            || (frames == 0U) || (frames > BRICK6_FM_RENDER_BLOCK))
        return 0U;

    if (brick6_fm_preview_render(g_patch_preview_render, frames) == 0U)
    {
        g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_PREPARED;
        return 0U;
    }
    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        const float sample = g_patch_preview_render[frame]
            * MIXER_TRACK_NOMINAL_TRIM;
        out_main_l[frame] += sample;
        out_main_r[frame] += sample;
    }
    return 1U;
}
