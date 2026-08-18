#ifndef UI_PARAM_H
#define UI_PARAM_H

#include <stdint.h>

#include "param_store.h"

typedef struct
{
    param_id_t params[4];
} ui_param_bank_t;

typedef struct
{
    ui_param_bank_t bank;
    uint8_t valid;
    uint8_t active_track;
    uint8_t shift_down;
} ui_param_encoder_context_t;

typedef struct
{
    param_id_t parameter_id;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    float value;
} ui_param_encoder_target_t;

typedef struct
{
    uint8_t seq_context_active;
    uint8_t has_ref_step;
    uint8_t ref_track;
    uint8_t ref_step;
} ui_param_seq_plock_feedback_frame_t;

typedef enum
{
    UI_PARAM_VALUE_FLASH_DIRECT = 0,
    UI_PARAM_VALUE_FLASH_PLOCK,
    UI_PARAM_VALUE_FLASH_LIVE_REC_PLOCK,
    UI_PARAM_VALUE_FLASH_MACRO_SCENE_ASSIGN
} ui_param_value_flash_kind_t;

void ui_param_set_bank(const ui_param_bank_t *bank);
void ui_param_invalidate_bank(void);
void ui_param_clear_value_flash(void);
void ui_param_sync_active_bank_values(void);
void ui_param_sync_active_track_mirror_from_runtime(void);
void ui_param_capture_encoder_context(ui_param_encoder_context_t *out_ctx);
void ui_param_capture_encoder_context_for_state(ui_param_encoder_context_t *out_ctx,
                                                uint8_t active_track,
                                                uint8_t shift_down);
void ui_param_publish_encoder_binding(uint8_t active_track, uint8_t shift_down);
void ui_param_begin_encoder_edit_group(const ui_param_encoder_context_t *ctx);
void ui_param_end_encoder_edit_group(void);
void ui_param_note_user_value_flash(uint8_t slot,
                                    param_id_t param,
                                    uint8_t track,
                                    float value,
                                    ui_param_value_flash_kind_t kind);
uint8_t ui_param_get_slot_value_flash(uint8_t slot,
                                      param_id_t param,
                                      uint8_t track,
                                      float *out_value,
                                      ui_param_value_flash_kind_t *out_kind);
void ui_param_note_user_tweak(uint8_t slot, param_id_t param);
uint8_t ui_param_is_user_tweak_active(uint8_t slot, param_id_t param);
uint8_t ui_param_get_active_bank_param(uint8_t encoder, param_id_t *out_param);
uint8_t ui_param_handle_encoder_with_context(const ui_param_encoder_context_t *ctx,
                                             uint8_t encoder,
                                             int16_t delta);
uint8_t ui_param_resolve_encoder_detent(const ui_param_encoder_context_t *ctx,
                                        uint8_t encoder,
                                        int8_t direction,
                                        float current_value,
                                        ui_param_encoder_target_t *out_target);
void ui_param_handle_encoder(uint8_t encoder, int16_t delta);
float ui_param_get_active_track_display_value(param_id_t param, uint8_t active_track);
uint8_t ui_param_get_audio_owned_command_value(param_id_t param,
                                               uint8_t track,
                                               float *out_value);
uint8_t ui_param_accept_audio_owned_command(param_id_t param,
                                            uint8_t scope,
                                            uint8_t track,
                                            float value);
uint8_t ui_param_resolve_encoder_detent_from_binding(param_id_t param,
                                                     uint8_t scope,
                                                     uint8_t track,
                                                     uint8_t slot,
                                                     uint8_t shift_down,
                                                     int8_t direction,
                                                     float current_value,
                                                     ui_param_encoder_target_t *out_target);
void ui_param_seq_plock_feedback_frame_begin(ui_param_seq_plock_feedback_frame_t *frame_ctx);
uint8_t ui_param_try_get_seq_plock_feedback_with_frame(const ui_param_seq_plock_feedback_frame_t *frame_ctx,
                                                       param_id_t param,
                                                       float *out_value,
                                                       uint8_t *out_inverted);
uint8_t ui_param_try_get_seq_plock_feedback(param_id_t param, float *out_value, uint8_t *out_inverted);

#endif /* UI_PARAM_H */
