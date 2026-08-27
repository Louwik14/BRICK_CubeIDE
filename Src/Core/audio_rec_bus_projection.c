#include "Core/audio_rec_bus_projection.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint16_t source_entity_mask;
    volatile uint8_t arm;
    volatile uint8_t source_flags;
} audio_rec_bus_projection_mailbox_t;

_Static_assert(sizeof(audio_rec_bus_projection_mailbox_t) == 8U,
               "AUDIO REC CONTROL mailbox ABI changed");

D3_IPC static audio_rec_bus_projection_mailbox_t g_audio_rec_bus_mailbox;
AUDIO_HOT static audio_rec_bus_control_snapshot_t g_audio_rec_bus_snapshot;

void audio_rec_bus_projection_control_init(void)
{
    g_audio_rec_bus_mailbox.sequence = 0U;
    audio_rec_bus_projection_control_publish(0U, AUDIO_REC_BUS_ARM_OFF, 0U);
}

void audio_rec_bus_projection_control_publish(uint16_t source_entity_mask,
                                              audio_rec_bus_arm_t arm,
                                              uint8_t source_flags)
{
    uint32_t sequence = g_audio_rec_bus_mailbox.sequence;
    if ((sequence & 1U) != 0U)
    {
        ++sequence;
    }

    g_audio_rec_bus_mailbox.sequence = sequence + 1U;
    __DMB();
    g_audio_rec_bus_mailbox.source_entity_mask = source_entity_mask;
    g_audio_rec_bus_mailbox.arm = (uint8_t)arm;
    g_audio_rec_bus_mailbox.source_flags = (uint8_t)(source_flags
        & (AUDIO_REC_BUS_SOURCE_LINE_DIRECT
            | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
            | AUDIO_REC_BUS_CAPTURE_ENABLED));
    __DMB();
    g_audio_rec_bus_mailbox.sequence = sequence + 2U;
    if (g_audio_rec_bus_mailbox.sequence == 0U)
    {
        g_audio_rec_bus_mailbox.sequence = 2U;
    }
    __DMB();
}

void audio_rec_bus_projection_audio_init(void)
{
    g_audio_rec_bus_snapshot = (audio_rec_bus_control_snapshot_t){0};
}

uint8_t audio_rec_bus_projection_audio_read(
    audio_rec_bus_control_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return 0U;
    }

    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_audio_rec_bus_mailbox.sequence;
        if ((before == 0U) || ((before & 1U) != 0U))
        {
            continue;
        }

        __DMB();
        audio_rec_bus_control_snapshot_t next = {
            .generation = before,
            .source_entity_mask = g_audio_rec_bus_mailbox.source_entity_mask,
            .arm = g_audio_rec_bus_mailbox.arm,
            .source_flags = g_audio_rec_bus_mailbox.source_flags
        };
        __DMB();

        const uint32_t after = g_audio_rec_bus_mailbox.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            g_audio_rec_bus_snapshot = next;
            *out_snapshot = next;
            return 1U;
        }
    }

    *out_snapshot = g_audio_rec_bus_snapshot;
    return (uint8_t)(g_audio_rec_bus_snapshot.generation != 0U);
}
