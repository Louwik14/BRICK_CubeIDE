#include "Param/param_registry_apply_bindings.h"
#include "Param/param_registry.h"
#include "Core/track_state.h"
#include "Audio/metronome_runtime.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_core.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Mod/mod_matrix.h"
#include "UI/ui_track_catalog.h"
#include <math.h>

static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void apply_tone_live_track(param_id_t id, float value)
{
    /* Explicit apply seam: live UI edits route to the track-aware mutation surface. */
    (void)param_registry_apply_track_value(id, ui_get_active_track(), value);
}

volatile uint32_t g_param_cfg_track_type_apply_stage = 0U;

void apply_midi_program(float v) { apply_tone_live_track(PARAM_MIDI_PROGRAM, v); }
void apply_sampler_sample(float v) { apply_tone_live_track(PARAM_SAMPLER_SAMPLE, v); }
void apply_sampler_gain(float v) { apply_tone_live_track(PARAM_SAMPLER_GAIN, v); }
void apply_sampler_clip_source_bpm(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_SOURCE_BPM, v); }
void apply_sampler_clip_sync_length(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_SYNC_LENGTH, v); }
void apply_sampler_clip_pitch(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_PITCH, v); }
void apply_sampler_clip_play_mode(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_PLAY_MODE, v); }
void apply_sampler_clip_loop(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_LOOP, v); }
void apply_sampler_clip_stretch_mode(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_STRETCH_MODE, v); }
void apply_sampler_clip_grain(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_GRAIN, v); }
void apply_sampler_clip_hop(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_HOP, v); }
void apply_sampler_clip_search(float v) { apply_tone_live_track(PARAM_SAMPLER_CLIP_SEARCH, v); }
void apply_sampler_start(float v) { apply_tone_live_track(PARAM_SAMPLER_START, v); }
void apply_sampler_length(float v) { apply_tone_live_track(PARAM_SAMPLER_LENGTH, v); }
void apply_sampler_mode(float v) { apply_tone_live_track(PARAM_SAMPLER_MODE, v); }
void apply_sampler_loop_start(float v) { apply_tone_live_track(PARAM_SAMPLER_LOOP_START, v); }
void apply_sampler_tune(float v) { apply_tone_live_track(PARAM_SAMPLER_TUNE, v); }
void apply_sampler_slice_count(float v) { apply_tone_live_track(PARAM_SAMPLER_SLICE_COUNT, v); }
void apply_sampler_multi_loop(float v) { apply_tone_live_track(PARAM_SAMPLER_MULTI_LOOP, v); }
void apply_midi_cc1_1(float v) { apply_tone_live_track(PARAM_MIDI_CC1_1, v); }
void apply_midi_cc1_2(float v) { apply_tone_live_track(PARAM_MIDI_CC1_2, v); }
void apply_midi_cc1_3(float v) { apply_tone_live_track(PARAM_MIDI_CC1_3, v); }
void apply_midi_cc1_4(float v) { apply_tone_live_track(PARAM_MIDI_CC1_4, v); }
void apply_midi_cc2_1(float v) { apply_tone_live_track(PARAM_MIDI_CC2_1, v); }
void apply_midi_cc2_2(float v) { apply_tone_live_track(PARAM_MIDI_CC2_2, v); }
void apply_midi_cc2_3(float v) { apply_tone_live_track(PARAM_MIDI_CC2_3, v); }
void apply_midi_cc2_4(float v) { apply_tone_live_track(PARAM_MIDI_CC2_4, v); }
void apply_midi_cc3_1(float v) { apply_tone_live_track(PARAM_MIDI_CC3_1, v); }
void apply_midi_cc3_2(float v) { apply_tone_live_track(PARAM_MIDI_CC3_2, v); }
void apply_midi_cc3_3(float v) { apply_tone_live_track(PARAM_MIDI_CC3_3, v); }
void apply_midi_cc3_4(float v) { apply_tone_live_track(PARAM_MIDI_CC3_4, v); }

void apply_mod_matrix_slot(float v)
{
    (void)mod_matrix_set_selected_slot(ui_get_active_track(), v);
}

void apply_mod_matrix_source(float v)
{
    (void)mod_matrix_set_selected_slot_source(ui_get_active_track(), v);
}

void apply_mod_matrix_dest(float v)
{
    (void)mod_matrix_set_selected_slot_destination_index(ui_get_active_track(), v);
}

void apply_mod_matrix_depth(float v)
{
    (void)mod_matrix_set_selected_slot_depth(ui_get_active_track(), v);
}

static void apply_mod_multi_active_track(uint8_t op, uint8_t input, float v)
{
    (void)mod_matrix_set_multi_source(ui_get_active_track(), op, input, v);
}

void apply_mod_multi_1_a(float v) { apply_mod_multi_active_track(0U, 0U, v); }
void apply_mod_multi_1_b(float v) { apply_mod_multi_active_track(0U, 1U, v); }
void apply_mod_multi_2_a(float v) { apply_mod_multi_active_track(1U, 0U, v); }
void apply_mod_multi_2_b(float v) { apply_mod_multi_active_track(1U, 1U, v); }

static void apply_mod_slew_source_active_track(uint8_t op, float v)
{
    (void)mod_matrix_set_slew_source(ui_get_active_track(), op, v);
}

static void apply_mod_slew_amount_active_track(uint8_t op, float v)
{
    (void)mod_matrix_set_slew_amount(ui_get_active_track(), op, v);
}

void apply_mod_slew_1_source(float v) { apply_mod_slew_source_active_track(0U, v); }
void apply_mod_slew_1_amount(float v) { apply_mod_slew_amount_active_track(0U, v); }
void apply_mod_slew_2_source(float v) { apply_mod_slew_source_active_track(1U, v); }
void apply_mod_slew_2_amount(float v) { apply_mod_slew_amount_active_track(1U, v); }

void apply_cfg_track(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t previous_family = track_state_get_family(active_track);
    const ui_track_type_t previous_type = track_state_get_type(active_track);
    const ui_track_family_t requested_family = (ui_track_family_t)((uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U)) + 0.5f));

    if (ui_set_track_family(active_track, requested_family) == false)
    {
        param_store_set_active(PARAM_CFG_TRACK, (float)track_state_get_family(active_track));
        param_store_set_active(PARAM_CFG_TRACK_TYPE,
                               (float)ui_track_catalog_type_index_for_family(track_state_get_family(active_track),
                                                                             track_state_get_type(active_track),
                                                                             active_track,
                                                                             track_state_get_configs()));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK, (float)track_state_get_family(active_track));
    param_store_set_active(PARAM_CFG_TRACK_TYPE,
                           (float)ui_track_catalog_type_index_for_family(track_state_get_family(active_track),
                                                                         track_state_get_type(active_track),
                                                                         active_track,
                                                                         track_state_get_configs()));
    if ((track_state_get_family(active_track) == previous_family)
            && (track_state_get_type(active_track) == previous_type))
    {
        return;
    }
}

void apply_cfg_track_type(float v)
{
    g_param_cfg_track_type_apply_stage = 1U;
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t active_family = track_state_get_family(active_track);
    const ui_track_type_t previous_type = track_state_get_type(active_track);
    const uint8_t requested_index = (uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U)) + 0.5f);
    const ui_track_type_t requested_type = ui_track_catalog_type_from_family_index(active_family,
                                                                                   requested_index,
                                                                                   active_track,
                                                                                   track_state_get_configs());

    g_param_cfg_track_type_apply_stage = 2U;

    if (ui_set_track_type(active_track, requested_type) == false)
    {
        g_param_cfg_track_type_apply_stage = 3U;
        param_store_set_active(PARAM_CFG_TRACK_TYPE,
                               (float)ui_track_catalog_type_index_for_family(active_family,
                                                                             track_state_get_type(active_track),
                                                                             active_track,
                                                                             track_state_get_configs()));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK_TYPE,
                           (float)ui_track_catalog_type_index_for_family(active_family,
                                                                         track_state_get_type(active_track),
                                                                         active_track,
                                                                         track_state_get_configs()));
    if (track_state_get_type(active_track) == previous_type)
    {
        g_param_cfg_track_type_apply_stage = 4U;
        return;
    }
    g_param_cfg_track_type_apply_stage = 4U;
}

void apply_cfg_midi_ch(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const uint8_t requested_channel = (uint8_t)(clamp_value(v, 1.0f, 16.0f) + 0.5f);
    (void)ui_set_track_midi_channel(active_track, requested_channel);
    param_store_set_active(PARAM_CFG_MIDI_CH, (float)track_state_get_midi_channel(active_track));
}

void apply_cfg_midi_src(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_midi_source_t requested_source =
            (ui_track_midi_source_t)((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f));
    (void)ui_set_track_midi_source(active_track, requested_source);
    param_store_set_active(PARAM_CFG_MIDI_SRC, (float)track_state_get_midi_source(active_track));
}

void apply_cfg_start(float v)
{
    uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f);
    /* Command surface: REC start mode is written explicitly, then mirrored back for UI/store. */
    seq_runtime_set_rec_start_mode(mode);
    /* Post-apply mirror: runtime getter is read back explicitly, not used as a mutation trigger. */
    mode = seq_runtime_get_rec_start_mode();
    param_store_set_active(PARAM_CFG_START, (float)mode);
}

void apply_cfg_tempo(float v)
{
    uint32_t bpm_milli = (uint32_t)(clamp_value(v, 40.0f, 300.0f) * 1000.0f + 0.5f);
    /* Command surface: tempo is written explicitly, then mirrored back for UI/store. */
    seq_runtime_set_tempo_bpm_milli(bpm_milli);
    /* Post-apply mirror: runtime getter is read back explicitly, not used as a mutation trigger. */
    bpm_milli = seq_runtime_get_tempo_bpm_milli();
    param_store_set_active(PARAM_CFG_TEMPO, (float)bpm_milli / 1000.0f);
    param_set(PARAM_MIX_DELAY_TIME, param_get(PARAM_MIX_DELAY_TIME));
    param_set(PARAM_MIX_DELAY_TIME_R, param_get(PARAM_MIX_DELAY_TIME_R));
}

void apply_cfg_sync(float v)
{
    const uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f);
    seq_clock_src_t source = SEQ_CLOCK_SRC_INTERNAL;
    if (mode == 1U)
    {
        source = SEQ_CLOCK_SRC_EXTERNAL_MIDI;
    }
    else if (mode == 2U)
    {
        source = SEQ_CLOCK_SRC_EXTERNAL_USB;
    }

    /* Command surface: clock source is written explicitly, then mirrored back for UI/store. */
    seq_runtime_set_clock_source(source);

    uint8_t synced_mode = 0U;
    /* Post-apply mirror: runtime getter is read back explicitly, not used as a mutation trigger. */
    switch (seq_runtime_get_clock_source())
    {
        case SEQ_CLOCK_SRC_EXTERNAL_MIDI:
            synced_mode = 1U;
            break;
        case SEQ_CLOCK_SRC_EXTERNAL_USB:
            synced_mode = 2U;
            break;
        case SEQ_CLOCK_SRC_INTERNAL:
        default:
            synced_mode = 0U;
            break;
    }
    param_store_set_active(PARAM_CFG_SYNC, (float)synced_mode);
    param_set(PARAM_MIX_DELAY_TIME, param_get(PARAM_MIX_DELAY_TIME));
    param_set(PARAM_MIX_DELAY_TIME_R, param_get(PARAM_MIX_DELAY_TIME_R));
}

void apply_cfg_rec_len(float v)
{
    uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 1.0f) + 0.5f);
    /* Command surface: rec-length mode is written explicitly, then mirrored back for UI/store. */
    seq_runtime_set_rec_len_mode(mode);
    /* Post-apply mirror: runtime getter is read back explicitly, not used as a mutation trigger. */
    mode = seq_runtime_get_rec_len_mode();
    param_store_set_active(PARAM_CFG_REC_LEN, (float)mode);
}

void apply_cfg_metro(float v)
{
    uint8_t level = (uint8_t)(clamp_value(v, 0.0f, 127.0f) + 0.5f);
    metronome_runtime_set_level_u7(level);
    param_store_set_active(PARAM_CFG_METRO, (float)level);
}

void apply_seq_length(float v)
{
    const uint8_t track = ui_get_active_track();
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) != 0U)
    {
        return;
    }

    seq_model_set_track_length(track, (uint8_t)(v + 0.5f));
    seq_runtime_on_track_length_changed(track);
}

void apply_seq_div(float v)
{
    /* Command surface: track div is written explicitly on the active track. */
    const uint8_t track = ui_get_active_track();
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) == 0U)
    {
        seq_runtime_set_track_div(track,
                                   seq_division_track_div_from_ui((uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f)));
    }
}

void apply_seq_quant(float v)
{
    /* Command surface: track quant is written explicitly on the active track. */
    const uint8_t track = ui_get_active_track();
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) == 0U)
    {
        seq_runtime_set_track_quant(track, (uint8_t)(v + 0.5f));
    }
}

void apply_seq_swing(float v)
{
    /* Command surface: track swing is written explicitly on the active track. */
    const uint8_t track = ui_get_active_track();
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) == 0U)
    {
        seq_runtime_set_track_swing(track, (uint8_t)(v + 0.5f));
    }
}

void apply_kbd_root(float v) { keyboard_runtime_set_root((uint8_t)(clamp_value(v, 0.0f, 11.0f) + 0.5f)); }
void apply_kbd_scale(float v) { keyboard_runtime_set_scale((uint8_t)(clamp_value(v, 0.0f, (float)KBD_SCALE_CHROMATIC) + 0.5f)); }
void apply_kbd_omnichord(float v) { keyboard_runtime_set_omnichord(v >= 0.5f); }
void apply_kbd_note_order(float v) { keyboard_runtime_set_note_order((v >= 0.5f) ? NOTE_ORDER_FIFTHS : NOTE_ORDER_NATURAL); }
void apply_kbd_chord_override(float v) { keyboard_runtime_set_chord_override(v >= 0.5f); }
void apply_kbd_mono_last(float v) { keyboard_runtime_set_mono_last(v >= 0.5f); }
