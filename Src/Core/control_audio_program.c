#include "Core/control_audio_program.h"

#include <stddef.h>
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    control_audio_program_descriptor_t descriptor;
    uint32_t id;
} control_audio_program_slot_t;

AUDIO_STORAGE_SHARED_SDRAM static control_audio_program_slot_t
    g_control_audio_program[CONTROL_AUDIO_PROGRAM_CAPACITY];
D2_IPC static volatile uint32_t
    g_control_audio_program_consumed[CONTROL_AUDIO_PROGRAM_CAPACITY];
static uint32_t g_control_audio_next_program_id;

void control_audio_program_init(void)
{
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
        g_control_audio_program[i].id = 0U;
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
        g_control_audio_program_consumed[i] = 0U;
    g_control_audio_next_program_id = 1U;
    __DMB();
}

uint32_t control_audio_program_prepare(
    const control_audio_program_descriptor_t *descriptor)
{
    if (descriptor == NULL) return 0U;
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
    {
        if ((g_control_audio_program[i].id != 0U)
            && (g_control_audio_program_consumed[i]
                == g_control_audio_program[i].id))
        {
            g_control_audio_program_consumed[i] = 0U;
            __DMB();
            g_control_audio_program[i].id = 0U;
        }
        if (g_control_audio_program[i].id != 0U) continue;
        uint32_t id = g_control_audio_next_program_id++;
        if (id == 0U) id = g_control_audio_next_program_id++;
        g_control_audio_program[i].descriptor = *descriptor;
        __DMB();
        g_control_audio_program[i].id = id;
        __DMB();
        return id;
    }
    return 0U;
}

uint8_t control_audio_program_resolve(
    uint32_t program_id, control_audio_program_descriptor_t *out_descriptor)
{
    if ((program_id == 0U) || (out_descriptor == NULL)) return 0U;
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
        if (g_control_audio_program[i].id == program_id)
        {
            __DMB();
            *out_descriptor = g_control_audio_program[i].descriptor;
            __DMB();
            return (g_control_audio_program[i].id == program_id) ? 1U : 0U;
        }
    return 0U;
}

void control_audio_program_cancel(uint32_t program_id)
{
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
        if (g_control_audio_program[i].id == program_id)
        {
            g_control_audio_program[i].id = 0U;
            __DMB();
            return;
        }
}

void control_audio_program_consumer_release(uint32_t program_id)
{
    for (uint16_t i = 0U; i < CONTROL_AUDIO_PROGRAM_CAPACITY; ++i)
        if (g_control_audio_program[i].id == program_id)
        {
            __DMB();
            g_control_audio_program_consumed[i] = program_id;
            return;
        }
}
