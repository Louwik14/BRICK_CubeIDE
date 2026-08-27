#include "Audio/audio_music_action_executor.h"

#include <stddef.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/synth_polyphony.h"
#include "Audio/control_music_queue.h"

uint8_t audio_music_action_execute(const control_music_action_t *action)
{
    if ((action == NULL) || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (action->kind > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U))
        return 0U;

    audio_binding_snapshot_t snapshot;
    if ((audio_note_engine_adapter_snapshot_read(action->entity_id,
                                                  &snapshot) == 0U)
            || (snapshot.binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }
    if (snapshot.binding.generation != action->binding_generation)
    {
        return 0U;
    }

    audio_note_engine_binding_t binding;
    if (audio_note_engine_adapter_resolve(action->entity_id,
                                          action->binding_generation,
                                          &binding) == 0U)
    {
        return 0U;
    }

    if (action->kind == (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
    {
        (void)audio_note_engine_adapter_apply(&binding, action->note, 0U, 0U,
                                              action->output_id);
    }
    const uint8_t applied = audio_note_engine_adapter_apply(
        &binding, action->note, action->velocity,
        (action->kind != (uint8_t)CONTROL_MUSIC_ACTION_STOP) ? 1U : 0U,
        action->output_id);
    return applied;
}

void audio_music_action_force_close_all(void)
{
    /* Quiesce/corruption recovery only: this is a physical panic, not normal
     * note admission and it creates no AUDIO -> CONTROL feedback. */
    synth_polyphony_panic();
    drum_synth_all_notes_off_all();
    brick6_sampler_runtime_stop_transport_clips();
}
