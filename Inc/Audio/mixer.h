#pragma once

#include <stdint.h>
#include "audio_float.h"
#include "Seq/seq_types.h"
#include "fx_dj_eq3_cmsis.h"

/**
 * @file mixer.h
 * @brief Interface du moteur de mixage final (tracks/inserts/sends/routing).
 *
 * Rôle du module:
 * - Exposer la configuration runtime du mixer.
 * - Exécuter le mix final MAIN/CUE par bloc audio.
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c, control_router.c, callback DSP principal.
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
#define MIXER_MAX_TRACKS SEQ_TRACK_COUNT
#define MIXER_NUM_SENDS 2U
#define MIXER_INSERTS_PER_TRACK 2U
/* Nominal per-track trim for dry bus summing headroom. */
#define MIXER_TRACK_NOMINAL_TRIM 0.125f

typedef enum
{
    MIXER_TRACK_FILTER_OFF = 0,
    MIXER_TRACK_FILTER_EQ3,
    MIXER_TRACK_FILTER_LP_BI,
    MIXER_TRACK_FILTER_HP_BI,
    MIXER_TRACK_FILTER_BP_BI
} mixer_track_filter_type_t;

typedef enum
{
    MIXER_ROUTE_NONE = 0,
    MIXER_ROUTE_MASTER = 1,
    MIXER_ROUTE_CUE = 2,
    MIXER_ROUTE_BOTH = 3
} mixer_route_t;

void mixer_init(void);
void mixer_set_master(float gain);
float mixer_get_master(void);

void mixer_set_track_gain(uint32_t track_id, float gain);
float mixer_get_track_gain(uint32_t track_id);

void mixer_set_track_pan(uint32_t track_id, float pan);
void mixer_set_track_mute(uint32_t track_id, uint8_t mute);
uint8_t mixer_get_track_mute(uint32_t track_id);
void mixer_set_track_route(uint32_t track_id, mixer_route_t route);
void mixer_set_track_insert_slot(uint32_t track_id, uint32_t insert_idx, int8_t slot);
void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level);
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot);
void mixer_set_reverb_wet(float wet);
void mixer_set_reverb_size(float size);
void mixer_set_reverb_decay(float decay);
void mixer_set_reverb_pre_delay(float pre_delay);
void mixer_set_reverb_surround(float surround);
void mixer_set_reverb_type(uint8_t type);
void mixer_set_reverb_hpf(float hpf);
void mixer_set_reverb_lpf(float lpf);
void mixer_set_delay_type(uint8_t type);
void mixer_set_delay_mode(uint8_t mode);
void mixer_set_delay_time(float time_s);
void mixer_set_delay_time_r(float time_s);
void mixer_set_delay_feedback(float feedback);
void mixer_set_delay_hpf(float hpf);
void mixer_set_delay_lpf(float lpf);
void mixer_set_delay_pingpong(uint8_t enabled);
void mixer_set_delay_width(float width);
void mixer_set_delay_feedback_width(float width);
void mixer_set_delay_mod_depth(float depth);
void mixer_set_delay_mod_rate(float rate_hz);
void mixer_set_delay_reverb_send(float reverb_send);
void mixer_set_delay_volume(float volume);
void mixer_set_track_filter_type(uint32_t track_id, mixer_track_filter_type_t type);
void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz);
void mixer_set_track_filter_resonance(uint32_t track_id, float resonance);
void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount);
void mixer_set_track_filter_attack(uint32_t track_id, float attack_s);
void mixer_set_track_filter_decay(uint32_t track_id, float decay_s);
void mixer_set_track_filter_sustain(uint32_t track_id, float sustain);
void mixer_set_track_filter_release(uint32_t track_id, float release_s);
void mixer_set_track_filter_keytrack(uint32_t track_id, float amount);
void mixer_set_track_filter_env_reset(uint32_t track_id, uint8_t enabled);
void mixer_set_track_filter_env_delay(uint32_t track_id, float delay_s);
void mixer_set_track_filter_eq_low(uint32_t track_id, float gain_db);
void mixer_set_track_filter_eq_mid(uint32_t track_id, float gain_db);
void mixer_set_track_filter_eq_high(uint32_t track_id, float gain_db);
void mixer_set_track_vca_attack(uint32_t track_id, float attack_s);
void mixer_set_track_vca_decay(uint32_t track_id, float decay_s);
void mixer_set_track_vca_sustain(uint32_t track_id, float sustain);
void mixer_set_track_vca_release(uint32_t track_id, float release_s);
void mixer_set_track_vca_enabled(uint32_t track_id, uint8_t enabled);
void mixer_track_vca_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity);
void mixer_track_vca_note_off(uint32_t track_id, uint8_t midi_note);
void mixer_track_vca_all_notes_off(uint32_t track_id);
uint8_t mixer_track_vca_is_running(uint32_t track_id);
void mixer_track_filter_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity);
void mixer_track_filter_note_off(uint32_t track_id, uint8_t midi_note);
void mixer_track_filter_all_notes_off(uint32_t track_id);
void mixer_rebind_track_states(const uint8_t *previous_mix_tracks,
                               const uint8_t *next_mix_tracks,
                               uint32_t track_count);
void mixer_rebind_track_state(uint8_t previous_mix_track, uint8_t next_mix_track);
void mixer_snap_track_runtime_state(uint32_t track_id);
void mixer_external_inputs_clear(void);
void mixer_submit_external_mono(uint32_t track_id, const float *mono, uint32_t frames);
void mixer_submit_external_mono_native(uint32_t track_id, const float *mono, uint32_t frames);
void mixer_submit_external_stereo(uint32_t track_id, const float *left, const float *right, uint32_t frames);
uint8_t mixer_begin_external_stereo(uint32_t track_id,
                                    uint32_t frames,
                                    float **out_left,
                                    float **out_right);
void mixer_commit_external_stereo(uint32_t track_id, uint32_t frames);

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
