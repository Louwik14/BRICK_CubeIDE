#include "Core/mixer_routing_publication.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t route_master[MIXER_MAX_TRACKS];
    volatile int8_t insert_slot[MIXER_MAX_TRACKS][MIXER_INSERTS_PER_TRACK];
} mixer_routing_mailbox_t;

_Static_assert(sizeof(mixer_routing_mailbox_t) == 56U,
               "Mixer routing mailbox ABI changed");

D3_IPC static mixer_routing_mailbox_t g_mixer_routing_mailbox;

static void mixer_routing_publish_begin(void)
{
    uint32_t sequence = g_mixer_routing_mailbox.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_mixer_routing_mailbox.sequence = sequence + 1U;
    __DMB();
}

static void mixer_routing_publish_end(void)
{
    __DMB();
    g_mixer_routing_mailbox.sequence++;
    if (g_mixer_routing_mailbox.sequence == 0U)
        g_mixer_routing_mailbox.sequence = 2U;
    __DMB();
}

void mixer_routing_control_init(void)
{
    g_mixer_routing_mailbox.sequence = 1U;
    for (uint32_t track = 0U; track < MIXER_MAX_TRACKS; ++track)
    {
        g_mixer_routing_mailbox.route_master[track] = 1U;
        for (uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
            g_mixer_routing_mailbox.insert_slot[track][insert] = -1;
    }
    mixer_routing_publish_end();
}

uint8_t mixer_routing_control_set_route(uint32_t track_id,
                                        mixer_route_t route)
{
    if (track_id >= MIXER_MAX_TRACKS)
        return 0U;
    mixer_routing_publish_begin();
    g_mixer_routing_mailbox.route_master[track_id] =
        ((route & MIXER_ROUTE_MASTER) != 0U) ? 1U : 0U;
    mixer_routing_publish_end();
    return 1U;
}

uint8_t mixer_routing_control_set_insert_slot(uint32_t track_id,
                                              uint32_t insert_idx,
                                              int8_t slot)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (insert_idx >= MIXER_INSERTS_PER_TRACK))
        return 0U;
    mixer_routing_publish_begin();
    g_mixer_routing_mailbox.insert_slot[track_id][insert_idx] = slot;
    mixer_routing_publish_end();
    return 1U;
}

uint8_t mixer_routing_publication_audio_read(mixer_routing_snapshot_t *out)
{
    if (out == 0)
        return 0U;

    /* Bounded seqlock read: a structural update may be deferred by one block. */
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_mixer_routing_mailbox.sequence;
        __DMB();
        if ((before == 0U) || ((before & 1U) != 0U))
            continue;
        for (uint32_t track = 0U; track < MIXER_MAX_TRACKS; ++track)
        {
            out->route_master[track] =
                g_mixer_routing_mailbox.route_master[track];
            for (uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
            {
                out->insert_slot[track][insert] =
                    g_mixer_routing_mailbox.insert_slot[track][insert];
            }
        }
        __DMB();
        const uint32_t after = g_mixer_routing_mailbox.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            out->generation = after;
            return 1U;
        }
    }
    return 0U;
}
