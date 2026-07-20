#include "Audio/audio_midi_out.h"

#include <string.h>

#include "midi.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    uint8_t len;
    uint8_t priority;
    uint8_t valid;
    uint16_t order;
    uint64_t sample_time;
} audio_midi_out_event_t;

static audio_midi_out_event_t g_audio_midi_out_queue[AUDIO_MIDI_OUT_QUEUE_CAPACITY];
static volatile uint16_t g_audio_midi_out_head;
static volatile uint16_t g_audio_midi_out_tail;
static volatile uint32_t g_audio_midi_out_depth;
static uint16_t g_audio_midi_out_order;

static volatile uint32_t g_diag_high_water;
static volatile uint32_t g_diag_submitted;
static volatile uint32_t g_diag_drained;
static volatile uint32_t g_diag_dropped;
static volatile uint32_t g_diag_critical_failures;
static volatile uint32_t g_diag_replaced_low_priority;

static uint32_t audio_midi_out_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void audio_midi_out_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void audio_midi_out_update_high_water(void)
{
    if (g_audio_midi_out_depth > g_diag_high_water)
    {
        g_diag_high_water = g_audio_midi_out_depth;
    }
}

static uint16_t audio_midi_out_find_replaceable_normal(void)
{
    uint16_t index = g_audio_midi_out_tail;
    for (uint32_t scanned = 0U; scanned < AUDIO_MIDI_OUT_QUEUE_CAPACITY; ++scanned)
    {
        audio_midi_out_event_t *const event = &g_audio_midi_out_queue[index];
        if ((event->valid != 0U) && (event->priority == (uint8_t)AUDIO_MIDI_OUT_PRIORITY_NORMAL))
        {
            return index;
        }
        index = (uint16_t)((index + 1U) % AUDIO_MIDI_OUT_QUEUE_CAPACITY);
    }
    return UINT16_MAX;
}

void audio_midi_out_init(void)
{
    memset(g_audio_midi_out_queue, 0, sizeof(g_audio_midi_out_queue));
    g_audio_midi_out_head = 0U;
    g_audio_midi_out_tail = 0U;
    g_audio_midi_out_depth = 0U;
    g_audio_midi_out_order = 0U;
    audio_midi_out_diag_reset();
}

uint8_t audio_midi_out_submit_raw(uint8_t status,
                                  uint8_t data1,
                                  uint8_t data2,
                                  uint8_t len,
                                  uint64_t sample_time,
                                  uint8_t priority)
{
    if ((len == 0U) || (len > 3U))
    {
        return 0U;
    }

    audio_midi_out_event_t event;
    event.status = status;
    event.data1 = data1;
    event.data2 = data2;
    event.len = len;
    event.priority = (priority != 0U) ? (uint8_t)AUDIO_MIDI_OUT_PRIORITY_CRITICAL
                                      : (uint8_t)AUDIO_MIDI_OUT_PRIORITY_NORMAL;
    event.valid = 1U;
    event.sample_time = sample_time;
    event.order = ++g_audio_midi_out_order;
    if (g_audio_midi_out_order == 0U)
    {
        g_audio_midi_out_order = 1U;
        event.order = 1U;
    }

    const uint32_t primask = audio_midi_out_enter_critical();
    const uint16_t next_head = (uint16_t)((g_audio_midi_out_head + 1U) % AUDIO_MIDI_OUT_QUEUE_CAPACITY);
    if (next_head == g_audio_midi_out_tail)
    {
        if (event.priority != (uint8_t)AUDIO_MIDI_OUT_PRIORITY_CRITICAL)
        {
            g_diag_dropped++;
            audio_midi_out_exit_critical(primask);
            return 0U;
        }

        const uint16_t replace = audio_midi_out_find_replaceable_normal();
        if (replace == UINT16_MAX)
        {
            g_diag_dropped++;
            g_diag_critical_failures++;
            audio_midi_out_exit_critical(primask);
            return 0U;
        }
        g_audio_midi_out_queue[replace] = event;
        g_diag_replaced_low_priority++;
        g_diag_submitted++;
        audio_midi_out_exit_critical(primask);
        return 1U;
    }

    g_audio_midi_out_queue[g_audio_midi_out_head] = event;
    g_audio_midi_out_head = next_head;
    g_audio_midi_out_depth++;
    g_diag_submitted++;
    audio_midi_out_update_high_water();
    audio_midi_out_exit_critical(primask);
    return 1U;
}

uint8_t audio_midi_out_note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint64_t sample_time)
{
    return audio_midi_out_submit_raw((uint8_t)(0x90U | (channel & 0x0FU)),
                                     note,
                                     velocity,
                                     3U,
                                     sample_time,
                                     AUDIO_MIDI_OUT_PRIORITY_NORMAL);
}

uint8_t audio_midi_out_note_off(uint8_t channel, uint8_t note, uint8_t velocity, uint64_t sample_time)
{
    return audio_midi_out_submit_raw((uint8_t)(0x80U | (channel & 0x0FU)),
                                     note,
                                     velocity,
                                     3U,
                                     sample_time,
                                     AUDIO_MIDI_OUT_PRIORITY_CRITICAL);
}

uint8_t audio_midi_out_program_change(uint8_t channel, uint8_t program, uint64_t sample_time)
{
    return audio_midi_out_submit_raw((uint8_t)(0xC0U | (channel & 0x0FU)),
                                     program,
                                     0U,
                                     2U,
                                     sample_time,
                                     AUDIO_MIDI_OUT_PRIORITY_NORMAL);
}

uint8_t audio_midi_out_clock(uint64_t sample_time)
{
    return audio_midi_out_submit_raw(0xF8U, 0U, 0U, 1U, sample_time, AUDIO_MIDI_OUT_PRIORITY_NORMAL);
}

uint8_t audio_midi_out_start(uint64_t sample_time)
{
    return audio_midi_out_submit_raw(0xFAU, 0U, 0U, 1U, sample_time, AUDIO_MIDI_OUT_PRIORITY_CRITICAL);
}

uint8_t audio_midi_out_stop(uint64_t sample_time)
{
    return audio_midi_out_submit_raw(0xFCU, 0U, 0U, 1U, sample_time, AUDIO_MIDI_OUT_PRIORITY_CRITICAL);
}

uint8_t audio_midi_out_all_notes_off(uint8_t channel, uint64_t sample_time)
{
    return audio_midi_out_submit_raw((uint8_t)(0xB0U | (channel & 0x0FU)),
                                     123U,
                                     0U,
                                     3U,
                                     sample_time,
                                     AUDIO_MIDI_OUT_PRIORITY_CRITICAL);
}

void audio_midi_out_process(uint32_t max_events)
{
    while (max_events > 0U)
    {
        const uint32_t primask = audio_midi_out_enter_critical();
        if (g_audio_midi_out_tail == g_audio_midi_out_head)
        {
            audio_midi_out_exit_critical(primask);
            return;
        }

        audio_midi_out_event_t event = g_audio_midi_out_queue[g_audio_midi_out_tail];
        g_audio_midi_out_queue[g_audio_midi_out_tail].valid = 0U;
        g_audio_midi_out_tail = (uint16_t)((g_audio_midi_out_tail + 1U) % AUDIO_MIDI_OUT_QUEUE_CAPACITY);
        if (g_audio_midi_out_depth > 0U)
        {
            g_audio_midi_out_depth--;
        }
        audio_midi_out_exit_critical(primask);

        if (event.valid != 0U)
        {
            const uint8_t msg[3] = { event.status, event.data1, event.data2 };
            midi_send_raw(MIDI_DEST_BOTH, msg, event.len);
            g_diag_drained++;
        }
        max_events--;
    }
}

void audio_midi_out_diag_snapshot(audio_midi_out_diag_t *out_diag)
{
    if (out_diag == 0)
    {
        return;
    }

    const uint32_t primask = audio_midi_out_enter_critical();
    out_diag->current_depth = g_audio_midi_out_depth;
    out_diag->high_water = g_diag_high_water;
    out_diag->submitted = g_diag_submitted;
    out_diag->drained = g_diag_drained;
    out_diag->dropped = g_diag_dropped;
    out_diag->critical_failures = g_diag_critical_failures;
    out_diag->replaced_low_priority = g_diag_replaced_low_priority;
    audio_midi_out_exit_critical(primask);
}

void audio_midi_out_diag_reset(void)
{
    g_diag_high_water = g_audio_midi_out_depth;
    g_diag_submitted = 0U;
    g_diag_drained = 0U;
    g_diag_dropped = 0U;
    g_diag_critical_failures = 0U;
    g_diag_replaced_low_priority = 0U;
}
