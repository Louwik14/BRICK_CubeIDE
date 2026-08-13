#include "Audio/audio_note_admission.h"

#include <stddef.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Core/track_runtime.h"
#include "Storage/memory_layout.h"

#define AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY 64U

typedef struct
{
    uint32_t token;
    audio_note_engine_binding_t binding;
    uint8_t note;
    uint8_t active;
} audio_note_occurrence_t;

AUDIO_HOT static audio_note_occurrence_t
    g_audio_note_occurrences[AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY];

void audio_note_admission_init(void)
{
    for (uint16_t i = 0U; i < AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY; ++i)
        g_audio_note_occurrences[i].active = 0U;
}

static int16_t audio_note_admission_find(brick_entity_id_t entity_id,
                                         uint32_t token,
                                         uint32_t binding_generation)
{
    for (uint16_t i = 0U; i < AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY; ++i)
    {
        const audio_note_occurrence_t *const occurrence =
            &g_audio_note_occurrences[i];
        if ((occurrence->active != 0U)
                && (occurrence->binding.audio_binding.entity_id == entity_id)
                && (occurrence->token == token)
                && (occurrence->binding.audio_binding.generation
                    == binding_generation))
            return (int16_t)i;
    }
    return -1;
}

static int16_t audio_note_admission_find_free(void)
{
    for (uint16_t i = 0U; i < AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY; ++i)
    {
        if (g_audio_note_occurrences[i].active == 0U)
            return (int16_t)i;
    }
    return -1;
}

uint8_t audio_note_admission_apply(const control_audio_event_t *event)
{
    if ((event == NULL) || (event->entity_id >= BRICK_ENTITY_CAPACITY)
            || (event->kind > (uint8_t)CONTROL_AUDIO_EVENT_NOTE_ON)
            || (event->occurrence_token == 0U))
        return 0U;

    if (event->kind == (uint8_t)CONTROL_AUDIO_EVENT_NOTE_ON)
    {
        if (audio_note_admission_find(event->entity_id, event->occurrence_token,
                                      event->binding_generation) >= 0)
            return 0U;
        const int16_t free_index = audio_note_admission_find_free();
        if (free_index < 0)
            return 0U;
        audio_note_engine_binding_t binding;
        if (audio_note_engine_adapter_resolve(event->entity_id,
                                              event->binding_generation,
                                              &binding) == 0U)
            return 0U;
        if (audio_note_engine_adapter_apply(&binding,
                                            event->note,
                                            event->velocity,
                                            1U,
                                            event->occurrence_token) == 0U)
            return 0U;
        g_audio_note_occurrences[free_index] =
            (audio_note_occurrence_t){
                .token = event->occurrence_token,
                .binding = binding,
                .note = event->note,
                .active = 1U
            };
        return 1U;
    }

    const int16_t index = audio_note_admission_find(
        event->entity_id, event->occurrence_token, event->binding_generation);
    if (index < 0)
        return 0U;
    audio_note_occurrence_t *const occurrence =
        &g_audio_note_occurrences[index];
    if (audio_note_engine_adapter_apply(&occurrence->binding,
                                        occurrence->note, 0U, 0U,
                                        occurrence->token) == 0U)
        return 0U;
    occurrence->active = 0U;
    return 1U;
}

void audio_note_admission_close_entity(brick_entity_id_t entity_id)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return;
    for (uint16_t i = 0U; i < AUDIO_NOTE_ADMISSION_OCCURRENCE_CAPACITY; ++i)
    {
        audio_note_occurrence_t *const occurrence =
            &g_audio_note_occurrences[i];
        if ((occurrence->active != 0U)
                && (occurrence->binding.audio_binding.entity_id == entity_id))
        {
            (void)audio_note_engine_adapter_apply(
                &occurrence->binding, occurrence->note, 0U, 0U,
                occurrence->token);
            occurrence->active = 0U;
        }
    }
}

void audio_note_admission_close_all(void)
{
    for (brick_entity_id_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        audio_note_admission_close_entity(entity);
}
