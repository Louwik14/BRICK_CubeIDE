#ifndef UI_ROLL_POPUP_H
#define UI_ROLL_POPUP_H

#include <stdint.h>

#include "Seq/seq_types.h"

void ui_roll_popup_show(seq_track_id_t track, seq_step_id_t step, uint8_t roll, uint32_t now_ms);
void ui_roll_popup_render(uint32_t now_ms);

#endif /* UI_ROLL_POPUP_H */
