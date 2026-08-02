#ifndef SEQ_CLIPBOARD_H
#define SEQ_CLIPBOARD_H

#include <stdint.h>

#include "Seq/seq_model.h"

typedef struct
{
    uint8_t partial;
    uint8_t trunc;
    uint8_t pasted_steps;
} seq_clipboard_paste_result_t;

void seq_clipboard_init(void);
uint8_t seq_clipboard_is_valid(void);

uint8_t seq_clipboard_copy(seq_track_id_t track,
                           const seq_step_id_t *steps,
                           uint8_t step_count);
uint8_t seq_clipboard_collect_paste_targets(seq_track_id_t target_track,
                                            const seq_step_id_t *dest_steps,
                                            uint8_t dest_count,
                                            seq_step_id_t *out_steps,
                                            uint8_t max_steps,
                                            uint8_t *out_count);
uint8_t seq_clipboard_paste(seq_track_id_t target_track,
                            const seq_step_id_t *dest_steps,
                            uint8_t dest_count,
                            seq_clipboard_paste_result_t *out_result);

#endif /* SEQ_CLIPBOARD_H */
