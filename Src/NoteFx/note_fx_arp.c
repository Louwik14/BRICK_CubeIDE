#include "NoteFx/note_fx_arp.h"

#include <string.h>

void note_fx_arp_init(note_fx_arp_t *arp, uint32_t seed)
{
    if (arp == 0) return;
    memset(arp, 0, sizeof(*arp));
    arp->direction = 1;
    arp->random_state = (seed != 0U) ? seed : 0x6D2B79F5U;
}

uint8_t note_fx_arp_note_on(note_fx_arp_t *arp, uint8_t note, uint8_t velocity)
{
    if ((arp == 0) || (note > 127U)) return 0U;
    for (uint8_t i = 0U; i < arp->count; ++i) {
        if (arp->note[i] == note) { arp->velocity[i] = velocity; return 1U; }
    }
    if (arp->count >= NOTE_FX_ARP_MAX_SOURCES) return 0U;
    if (arp->count == 0U) { arp->phase = 0U; arp->cursor = 0U; arp->direction = 1; }
    arp->note[arp->count] = note;
    arp->velocity[arp->count++] = velocity;
    return 1U;
}

uint8_t note_fx_arp_note_off(note_fx_arp_t *arp, uint8_t note)
{
    if (arp == 0) return 0U;
    for (uint8_t i = 0U; i < arp->count; ++i) {
        if (arp->note[i] == note) {
            for (uint8_t j = i + 1U; j < arp->count; ++j) {
                arp->note[j - 1U] = arp->note[j]; arp->velocity[j - 1U] = arp->velocity[j];
            }
            --arp->count;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t sorted_index(const note_fx_arp_t *arp, uint8_t rank)
{
    uint8_t used[NOTE_FX_ARP_MAX_SOURCES] = {0};
    uint8_t selected = 0U;
    for (uint8_t r = 0U; r <= rank; ++r) {
        uint8_t best = 0xFFU;
        for (uint8_t i = 0U; i < arp->count; ++i)
            if (!used[i] && ((best == 0xFFU) || (arp->note[i] < arp->note[best]))) best = i;
        used[best] = 1U; selected = best;
    }
    return selected;
}

uint8_t note_fx_arp_next(note_fx_arp_t *arp, note_fx_arp_style_t style,
                         uint8_t range, uint8_t *note, uint8_t *velocity)
{
    if ((arp == 0) || (note == 0) || (velocity == 0) || (arp->count == 0U)) return 0U;
    if (range < 1U) range = 1U; else if (range > 4U) range = 4U;
    uint32_t p = arp->phase++;
    uint8_t rank;
    if (style == NOTE_FX_ARP_RANDOM) {
        uint32_t x = arp->random_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; arp->random_state = x;
        rank = (uint8_t)(x % arp->count);
    } else if (style == NOTE_FX_ARP_UP_DOWN && arp->count > 1U) {
        const uint8_t cycle = (uint8_t)(2U * arp->count - 2U);
        const uint8_t pos = (uint8_t)(p % cycle);
        rank = (pos < arp->count) ? pos : (uint8_t)(cycle - pos);
    } else {
        rank = (uint8_t)(p % arp->count);
        if (style == NOTE_FX_ARP_DOWN) rank = (uint8_t)(arp->count - 1U - rank);
    }
    uint8_t idx = (style == NOTE_FX_ARP_ORDER) ? rank : sorted_index(arp, rank);
    uint32_t cycle = (style == NOTE_FX_ARP_UP_DOWN && arp->count > 1U) ? (2U * arp->count - 2U) : arp->count;
    uint8_t octave = (uint8_t)((p / cycle) % range);
    uint16_t out = (uint16_t)arp->note[idx] + (uint16_t)(12U * octave);
    if (out > 127U) out = arp->note[idx];
    *note = (uint8_t)out; *velocity = arp->velocity[idx];
    arp->last_source_note = arp->note[idx];
    arp->cursor = rank;
    return 1U;
}
