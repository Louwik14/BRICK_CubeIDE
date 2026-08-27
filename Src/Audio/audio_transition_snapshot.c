#include "Audio/audio_transition_snapshot.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t global_active;
    volatile uint8_t track_active[BRICK_ENTITY_CAPACITY];
} audio_transition_snapshot_mailbox_t;

D3_IPC static audio_transition_snapshot_mailbox_t g_audio_transition_snapshot;
static AUDIO_HOT uint8_t
    g_audio_transition_snapshot_candidate[BRICK_ENTITY_CAPACITY];
static AUDIO_HOT uint8_t
    g_audio_transition_snapshot_last[BRICK_ENTITY_CAPACITY];
static AUDIO_HOT uint8_t g_audio_transition_snapshot_candidate_global;
static AUDIO_HOT uint8_t g_audio_transition_snapshot_last_global;
static AUDIO_HOT uint8_t g_audio_transition_snapshot_last_valid;

#define AUDIO_TRANSITION_SNAPSHOT_READ_ATTEMPTS 2U

void audio_transition_snapshot_init(void)
{
    memset((void *)&g_audio_transition_snapshot, 0,
           sizeof(g_audio_transition_snapshot));
    memset(g_audio_transition_snapshot_candidate, 0,
           sizeof(g_audio_transition_snapshot_candidate));
    memset(g_audio_transition_snapshot_last, 0,
           sizeof(g_audio_transition_snapshot_last));
    g_audio_transition_snapshot_candidate_global = 0U;
    g_audio_transition_snapshot_last_global = 0U;
    g_audio_transition_snapshot_last_valid = 0U;
}

void audio_transition_snapshot_publish(uint8_t global_active,
                                       const uint8_t *track_active)
{
    uint32_t sequence = g_audio_transition_snapshot.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_audio_transition_snapshot.sequence = sequence + 1U;
    __DMB();
    g_audio_transition_snapshot.global_active = global_active;
    if (track_active != NULL)
    {
        memcpy((void *)g_audio_transition_snapshot.track_active,
               track_active, sizeof(g_audio_transition_snapshot.track_active));
    }
    else
    {
        memset((void *)g_audio_transition_snapshot.track_active, 0,
               sizeof(g_audio_transition_snapshot.track_active));
    }
    __DMB();
    g_audio_transition_snapshot.sequence = sequence + 2U;
    __DMB();
}

uint8_t audio_transition_snapshot_read_all(uint8_t *out_global_active,
                                           uint8_t *out_track_active,
                                           uint8_t capacity)
{
    if ((out_global_active == NULL) || (out_track_active == NULL)
            || (capacity == 0U) || (capacity > BRICK_ENTITY_CAPACITY))
        return 0U;

    for (uint8_t attempt = 0U;
         attempt < AUDIO_TRANSITION_SNAPSHOT_READ_ATTEMPTS;
         ++attempt)
    {
        const uint32_t before = g_audio_transition_snapshot.sequence;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        g_audio_transition_snapshot_candidate_global =
            g_audio_transition_snapshot.global_active;
        memcpy(g_audio_transition_snapshot_candidate,
               (const void *)g_audio_transition_snapshot.track_active,
               sizeof(g_audio_transition_snapshot_candidate));
        __DMB();
        const uint32_t after = g_audio_transition_snapshot.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            g_audio_transition_snapshot_last_global =
                g_audio_transition_snapshot_candidate_global;
            memcpy(g_audio_transition_snapshot_last,
                   g_audio_transition_snapshot_candidate,
                   sizeof(g_audio_transition_snapshot_last));
            g_audio_transition_snapshot_last_valid = 1U;
            *out_global_active = g_audio_transition_snapshot_last_global;
            memcpy(out_track_active,
                   g_audio_transition_snapshot_last,
                   capacity);
            return 1U;
        }
    }

    if (g_audio_transition_snapshot_last_valid != 0U)
    {
        *out_global_active = g_audio_transition_snapshot_last_global;
        memcpy(out_track_active,
               g_audio_transition_snapshot_last,
               capacity);
        return 1U;
    }

    return 0U;
}
