#ifndef UI_ROLL_POPUP_H
#define UI_ROLL_POPUP_H

#include <stdint.h>

#include "Seq/seq_types.h"

void ui_roll_popup_show(seq_track_id_t track, seq_step_id_t step, uint8_t roll, uint32_t now_ms);
void ui_roll_popup_render(uint32_t now_ms);
void ui_roll_popup_service(uint32_t now_ms);
uint8_t ui_roll_popup_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms);

#endif /* UI_ROLL_POPUP_H */
