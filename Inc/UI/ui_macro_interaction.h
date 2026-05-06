#ifndef UI_MACRO_INTERACTION_H
#define UI_MACRO_INTERACTION_H

#include <stdint.h>
#include "Param/param_registry.h"
#include "ui_param.h"

void ui_macro_interaction_init(void);
void ui_macro_interaction_reset(void);
void ui_macro_interaction_note_hall_press(uint8_t hall);
void ui_macro_interaction_note_hall_release(uint8_t hall);
void ui_macro_interaction_service_hall(uint8_t hall, uint8_t pressed);
uint8_t ui_macro_interaction_note_encoder_delta_with_context(const ui_param_encoder_context_t *ctx,
                                                            uint8_t encoder,
                                                            int16_t delta);
uint8_t ui_macro_interaction_note_encoder_delta(uint8_t encoder, int16_t delta);
uint8_t ui_macro_interaction_get_held_scene(uint8_t *out_scene);
uint8_t ui_macro_interaction_param_is_locked(param_id_t param);
uint8_t ui_macro_interaction_get_param_lock_value(param_id_t param,
                                                  uint8_t *out_track,
                                                  float *out_scene_value);
uint8_t ui_macro_interaction_get_active_slot_lock(param_id_t *out_param);
uint8_t ui_macro_interaction_get_active_slot_target(uint8_t *out_bank,
                                                    uint8_t *out_macro,
                                                    uint8_t *out_slot);
uint8_t ui_macro_interaction_get_active_slot_value(uint8_t *out_track,
                                                   param_id_t *out_param,
                                                   float *out_scene_value);

#endif /* UI_MACRO_INTERACTION_H */
