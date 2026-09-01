#include "IPC/control_music_publication.h"

#include <stddef.h>
#include "ControlRT/control_rt_publication.h"
#include "Platform/memory_layout.h"

CONTROL_STATE_SDRAM static control_audio_command_t
    g_music_publish_scratch[2U * (CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST
        + CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY)];

_Static_assert((2U * (CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST
                      + CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY))
                   == 722U,
               "NOTE admission no longer matches the functional FIFO proof");

static uint16_t control_music_convert(const control_music_action_t *action,
                                      control_audio_command_t *out)
{
    if ((action == NULL) || (out == NULL)
            || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (control_music_action_kind(action) > CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U)) return 0U;
    const control_audio_command_t base = {
        .effective_sample_time = action->due_sample,
        .value = action->output_id,
        .id = (uint16_t)action->note | ((uint16_t)action->velocity << 8),
        .entity = action->entity_id
    };
    out[0] = base;
    out[0].opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_NOTE,
        (control_music_action_kind(action) == CONTROL_MUSIC_ACTION_START)
            ? CONTROL_AUDIO_NOTE_ON : CONTROL_AUDIO_NOTE_OFF);
    if (control_music_action_kind(action) != CONTROL_MUSIC_ACTION_RETRIGGER) return 1U;
    out[1] = base;
    out[1].opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
        CONTROL_AUDIO_COMMAND_NOTE, CONTROL_AUDIO_NOTE_ON);
    return 2U;
}

uint16_t control_music_publication_free(void)
{
    return (uint16_t)(control_rt_publication_free() / 2U);
}

uint8_t control_music_publication_publish_merged_window(
    const control_music_action_t *internal_actions,
    const uint16_t *internal_next, const uint16_t *internal_heads,
    uint16_t internal_count,
    const control_music_action_t *external_actions,
    const uint16_t *external_next, const uint16_t *external_heads,
    uint16_t external_count, uint16_t bucket_count)
{
    if ((internal_actions == NULL) || (internal_next == NULL)
            || (internal_heads == NULL) || (external_actions == NULL)
            || (external_next == NULL) || (external_heads == NULL)) return 0U;
    uint16_t emitted = 0U;
    uint16_t internal_visited = 0U;
    uint16_t external_visited = 0U;
    for (uint16_t bucket = 0U; bucket < bucket_count; ++bucket)
    {
        for (uint8_t source = 0U; source < 2U; ++source)
        {
            const control_music_action_t *const actions = source
                ? external_actions : internal_actions;
            const uint16_t *const next = source ? external_next : internal_next;
            uint16_t index = source ? external_heads[bucket] : internal_heads[bucket];
            while (index != UINT16_MAX)
            {
                const uint16_t count = source ? external_count : internal_count;
                if (index >= count) return 0U;
                if (source) ++external_visited; else ++internal_visited;
                const uint16_t n = control_music_convert(&actions[index],
                    &g_music_publish_scratch[emitted]);
                if (n == 0U) return 0U;
                emitted = (uint16_t)(emitted + n);
                index = next[index];
            }
        }
    }
    if ((internal_visited != internal_count)
            || (external_visited != external_count) || (emitted == 0U)) return 0U;
    return control_rt_publish_batch_scheduled(g_music_publish_scratch, emitted);
}
