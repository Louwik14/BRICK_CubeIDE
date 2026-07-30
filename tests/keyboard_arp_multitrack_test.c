#include "Keyboard/keyboard_arp.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t on;
} emitted_note_t;

static uint32_t g_now_ms;
static emitted_note_t g_emitted[256];
static uint16_t g_emitted_count;

uint32_t HAL_GetTick(void)
{
    return g_now_ms;
}

void keyboard_engine_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity)
{
    assert(g_emitted_count < (uint16_t)(sizeof(g_emitted) / sizeof(g_emitted[0])));
    g_emitted[g_emitted_count++] = (emitted_note_t){track, note, velocity, 1U};
}

void keyboard_engine_note_off_for_track(uint8_t track, uint8_t note)
{
    assert(g_emitted_count < (uint16_t)(sizeof(g_emitted) / sizeof(g_emitted[0])));
    g_emitted[g_emitted_count++] = (emitted_note_t){track, note, 0U, 0U};
}

static uint16_t count_events(uint8_t track, uint8_t on)
{
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < g_emitted_count; ++i)
    {
        if ((g_emitted[i].track == track) && (g_emitted[i].on == on))
        {
            ++count;
        }
    }
    return count;
}

int main(void)
{
    g_now_ms = 0U;
    keyboard_arp_init();

    keyboard_arp_set_hold_for_track(0U, true);
    keyboard_arp_set_rate_for_track(0U, 3U);
    keyboard_arp_set_pattern_for_track(0U, 0U);
    keyboard_arp_note_on_for_track(0U, 60U, 100U);
    keyboard_arp_note_on_for_track(0U, 64U, 100U);
    keyboard_arp_note_off_for_track(0U, 60U);
    keyboard_arp_note_off_for_track(0U, 64U);

    keyboard_arp_set_hold_for_track(1U, true);
    keyboard_arp_set_rate_for_track(1U, 0U);
    keyboard_arp_set_pattern_for_track(1U, 1U);
    keyboard_arp_note_on_for_track(1U, 67U, 90U);
    keyboard_arp_note_on_for_track(1U, 71U, 90U);
    keyboard_arp_note_off_for_track(1U, 67U);
    keyboard_arp_note_off_for_track(1U, 71U);

    keyboard_arp_sync_track(1U);
    keyboard_arp_on_mode_leave();
    g_now_ms = 500U;
    keyboard_arp_tick();
    assert(count_events(0U, 1U) > 0U);
    assert(count_events(1U, 1U) > 0U);

    const uint16_t track0_before = count_events(0U, 1U);
    keyboard_arp_set_hold_for_track(1U, false);
    g_now_ms = 1000U;
    keyboard_arp_tick();
    assert(count_events(0U, 1U) > track0_before);

    const uint16_t track1_after_disable = count_events(1U, 1U);
    g_now_ms = 2000U;
    keyboard_arp_tick();
    assert(count_events(1U, 1U) == track1_after_disable);

    assert(keyboard_arp_get_rate_for_track(0U) == 3U);
    assert(keyboard_arp_get_rate_for_track(1U) == 0U);
    assert(keyboard_arp_get_pattern_for_track(0U) == 0U);
    assert(keyboard_arp_get_pattern_for_track(1U) == 1U);
    return 0;
}
