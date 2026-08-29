#pragma once
#include <stdint.h>
#include "Board/board_audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define AUDIO_RESTRICT __restrict
#else
#define AUDIO_RESTRICT
#endif

/**
 * @file audio_float.h
 * @brief Frontière DSP float track-based (stéréo) pour le contrat audio commun.
 *
 * Rôle du module:
 * - Convertir le flux int24 (DMA/SAI) <-> float.
 * - Exposer un modèle de traitement par tracks stéréo actives.
 * - Effectuer le mixage master et le mapping de sortie stéréo.
 *
 * Architecture:
 * - Appelé par: audio.c (IRQ DMA RX half/full).
 * - Appelle: callback DSP utilisateur (audio_dsp_cb).
 *
 * Contraintes temps réel:
 * - audio_process_block_int32() est exécutée en IRQ audio.
 * - Aucun blocage, aucune allocation dynamique.
 */

/* ============================================================
   Track-based stereo DSP model
   ============================================================ */

#define AUDIO_BLOCK_SIZE BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF
/* Le transport physique commun expose une seule paire stéréo (slots 0/1).
 * Les tracks DSP supplémentaires restent des lanes logiques internes. */
#define MAX_TRACKS       4U

/**
 * @brief Structure de track stéréo (buffers bloc + état actif).
 *
 * - L/R: buffers en float, taille AUDIO_BLOCK_SIZE samples.
 * - enabled: 1 = track active, 0 = track inactive.
 */
typedef struct
{
    float   L[AUDIO_BLOCK_SIZE];
    float   R[AUDIO_BLOCK_SIZE];
    uint8_t enabled;
} StereoTrack;

/**
 * @brief Signature du callback DSP utilisateur.
 *
 * Contrat:
 * - tracks[t].enabled == 0 => track inactive, à ignorer.
 * - Les buffers des tracks inactives sont remis à zéro par le moteur à chaque bloc.
 *
 * @param tracks Tableau de tracks stéréo.
 * @param track_count Nombre de tracks valides (MAX_TRACKS).
 * @param frames Taille de bloc en frames (samples par canal).
 */
typedef void (*audio_dsp_cb)(StereoTrack *AUDIO_RESTRICT tracks,
                             uint32_t track_count,
                             uint32_t frames);

typedef struct
{
    uint32_t audio_block_counter;
    uint32_t dsp_frames_counter;
} audio_debug_stats_t;


/**
 * @brief Enregistre le callback DSP principal.
 *
 * @param cb Callback DSP track-based (peut être NULL).
 *
 * Contexte d'appel:
 * - Main loop (init/config) recommandé.
 *
 * Effets de bord:
 * - Met à jour le pointeur de callback global utilisé en IRQ.
 */
void audio_set_float_callback(audio_dsp_cb cb);

/**
 * @brief Initialise l'état des tracks audio.
 *
 * - enabled = 0 pour toutes les tracks.
 * - gain track = 1.0.
 * - buffers L/R remis à zéro.
 *
 * Contexte d'appel:
 * - Main loop (avant démarrage DMA audio).
 */
void audio_tracks_init(void);

/** Initialise synchronously le gain staging AUDIO avant le démarrage du flux. */
void audio_float_init_gain_staging(float postgain,
                                   float output_compensation);

/**
 * @brief Active/désactive une track.
 *
 * @param track_id Index track [0..MAX_TRACKS-1].
 * @param enabled 0 = désactivée, non-zéro = activée.
 *
 * Effets de bord:
 * - Met à jour tracks[track_id].enabled.
 */
void track_enable(uint32_t track_id, uint8_t enabled);

/**
 * @brief Retourne l'état d'activation d'une track.
 *
 * @param track_id Index track [0..MAX_TRACKS-1].
 * @return 1 si active, 0 sinon (ou index invalide).
 */
uint32_t track_is_enabled(uint32_t track_id);
uint32_t audio_tracks_enabled_mask(void);

/**
 * @brief Configure le gain d'une track.
 *
 * @param track_id Index track [0..MAX_TRACKS-1].
 * @param gain Gain linéaire (clamp interne à [0..+inf[, typiquement 0..2).
 */
void track_set_gain(uint32_t track_id, float gain);

/**
 * @brief Configure le gain master appliqué après somme des tracks.
 *
 * @param gain Gain linéaire (clamp interne 0.0 .. 1.0, unity max).
 */
void audio_float_set_master_gain(float gain);

/**
 * @brief Lit le gain master courant.
 *
 * @return Gain master linéaire.
 */
float audio_float_get_master_gain(void);

/* ============================================================
   Gain staging (Daisy-style)
   ============================================================ */

/**
 * @brief Définit le postgain d'entrée (ADC -> DSP).
 *
 * @param gain Gain linéaire (>0 attendu, protection interne si <=0).
 *
 * Effet:
 * - met à jour postgain_recip (=1/gain) et output_adjust.
 */
void audio_float_set_postgain(float gain);

/**
 * @brief Définit la compensation de sortie float -> DAC.
 *
 * @param comp Facteur de compensation linéaire.
 *
 * Effet:
 * - met à jour output_adjust = postgain * output_comp.
 */
void audio_float_set_output_compensation(float comp);

void audio_float_set_dj_eq_low_db(float db);
void audio_float_set_dj_eq_mid_db(float db);
void audio_float_set_dj_eq_high_db(float db);
void audio_float_set_dj_eq_ui_params(uint8_t low, uint8_t mid, uint8_t high);
uint8_t audio_float_is_dj_eq_ui_neutral(void);
void audio_float_set_saturation_tone(float tone);
void audio_float_set_saturation_bias(float bias);
void audio_float_set_saturation_drive(float drive);
void audio_float_set_saturation_mix(float mix);

/* ============================================================
   Engine entry point called by audio.c
   ============================================================ */

/**
 * @brief Traite un bloc audio int32 stereo en modèle track stéréo float.
 *
 * @param rx Buffer d'entrée DMA (int24 right-aligned dans int32).
 * @param tx Buffer de sortie DMA (int24 right-aligned dans int32).
 * @param frames Nombre de frames à traiter (<= AUDIO_BLOCK_SIZE).
 *
 * Contexte d'appel:
 * - IRQ audio (DMA RX half/full callback).
 *
 * Pipeline:
 * 1) Unpack stereo -> tracks actives.
 * 2) Callback DSP utilisateur.
 * 3) Somme tracks actives + gains.
 * 4) Pack master vers les deux slots de sortie.
 */
void audio_process_block_int32(int32_t *AUDIO_RESTRICT rx,
                               int32_t *AUDIO_RESTRICT tx,
                               uint32_t frames);

extern volatile uint32_t g_audio_block_counter;
extern volatile uint32_t g_audio_dsp_frames_counter;

void audio_debug_get_stats(audio_debug_stats_t *out_stats);

uint32_t audio_get_frame_counter(void);

#ifdef __cplusplus
}
#endif
void audio_float_set_bus_comp_threshold_db(float threshold_db);
void audio_float_set_bus_comp_ratio(float ratio);
void audio_float_set_bus_comp_attack_index(uint8_t attack_index);
void audio_float_set_bus_comp_release_index(uint8_t release_index);
void audio_float_set_bus_comp_makeup_db(float makeup_db);
void audio_float_set_bus_comp_auto_makeup(uint8_t enabled);
