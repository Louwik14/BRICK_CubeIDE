#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "Keyboard/keyboard_note_lifecycle.h"

typedef struct { uint8_t note, on; uint64_t sample; } event_t;

static void apply(event_t *events, uint16_t count, uint8_t active[128])
{
    for (uint16_t i = 1U; i < count; ++i) {
        event_t x = events[i]; uint16_t j = i;
        while (j != 0U && (events[j - 1U].sample > x.sample
                || (events[j - 1U].sample == x.sample
                    && events[j - 1U].on > x.on))) {
            events[j] = events[j - 1U]; --j;
        }
        events[j] = x;
    }
    for (uint16_t i = 0U; i < count; ++i)
        active[events[i].note] = events[i].on;
}

int main(void)
{
    keyboard_note_time_order_t order;
    keyboard_note_time_order_reset(&order);
    event_t events[259]; uint16_t n = 0U; uint32_t serial = 0U;
    const uint8_t held[] = {60U, 64U, 65U};
    for (uint8_t i = 0U; i < 3U; ++i)
        events[n++] = (event_t){held[i], 1U,
            keyboard_note_time_order_apply(&order, 10U, ++serial, 480U)};
    for (uint8_t i = 0U; i < 64U; ++i) {
        events[n++] = (event_t){72U, 1U, keyboard_note_time_order_apply(&order, 11U, ++serial, 528U)};
        events[n++] = (event_t){72U, 0U, keyboard_note_time_order_apply(&order, 11U, ++serial, 528U)};
        events[n++] = (event_t){74U, 1U, keyboard_note_time_order_apply(&order, 11U, ++serial, 528U)};
        events[n++] = (event_t){74U, 0U, keyboard_note_time_order_apply(&order, 11U, ++serial, 528U)};
    }
    uint8_t active[128] = {0U}; apply(events, n, active);
    assert(active[60] && active[64] && active[65]);
    assert(!active[72] && !active[74]);

    keyboard_note_time_order_reset(&order); n = 0U; serial = 0U;
    for (uint8_t round = 0U; round < 16U; ++round)
        for (uint8_t note = 48U; note < 56U; ++note) {
            events[n++] = (event_t){note, 1U, keyboard_note_time_order_apply(&order, 20U + round, ++serial, 1000U + round * 48U)};
            events[n++] = (event_t){note, 0U, keyboard_note_time_order_apply(&order, 20U + round, ++serial, 1000U + round * 48U)};
        }
    for (uint8_t i = 0U; i < 128U; ++i) active[i] = 0U;
    apply(events, n, active);
    for (uint8_t i = 0U; i < 128U; ++i) assert(active[i] == 0U);
    assert(order.tie_adjustments != 0U);

    keyboard_note_time_order_reset(&order);
    assert(keyboard_note_time_order_apply(&order, 30U, 100U, 2000U) == 2000U);
    /* Serial spaces are producer-local (Hall, USB device, USB host). */
    assert(keyboard_note_time_order_apply(&order, 30U, 1U, 2000U) == 2001U);
    puts("keyboard note lifecycle: PASS");
    return 0;
}
