#include "Audio/waveform_control.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    waveform_control_command_t command;
} waveform_control_mailbox_t;

D3_IPC static waveform_control_mailbox_t g_waveform_control_mailbox;
static uint32_t g_waveform_control_audio_sequence;

void waveform_control_init(void)
{
    g_waveform_control_mailbox.sequence = 0U;
    g_waveform_control_mailbox.command = (waveform_control_command_t){
        .entity_id = BRICK_ENTITY_INVALID_ID
    };
    g_waveform_control_audio_sequence = 0U;
    __DMB();
}

void waveform_control_publish(brick_entity_id_t entity_id,
                              uint8_t enabled,
                              uint8_t fast_refresh)
{
    waveform_control_command_t command = {
        .entity_id = (entity_id < BRICK_ENTITY_CAPACITY)
            ? entity_id : BRICK_ENTITY_INVALID_ID,
        .enabled = (enabled != 0U) ? 1U : 0U,
        .fast_refresh = (fast_refresh != 0U) ? 1U : 0U,
    };
    if (command.enabled == 0U)
        command.entity_id = BRICK_ENTITY_INVALID_ID;

    uint32_t sequence = g_waveform_control_mailbox.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_waveform_control_mailbox.sequence = sequence + 1U;
    __DMB();
    g_waveform_control_mailbox.command = command;
    __DMB();
    g_waveform_control_mailbox.sequence = sequence + 2U;
}

uint8_t waveform_control_audio_consume(waveform_control_command_t *out_command)
{
    if (out_command == NULL)
        return 0U;

    const uint32_t before = g_waveform_control_mailbox.sequence;
    if (((before & 1U) != 0U) || (before == g_waveform_control_audio_sequence))
        return 0U;
    __DMB();
    const waveform_control_command_t command =
        g_waveform_control_mailbox.command;
    __DMB();
    const uint32_t after = g_waveform_control_mailbox.sequence;
    if ((before != after) || ((after & 1U) != 0U))
        return 0U;

    *out_command = command;
    g_waveform_control_audio_sequence = after;
    return 1U;
}
