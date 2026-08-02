#ifndef SEQ_DIVISION_CATALOG_H
#define SEQ_DIVISION_CATALOG_H

#include <stdint.h>

#define SEQ_DIVISION_ARP_COUNT 8U
#define SEQ_DIVISION_TRACK_UI_COUNT 4U

typedef struct
{
    const char *label;
    uint32_t numerator;
    uint32_t denominator;
    uint32_t ratio_q16;
} seq_division_desc_t;

extern const char *const seq_division_track_labels[SEQ_DIVISION_TRACK_UI_COUNT + 1U];
extern const char *const seq_division_arp_labels[SEQ_DIVISION_ARP_COUNT + 1U];

const seq_division_desc_t *seq_division_get(uint8_t index);
const char *seq_division_arp_label(uint8_t index);
uint8_t seq_division_track_div_from_ui(uint8_t index);
uint8_t seq_division_track_div_to_ui(uint8_t div);
uint8_t seq_division_is_track_div(uint8_t div);
uint64_t seq_division_period_samples(uint8_t index, uint32_t samples_per_step_q16);

#endif
