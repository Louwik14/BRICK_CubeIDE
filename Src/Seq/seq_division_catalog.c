#include "Seq/seq_division_catalog.h"

#include <stddef.h>

const char *const seq_division_track_labels[SEQ_DIVISION_TRACK_UI_COUNT + 1U] =
{
    "OFF", "1/2", "1/4", "1/8", NULL
};

const char *const seq_division_arp_labels[SEQ_DIVISION_ARP_COUNT + 1U] =
{
    "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T", NULL
};

#define SEQ_DIVISION_RATIO_Q16(_numerator, _denominator) \
    (uint32_t)((((uint64_t)(_numerator) << 16) + ((_denominator) / 2U)) / (_denominator))

static const seq_division_desc_t g_seq_division_catalog[SEQ_DIVISION_ARP_COUNT] =
{
    { "1/4",   4U, 1U, SEQ_DIVISION_RATIO_Q16(4U, 1U) },
    { "1/8",   2U, 1U, SEQ_DIVISION_RATIO_Q16(2U, 1U) },
    { "1/16",  1U, 1U, SEQ_DIVISION_RATIO_Q16(1U, 1U) },
    { "1/32",  1U, 2U, SEQ_DIVISION_RATIO_Q16(1U, 2U) },
    { "1/4T",  8U, 3U, SEQ_DIVISION_RATIO_Q16(8U, 3U) },
    { "1/8T",  4U, 3U, SEQ_DIVISION_RATIO_Q16(4U, 3U) },
    { "1/16T", 2U, 3U, SEQ_DIVISION_RATIO_Q16(2U, 3U) },
    { "1/32T", 1U, 3U, SEQ_DIVISION_RATIO_Q16(1U, 3U) },
};

const seq_division_desc_t *seq_division_get(uint8_t index)
{
    return (index < SEQ_DIVISION_ARP_COUNT) ? &g_seq_division_catalog[index] : 0;
}

const char *seq_division_arp_label(uint8_t index)
{
    return (index < SEQ_DIVISION_ARP_COUNT) ? seq_division_arp_labels[index] : "1/4";
}

uint8_t seq_division_track_div_from_ui(uint8_t index)
{
    static const uint8_t divisions[SEQ_DIVISION_TRACK_UI_COUNT] = { 1U, 2U, 4U, 8U };
    return (index < SEQ_DIVISION_TRACK_UI_COUNT) ? divisions[index] : divisions[0];
}

uint8_t seq_division_track_div_to_ui(uint8_t div)
{
    switch (div)
    {
        case 2U: return 1U;
        case 4U: return 2U;
        case 8U: return 3U;
        case 1U:
        default: return 0U;
    }
}

uint8_t seq_division_is_track_div(uint8_t div)
{
    return (div == 1U || div == 2U || div == 4U || div == 8U) ? 1U : 0U;
}

uint64_t seq_division_period_samples(uint8_t index, uint32_t samples_per_step_q16)
{
    const seq_division_desc_t *const desc = seq_division_get(index);
    if (desc == 0) return 1U;

    const uint64_t value = ((uint64_t)samples_per_step_q16 * desc->ratio_q16) >> 32;
    return (value != 0U) ? value : 1U;
}
