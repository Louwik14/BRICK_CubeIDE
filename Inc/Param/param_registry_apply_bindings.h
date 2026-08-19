#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void apply_cfg_midi_ch(float v);
void apply_cfg_midi_src(float v);
void apply_cfg_start(float v);
void apply_cfg_rec_len(float v);
void apply_cfg_metro(float v);
void apply_cfg_sync(float v);
void apply_cfg_tempo(float v);
void apply_cfg_track(float v);
void apply_cfg_track_type(float v);
void apply_kbd_chord_override(float v);
void apply_kbd_note_order(float v);
void apply_kbd_mono_last(float v);
void apply_kbd_omnichord(float v);
void apply_kbd_root(float v);
void apply_kbd_scale(float v);
void apply_mod_matrix_slot(float v);
void apply_mod_matrix_source(float v);
void apply_mod_matrix_dest(float v);
void apply_mod_matrix_depth(float v);
void apply_mod_multi_1_a(float v);
void apply_mod_multi_1_b(float v);
void apply_mod_multi_2_a(float v);
void apply_mod_multi_2_b(float v);
void apply_mod_slew_1_source(float v);
void apply_mod_slew_1_amount(float v);
void apply_mod_slew_2_source(float v);
void apply_mod_slew_2_amount(float v);
void apply_midi_cc1_1(float v);
void apply_midi_cc1_2(float v);
void apply_midi_cc1_3(float v);
void apply_midi_cc1_4(float v);
void apply_midi_cc2_1(float v);
void apply_midi_cc2_2(float v);
void apply_midi_cc2_3(float v);
void apply_midi_cc2_4(float v);
void apply_midi_cc3_1(float v);
void apply_midi_cc3_2(float v);
void apply_midi_cc3_3(float v);
void apply_midi_cc3_4(float v);
void apply_midi_program(float v);
void apply_sampler_end(float v);
void apply_sampler_gain(float v);
void apply_sampler_clip_play_mode(float v);
void apply_sampler_clip_loop(float v);
void apply_sampler_clip_stretch_mode(float v);
void apply_sampler_clip_grain(float v);
void apply_sampler_clip_hop(float v);
void apply_sampler_clip_search(float v);
void apply_sampler_clip_source_bpm(float v);
void apply_sampler_clip_sync_length(float v);
void apply_sampler_clip_pitch(float v);
void apply_sampler_mode(float v);
void apply_sampler_loop_start(float v);
void apply_sampler_sample(float v);
void apply_sampler_slice_count(float v);
void apply_sampler_start(float v);
void apply_sampler_tune(float v);
void apply_sampler_multi_loop(float v);
void apply_seq_div(float v);
void apply_seq_length(float v);
void apply_seq_quant(float v);
void apply_seq_swing(float v);

#ifdef __cplusplus
}
#endif
