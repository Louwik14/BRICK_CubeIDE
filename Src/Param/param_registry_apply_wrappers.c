#include "Param/param_registry_apply_bindings.h"
#include "Param/param_registry.h"
#include "Core/track_state.h"
#include "Core/track_runtime.h"
#include "audio_float.h"
#include "Audio/metronome_runtime.h"
#include "Keyboard/keyboard_runtime.h"
#include "fx_daisy_comp.h"
#include "fx_pool.h"
#include "mixer.h"
#include "ui_core.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Storage/undo_v2.h"
#include "Storage/kit_v1.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "UI/ui_track_catalog.h"

static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int8_t control_float_to_slot(float v)
{
    if (v < 0.0f)
        return -1;
    return (int8_t)v;
}

static uint8_t control_float_to_ui127(float v)
{
    if (v <= 0.0f)
        return 0U;
    if (v >= 1.0f)
        return 127U;
    return (uint8_t)(v * 127.0f + 0.5f);
}

static void apply_tone_live_track(param_id_t id, float value)
{
    /* Explicit apply seam: live UI edits route to the track-aware mutation surface. */
    (void)param_registry_apply_track_value(id, ui_get_active_track(), value);
}

static uint8_t seq_div_ui_to_runtime(float v)
{
    const uint8_t ui = (uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f);
    switch (ui)
    {
        case 1U: return 2U;
        case 2U: return 4U;
        case 3U: return 8U;
        case 0U:
        default: return 1U;
    }
}

static uint32_t delay_time_get_bpm_milli(void)
{
    uint32_t bpm_milli = 120000U;
    if ((seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL)
        && (seq_runtime_is_external_tempo_valid() != 0U))
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
    }
    else
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
    }

    if (bpm_milli < 40000U)
        return 40000U;
    if (bpm_milli > 300000U)
        return 300000U;
    return bpm_milli;
}

static float delay_time_sync_index_to_seconds(float v)
{
    static const float beats[] = {
        0.125f,      /* 1/32 */
        0.1666667f,  /* 1/16T */
        0.25f,       /* 1/16 */
        0.3333333f,  /* 1/8T */
        0.5f,        /* 1/8 */
        0.6666667f,  /* 1/4T */
        0.75f,       /* 1/8D */
        1.0f,        /* 1/4 */
        1.3333334f,  /* 1/2T */
        1.5f,        /* 1/4D */
        2.0f,        /* 1/2 */
        3.0f,        /* 1D */
        4.0f         /* 1 bar */
    };
    const uint8_t index = (uint8_t)(clamp_value(v, 0.0f, 12.0f) + 0.5f);
    return beats[index] * 60000.0f / (float)delay_time_get_bpm_milli();
}

volatile uint32_t g_param_cfg_track_type_apply_stage = 0U;

void apply_mix_send0_fx(float v) { mixer_set_send_fx_slot(0U, control_float_to_slot(v)); }
void apply_mix_send1_fx(float v) { mixer_set_send_fx_slot(1U, control_float_to_slot(v)); }

void apply_mix_reverb_wet(float v) { mixer_set_reverb_wet(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_size(float v) { mixer_set_reverb_size(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_decay(float v) { mixer_set_reverb_decay(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_pred(float v) { mixer_set_reverb_pre_delay(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_type(float v) { (void)v; mixer_set_reverb_type(0U); }
void apply_mix_reverb_surr(float v) { mixer_set_reverb_surround(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_hpf(float v) { mixer_set_reverb_hpf(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_reverb_lpf(float v) { mixer_set_reverb_lpf(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_delay_type(float v) { mixer_set_delay_type((uint8_t)(clamp_value(v, 0.0f, 1.0f) + 0.5f)); }
void apply_mix_delay_mode(float v) { mixer_set_delay_mode((uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f)); }
void apply_mix_delay_time(float v) { mixer_set_delay_time(delay_time_sync_index_to_seconds(v)); }
void apply_mix_delay_time_r(float v) { mixer_set_delay_time_r(delay_time_sync_index_to_seconds(v)); }
void apply_mix_delay_feedback(float v) { mixer_set_delay_feedback(clamp_value(v, 0.0f, 1.20f)); }
void apply_mix_delay_hpf(float v) { mixer_set_delay_hpf(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_delay_lpf(float v) { mixer_set_delay_lpf(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_delay_pingpong(float v) { mixer_set_delay_pingpong((v >= 0.5f) ? 1U : 0U); }
void apply_mix_delay_rev(float v) { mixer_set_delay_reverb_send(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_delay_width(float v) { mixer_set_delay_width(clamp_value(v, -1.0f, 1.0f)); }
void apply_mix_delay_feedback_width(float v) { mixer_set_delay_feedback_width(clamp_value(v, -1.0f, 1.0f)); }
void apply_mix_delay_mod(float v) { mixer_set_delay_mod_depth(clamp_value(v, 0.0f, 1.0f)); }
void apply_mix_delay_mod_rate(float v) { mixer_set_delay_mod_rate(clamp_value(v, 0.01f, 12.0f)); }
void apply_mix_delay_vol(float v) { mixer_set_delay_volume(clamp_value(v, 0.0f, 1.0f)); }

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
void apply_sampler_end(float v) { apply_tone_live_track(PARAM_SAMPLER_END, v); }
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

void apply_gran_density(float v)
{
    (void)v;
}

void apply_gran_pitch(float v)
{
    (void)v;
}

void apply_gran_mix(float v)
{
    (void)v;
}

void apply_gran_freeze(float v)
{
    (void)v;
}

void apply_gran_spread(float v)
{
    (void)v;
}

void apply_gran_stereo(float v)
{
    (void)v;
}

void apply_eq_low_db(float v) { audio_float_set_dj_eq_low_db(v); }
void apply_eq_mid_db(float v) { audio_float_set_dj_eq_mid_db(v); }
void apply_eq_high_db(float v) { audio_float_set_dj_eq_high_db(v); }

void apply_sat_tone(float v) { audio_float_set_saturation_tone_ui(control_float_to_ui127(v)); }
void apply_sat_bias(float v) { audio_float_set_saturation_bias_ui(control_float_to_ui127(v)); }
void apply_sat_drive(float v) { audio_float_set_saturation_drive_ui(control_float_to_ui127(v)); }
void apply_sat_mix(float v) { audio_float_set_saturation_mix_ui(control_float_to_ui127(v)); }

static void apply_lfo_active_track(uint8_t lfo, mod_lfo_param_t param, float v)
{
    if (mod_lfo_v1_set_track_param(ui_get_active_track(), lfo, param, v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_lfo1_rate(float v) { apply_lfo_active_track(0U, MOD_LFO_PARAM_RATE, v); }
void apply_lfo1_shape(float v) { apply_lfo_active_track(0U, MOD_LFO_PARAM_SHAPE, v); }
void apply_lfo1_trig(float v) { apply_lfo_active_track(0U, MOD_LFO_PARAM_TRIG, v); }
void apply_lfo1_phase(float v) { apply_lfo_active_track(0U, MOD_LFO_PARAM_PHASE, v); }
void apply_lfo2_rate(float v) { apply_lfo_active_track(1U, MOD_LFO_PARAM_RATE, v); }
void apply_lfo2_shape(float v) { apply_lfo_active_track(1U, MOD_LFO_PARAM_SHAPE, v); }
void apply_lfo2_trig(float v) { apply_lfo_active_track(1U, MOD_LFO_PARAM_TRIG, v); }
void apply_lfo2_phase(float v) { apply_lfo_active_track(1U, MOD_LFO_PARAM_PHASE, v); }
void apply_lfo3_rate(float v) { apply_lfo_active_track(2U, MOD_LFO_PARAM_RATE, v); }
void apply_lfo3_shape(float v) { apply_lfo_active_track(2U, MOD_LFO_PARAM_SHAPE, v); }
void apply_lfo3_trig(float v) { apply_lfo_active_track(2U, MOD_LFO_PARAM_TRIG, v); }
void apply_lfo3_phase(float v) { apply_lfo_active_track(2U, MOD_LFO_PARAM_PHASE, v); }

void apply_mod_matrix_slot(float v)
{
    if (mod_matrix_set_selected_slot(ui_get_active_track(), v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_mod_matrix_source(float v)
{
    if (mod_matrix_set_selected_slot_source(ui_get_active_track(), v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_mod_matrix_dest(float v)
{
    if (mod_matrix_set_selected_slot_destination_index(ui_get_active_track(), v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_mod_matrix_depth(float v)
{
    if (mod_matrix_set_selected_slot_depth(ui_get_active_track(), v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

static void apply_mod_multi_active_track(uint8_t op, uint8_t input, float v)
{
    if (mod_matrix_set_multi_source(ui_get_active_track(), op, input, v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_mod_multi_1_a(float v) { apply_mod_multi_active_track(0U, 0U, v); }
void apply_mod_multi_1_b(float v) { apply_mod_multi_active_track(0U, 1U, v); }
void apply_mod_multi_2_a(float v) { apply_mod_multi_active_track(1U, 0U, v); }
void apply_mod_multi_2_b(float v) { apply_mod_multi_active_track(1U, 1U, v); }

static void apply_mod_slew_source_active_track(uint8_t op, float v)
{
    if (mod_matrix_set_slew_source(ui_get_active_track(), op, v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

static void apply_mod_slew_amount_active_track(uint8_t op, float v)
{
    if (mod_matrix_set_slew_amount(ui_get_active_track(), op, v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_mod_slew_1_source(float v) { apply_mod_slew_source_active_track(0U, v); }
void apply_mod_slew_1_amount(float v) { apply_mod_slew_amount_active_track(0U, v); }
void apply_mod_slew_2_source(float v) { apply_mod_slew_source_active_track(1U, v); }
void apply_mod_slew_2_amount(float v) { apply_mod_slew_amount_active_track(1U, v); }

static void apply_env3_active_track(mod_env3_param_t param, float v)
{
    if (mod_env3_set_track_param(ui_get_active_track(), param, v) != 0U)
    {
        kit_v1_mark_dirty();
    }
}

void apply_env3_attack(float v) { apply_env3_active_track(MOD_ENV3_PARAM_ATTACK, v); }
void apply_env3_decay(float v) { apply_env3_active_track(MOD_ENV3_PARAM_DECAY, v); }
void apply_env3_sustain(float v) { apply_env3_active_track(MOD_ENV3_PARAM_SUSTAIN, v); }
void apply_env3_release(float v) { apply_env3_active_track(MOD_ENV3_PARAM_RELEASE, v); }

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
    kit_v1_mark_dirty();
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
    kit_v1_mark_dirty();
    g_param_cfg_track_type_apply_stage = 4U;
}

void apply_cfg_midi_ch(float v)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        param_store_set_active(PARAM_CFG_MIDI_CH, (float)track_state_get_midi_channel(active_track));
        return;
    }

    const uint8_t requested_channel = (uint8_t)(clamp_value(v, 1.0f, 16.0f) + 0.5f);
    (void)ui_set_track_midi_channel(active_track, requested_channel);
    param_store_set_active(PARAM_CFG_MIDI_CH, (float)track_state_get_midi_channel(active_track));
}

void apply_cfg_midi_src(float v)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        param_store_set_active(PARAM_CFG_MIDI_SRC, (float)track_state_get_midi_source(active_track));
        return;
    }

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
    apply_mix_delay_time(param_get(PARAM_MIX_DELAY_TIME));
    apply_mix_delay_time_r(param_get(PARAM_MIX_DELAY_TIME_R));
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
    apply_mix_delay_time(param_get(PARAM_MIX_DELAY_TIME));
    apply_mix_delay_time_r(param_get(PARAM_MIX_DELAY_TIME_R));
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

    const uint8_t gesture_key = (0x50000000UL
                                 | ((uint32_t)track << 16)
                                 | (uint32_t)((uint8_t)(v + 0.5f)));
    const uint8_t undo_started = (undo_v2_begin_snapshot_transaction(UNDO_V2_SOURCE_SYSTEM,
                                                                     gesture_key) == UNDO_V2_STATUS_OK)
                                 && (undo_v2_capture_snapshot_before() == UNDO_V2_STATUS_OK);
    seq_model_set_track_length(track, (uint8_t)(v + 0.5f));
    seq_runtime_on_track_length_changed(track);
    if (undo_started != 0U)
    {
        if (undo_v2_capture_snapshot_after() != UNDO_V2_STATUS_OK)
        {
            undo_v2_cancel_transaction();
        }
        else
        {
            (void)undo_v2_commit_transaction();
        }
    }
}

void apply_seq_div(float v)
{
    /* Command surface: track div is written explicitly on the active track. */
    const uint8_t track = ui_get_active_track();
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) == 0U)
    {
        seq_runtime_set_track_div(track, seq_div_ui_to_runtime(v));
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
void apply_arp_hold(float v) { keyboard_runtime_set_arp_hold(v >= 0.5f); }
void apply_arp_rate(float v) { keyboard_runtime_set_arp_rate((uint8_t)(clamp_value(v, 0.0f, 7.0f) + 0.5f)); }
void apply_arp_oct(float v) { keyboard_runtime_set_arp_oct((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
void apply_arp_pattern(float v) { keyboard_runtime_set_arp_pattern((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
void apply_arp_gate(float v) { keyboard_runtime_set_arp_gate((uint8_t)(clamp_value(v, 1.0f, 127.0f) + 0.5f)); }
void apply_arp_swing(float v) { keyboard_runtime_set_arp_swing((uint8_t)(clamp_value(v, 0.0f, 100.0f) + 0.5f)); }
void apply_arp_accent(float v) { keyboard_runtime_set_arp_accent((uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f)); }
void apply_arp_vel_acc(float v) { keyboard_runtime_set_arp_vel_acc((uint8_t)(clamp_value(v, 0.0f, 64.0f) + 0.5f)); }
void apply_arp_strum(float v) { keyboard_runtime_set_arp_strum((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
void apply_arp_offset(float v) { keyboard_runtime_set_arp_offset((int8_t)(clamp_value(v, -24.0f, 24.0f) + ((v >= 0.0f) ? 0.5f : -0.5f))); }
void apply_arp_trans(float v) { keyboard_runtime_set_arp_transpose((int8_t)(clamp_value(v, -24.0f, 24.0f) + ((v >= 0.0f) ? 0.5f : -0.5f))); }
void apply_arp_spread(float v) { keyboard_runtime_set_arp_spread((uint8_t)(clamp_value(v, 0.0f, 12.0f) + 0.5f)); }
void apply_arp_dir(float v) { keyboard_runtime_set_arp_dir((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f)); }
void apply_arp_sync(float v) { keyboard_runtime_set_arp_sync((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f)); }

void apply_master_gain(float v) { (void)v; }
void apply_post_gain(float v) { audio_float_set_postgain(v); }
void apply_output_comp(float v) { audio_float_set_output_compensation(v); }

void apply_bus_comp_threshold(float v) { audio_float_set_bus_comp_threshold_db(v); }
void apply_bus_comp_ratio(float v) { audio_float_set_bus_comp_ratio(v); }
void apply_bus_comp_attack_index(float v) { audio_float_set_bus_comp_attack_index((uint8_t)v); }
void apply_bus_comp_release_index(float v) { audio_float_set_bus_comp_release_index((uint8_t)v); }
void apply_bus_comp_makeup(float v) { audio_float_set_bus_comp_makeup_db(v); }
void apply_bus_comp_auto_makeup(float v) { audio_float_set_bus_comp_auto_makeup((v >= 0.5f) ? 1U : 0U); }

void apply_daisy_threshold(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_threshold_db(comp, v);
}

void apply_daisy_ratio(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_ratio(comp, v);
}

void apply_daisy_attack(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_attack_s(comp, v);
}

void apply_daisy_release(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_release_s(comp, v);
}

void apply_daisy_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_makeup_db(comp, v);
}

void apply_daisy_auto_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_auto_makeup(comp, (v >= 0.5f) ? 1U : 0U);
}

void apply_daisy_mix(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_mix(comp, v);
}
