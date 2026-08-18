#pragma once

#include <stdint.h>
#include "audio_float.h"
#include "Seq/seq_types.h"

/**
 * @file mixer.h
 * @brief Interface du moteur de mixage final (tracks/inserts/sends/routing).
 *
 * Rôle du module:
 * - Exposer la configuration runtime du mixer.
 * - Exécuter le mix final MAIN par bloc audio.
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c, callback DSP principal.
 * - Appelle: fx_chain/fx_pool via implémentation mixer.c.
 *
 * Contraintes temps réel:
 * - mixer_process(): IRQ audio, hard realtime.
 * - malloc: interdit.
 */

/*
 * Mixer runtime owns one logical lane per sequencer track. This is distinct
 * from the 4 physical DSP ingress tracks exposed by audio_float.h.
 */
/* 16 source lanes plus one post-sum GROUP bus. */
#define MIXER_MAX_TRACKS (SEQ_LANE_CAPACITY + 1U)
#define MIXER_GROUP_BUS_TRACK SEQ_LANE_CAPACITY
#define MIXER_NUM_SENDS 3U
#define MIXER_MODFX_SEND_INDEX 2U
#define MIXER_INSERTS_PER_TRACK 2U
/* Nominal per-track trim for dry bus summing headroom. */
#define MIXER_TRACK_NOMINAL_TRIM 0.125f

typedef struct
{
    uint8_t reverb_active;
    uint8_t delay_active;
    uint8_t delay_type;
    uint8_t _pad;
    float reverb_wet;
    float delay_volume;
    float delay_reverb_send;
} mixer_global_diag_state_t;

struct multi_voice_dsp_slot_t;

typedef enum
{
    MIXER_ROUTE_NONE = 0,
    MIXER_ROUTE_MASTER = 1
} mixer_route_t;

void mixer_init(void);
void mixer_reset_runtime_state(void);
void mixer_set_master(float gain);
float mixer_get_master(void);
void mixer_get_global_diag_state(mixer_global_diag_state_t *out);

void mixer_set_track_gain(uint32_t track_id, float gain);
float mixer_get_track_gain(uint32_t track_id);

void mixer_set_track_pan(uint32_t track_id, float pan);
void mixer_set_track_mute(uint32_t track_id, uint8_t mute);
uint8_t mixer_get_track_mute(uint32_t track_id);
void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level);
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot);
void mixer_set_reverb_wet(float wet);
void mixer_set_reverb_room_size(float room_size);
void mixer_set_reverb_damping(float damping);
void mixer_set_reverb_width(float width);
void mixer_set_reverb_hpf(float hpf);
void mixer_set_reverb_lpf(float lpf);
void mixer_set_reverb_delays(uint8_t tbd);
void mixer_set_delay_type(uint8_t type);
void mixer_set_delay_mode(uint8_t mode);
void mixer_set_delay_time(float time_s);
void mixer_set_delay_time_r(float time_s);
void mixer_set_delay_feedback(float feedback);
void mixer_set_delay_spectral_position(float position);
void mixer_set_delay_spectral_width(float width);
void mixer_set_delay_pingpong(uint8_t enabled);
void mixer_set_delay_width(float width);
void mixer_set_delay_feedback_width(float width);
void mixer_set_delay_mod_depth(float depth);
void mixer_set_delay_mod_rate(float rate_hz);
void mixer_set_delay_reverb_send(float reverb_send);
void mixer_set_delay_volume(float volume);
void mixer_set_track_filter_morph(uint32_t track_id, float morph);
void mixer_set_track_filter_mode(uint32_t track_id, uint8_t mode);
void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz);
void mixer_set_track_filter_cutoff_modulated(uint32_t track_id, float cutoff_hz);
void mixer_set_track_filter_resonance(uint32_t track_id, float resonance);
void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount);
void mixer_set_track_filter_attack(uint32_t track_id, float attack_s);
void mixer_set_track_filter_decay(uint32_t track_id, float decay_s);
void mixer_set_track_filter_sustain(uint32_t track_id, float sustain);
void mixer_set_track_filter_release(uint32_t track_id, float release_s);
void mixer_set_track_filter_keytrack(uint32_t track_id, float amount);
void mixer_set_track_filter_env_reset(uint32_t track_id, uint8_t enabled);
void mixer_set_track_filter_env_delay(uint32_t track_id, float delay_s);
void mixer_set_track_filter_retrigger_hard(uint32_t track_id, uint8_t hard);
void mixer_set_track_vca_attack(uint32_t track_id, float attack_s);
void mixer_set_track_vca_decay(uint32_t track_id, float decay_s);
void mixer_set_track_vca_sustain(uint32_t track_id, float sustain);
void mixer_set_track_vca_release(uint32_t track_id, float release_s);
void mixer_set_track_vca_enabled(uint32_t track_id, uint8_t enabled);
void mixer_set_track_vca_retrigger_hard(uint32_t track_id, uint8_t hard);
/*
 * These APIs own the existing paraphonic track VCA. They remain valid for
 * the current sampler path, but are not the per-occurrence Multi handle
 * contract and must not become the ownership authority for future Multi DSP
 * slots.
 */
void mixer_track_vca_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity);
void mixer_track_vca_note_off(uint32_t track_id, uint8_t midi_note);
void mixer_track_vca_all_notes_off(uint32_t track_id);
uint8_t mixer_track_vca_is_running(uint32_t track_id);
uint8_t mixer_track_vca_requires_source(uint32_t track_id);
float mixer_get_track_vca_env_value(uint32_t track_id);
float mixer_get_track_filter_env_value(uint32_t track_id);
float mixer_prepare_track_filter_env_source(uint32_t track_id, uint32_t frames);
void mixer_multi_filter_note_on(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                uint8_t midi_note);
void mixer_multi_filter_note_off(struct multi_voice_dsp_slot_t *slot);
uint8_t mixer_multi_voice_vca_requires_source(
    const struct multi_voice_dsp_slot_t *slot);
void mixer_multi_filter_prepare_voice_block(uint32_t track_id,
                                            struct multi_voice_dsp_slot_t *slot);
void mixer_multi_filter_set_voice_cutoff(struct multi_voice_dsp_slot_t *slot, float cutoff_hz);
void mixer_multi_filter_set_voice_resonance(struct multi_voice_dsp_slot_t *slot, float resonance);
void mixer_multi_filter_set_voice_eg_amount(struct multi_voice_dsp_slot_t *slot, float amount);
void mixer_multi_filter_set_voice_env_attack(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_set_voice_env_decay(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_set_voice_env_sustain(struct multi_voice_dsp_slot_t *slot, float sustain);
void mixer_multi_filter_set_voice_env_release(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_set_voice_vca_attack(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_set_voice_vca_decay(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_set_voice_vca_sustain(struct multi_voice_dsp_slot_t *slot, float sustain);
void mixer_multi_filter_set_voice_vca_release(struct multi_voice_dsp_slot_t *slot, float seconds);
void mixer_multi_filter_process_prepared(uint32_t track_id,
                                         struct multi_voice_dsp_slot_t *slot,
                                         float *left,
                                         float *right,
                                         uint32_t frames);
void mixer_multi_filter_process_mono_prepared(uint32_t track_id,
                                              struct multi_voice_dsp_slot_t *slot,
                                              float *mono,
                                              uint32_t frames);
void mixer_multi_filter_process(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                float *left,
                                float *right,
                                uint32_t frames);
void mixer_multi_filter_process_mono(uint32_t track_id,
                                     struct multi_voice_dsp_slot_t *slot,
                                     float *mono,
                                     uint32_t frames);
/* Track filter gate; Multi per-voice filter state is a separate contract. */
void mixer_track_filter_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity);
void mixer_track_filter_note_off(uint32_t track_id, uint8_t midi_note);
void mixer_track_filter_all_notes_off(uint32_t track_id);
void mixer_rebind_track_states(const uint8_t *previous_mix_tracks,
                               const uint8_t *next_mix_tracks,
                               uint32_t track_count);
void mixer_rebind_track_state(uint8_t previous_mix_track, uint8_t next_mix_track);
void mixer_snap_track_runtime_state(uint32_t track_id);
void mixer_external_inputs_clear(void);
void mixer_submit_external_mono_native(uint32_t track_id, const float *mono, uint32_t frames);
void mixer_submit_external_stereo(uint32_t track_id, const float *left, const float *right, uint32_t frames);
uint8_t mixer_begin_external_mono_native(uint32_t track_id,
                                         uint32_t frames,
                                         float **out_mono);
void mixer_commit_external_mono_native(uint32_t track_id, uint32_t frames);
uint8_t mixer_begin_external_multi_mono(uint32_t track_id,
                                        uint32_t frames,
                                        float **out_mono);
void mixer_commit_external_multi_mono(uint32_t track_id, uint32_t frames);
uint8_t mixer_begin_external_poly(uint32_t track_id, uint32_t frames);
uint8_t mixer_process_external_poly_voice(uint32_t mix_track_id,
                                          uint32_t poly_track_id,
                                          uint8_t voice,
                                          float *mono,
                                          uint32_t frames,
                                          float voice_pan);
uint8_t mixer_process_external_poly_voice_prepared(uint32_t mix_track_id,
                                                   uint32_t poly_track_id,
                                                   uint8_t voice,
                                                   float *mono,
                                                   uint32_t frames,
                                                   float voice_pan);
void mixer_prepare_external_poly_voice(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice);
void mixer_invalidate_external_poly_track(uint32_t poly_track_id);
void mixer_poly_voice_set_cutoff(uint8_t voice_slot, float cutoff_hz);
void mixer_poly_voice_set_resonance(uint8_t voice_slot, float resonance);
void mixer_poly_voice_set_eg_amount(uint8_t voice_slot, float amount);
void mixer_poly_voice_set_filter_attack(uint8_t voice_slot, float seconds);
void mixer_poly_voice_set_filter_decay(uint8_t voice_slot, float seconds);
void mixer_poly_voice_set_filter_sustain(uint8_t voice_slot, float sustain);
void mixer_poly_voice_set_filter_release(uint8_t voice_slot, float seconds);
void mixer_poly_voice_set_vca_attack(uint8_t voice_slot, float seconds);
void mixer_poly_voice_set_vca_decay(uint8_t voice_slot, float seconds);
void mixer_poly_voice_set_vca_sustain(uint8_t voice_slot, float sustain);
void mixer_poly_voice_set_vca_release(uint8_t voice_slot, float seconds);
void mixer_commit_external_poly(uint32_t track_id, uint32_t frames);
void mixer_track_poly_note_on(uint32_t poly_track_id,
                              uint32_t mix_track_id,
                              uint8_t voice,
                              uint8_t note,
                              uint8_t velocity);
void mixer_track_poly_note_off(uint32_t poly_track_id,
                               uint8_t voice,
                               uint8_t note);
void mixer_track_poly_all_notes_off(uint32_t poly_track_id);
void mixer_synth_voice_slot_reset(uint8_t slot);
void mixer_synth_voice_slot_copy(uint8_t source_slot, uint8_t destination_slot);
void mixer_track_voice_state_to_poly(uint32_t mix_track_id,
                                     uint32_t poly_track_id,
                                     uint8_t voice);
void mixer_track_voice_state_from_poly(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice);
uint8_t mixer_begin_external_stereo(uint32_t track_id,
                                    uint32_t frames,
                                    float **out_left,
                                    float **out_right);
void mixer_commit_external_stereo(uint32_t track_id, uint32_t frames);
uint8_t mixer_begin_external_multi_stereo(uint32_t track_id,
                                          uint32_t frames,
                                          float **out_left,
                                          float **out_right);
void mixer_commit_external_multi_stereo(uint32_t track_id, uint32_t frames);

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
