#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file audio_float.h
 * @brief Frontière DSP float track-based (stéréo) pour moteur audio TDM8.
 *
 * Rôle du module:
 * - Convertir le flux int24 (DMA/SAI) <-> float.
 * - Exposer un modèle de traitement par tracks stéréo actives.
 * - Effectuer le mixage master et le mapping de sortie TDM.
 *
 * Architecture:
 * - Appelé par: audio.c (IRQ DMA RX half/full).
 * - Appelle: callback DSP utilisateur (audio_dsp_cb).
 *
 * Contraintes temps réel:
 * - audio_process_block_int32() est exécutée en IRQ audio (I/O uniquement).
 * - audio_dsp_main_process() s'exécute en main loop (DSP).
 * - Aucun blocage, aucune allocation dynamique.
 */

/* ============================================================
   Track-based stereo DSP model
   ============================================================ */

#define AUDIO_BLOCK_SIZE 32U
#define MAX_TRACKS       3U

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
typedef void (*audio_dsp_cb)(StereoTrack *tracks,
                             uint32_t track_count,
                             uint32_t frames);

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
 * @param gain Gain linéaire (clamp interne 0.0 .. 2.0).
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

/* ============================================================
   Engine entry point called by audio.c
   ============================================================ */

/**
 * @brief Traite un bloc audio int32 (TDM8) en modèle track stéréo float.
 *
 * @param rx Buffer d'entrée DMA (int24 right-aligned dans int32).
 * @param tx Buffer de sortie DMA (int24 right-aligned dans int32).
 * @param frames Nombre de frames à traiter (<= AUDIO_BLOCK_SIZE).
 *
 * Contexte d'appel:
 * - IRQ audio (DMA RX half/full callback).
 *
 * Pipeline IRQ:
 * 1) Unpack TDM -> tracks actives.
 * 2) Publication vers buffer DSP partagé.
 * 3) Pack master/cue à partir de la dernière sortie DSP prête.
 */
void audio_process_block_int32(int32_t *rx,
                               int32_t *tx,
                               uint32_t frames);

/**
 * @brief Exécute le traitement DSP hors IRQ sur le buffer partagé.
 *
 * @param frames Nombre de frames à traiter (<= AUDIO_BLOCK_SIZE).
 *
 * Contexte d'appel:
 * - Main loop uniquement.
 */
void audio_dsp_main_process(uint32_t frames);

/**
 * @brief Lit le nombre de blocs d'entrée DSP perdus (main loop en retard).
 */
uint32_t audio_float_get_in_overrun_count(void);

/**
 * @brief Lit le nombre de blocs sortis en silence faute de résultat DSP prêt.
 */
uint32_t audio_float_get_out_underflow_count(void);

/**
 * @brief Remet à zéro les compteurs overrun/underflow IRQ<->main.
 */
void audio_float_reset_xrun_counters(void);

#ifdef __cplusplus
}
#endif
