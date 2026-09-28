/**
 * @file audio_float.c
 * @brief Moteur frontière int24 <-> float, architecture tracks stéréo actives.
 *
 * Rôle du module:
 * - Convertir le flux DMA stereo 2 slots (int24 right-aligned) en buffers float.
 * - Exécuter le callback DSP utilisateur sur les tracks.
 * - Réaliser le mix vers buses internes et le remappage de sortie stereo.
 *
 * Architecture (appelant -> appelé):
 * - audio.c (IRQ DMA RX) -> audio_process_block_int32().
 * - audio_process_block_int32() -> float_cb(tracks, MAX_TRACKS, frames).
 *
 * Modèle audio track-based:
 * - La paire d'entree logique lit les slots 0/1 (L/R).
 * - Les tracks DSP supplementaires sont des lanes logiques internes.
 *
 * Mapping sortie physique:
 * - MAIN L/R -> slots 0/1.
 * - Les anciennes voies physiques auxiliaires sont ignorees par l'adapter board.
 *
 * Contraintes temps réel:
 * - Fonction principale exécutée en IRQ audio.
 * - Aucune allocation dynamique, buffers statiques uniquement.
 * - Zéro logs / printf / appels bloquants.
 */

#include "audio_float.h"
#include <stdint.h>
#include <string.h>
#include <arm_acle.h>
#include "arm_math.h"
#include "fx_saturation.h"
#include "Platform/memory_layout.h"
#include "audio_io.h"
#include "dsp_engine.h"
#include "fx_pool.h"
#include "fx_comp_lab.h"

/* ============================================================
   GAIN STAGING (style Daisy)

   postgain_recip : facteur appliqué à l'entrée ADC (1/postgain).
   output_adjust  : correction de sortie commune (postgain * output_comp).
   master_gain    : gain master appliqué après somme des tracks.
   ============================================================ */

static AUDIO_HOT float postgain_recip;
static AUDIO_HOT float output_adjust;
static AUDIO_HOT float master_gain_smoothed;
static AUDIO_HOT float master_gain_target;
static AUDIO_HOT float master_gain;

static float postgain = 1.0f;
static float output_comp = 1.0f;


/**
 * @brief Convertit l'index d'attaque du compresseur de bus.
 *
 * Rôle:
 * - Retourner le temps d'attaque borné associé à l'index.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float bus_comp_attack_index_to_seconds(uint8_t attack_index)
{
    static const float attack_s[6] = {0.0001f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f};
    if(attack_index > 5U) attack_index = 5U;
    return attack_s[attack_index];
}
static inline fx_saturation_t *fx_pool_sat_state(void)
{
    fx_slot_t *s = fx_pool_get_slot(1U);
    return (s != 0) ? (fx_saturation_t *)s->state : 0;
}

static inline fx_comp_lab_t *fx_pool_comp_lab_state(void)
{
    fx_slot_t *s = fx_pool_get_slot(2U);
    return (s != 0) ? (fx_comp_lab_t *)s->state : 0;
}

/**
 * @brief Point d'entrée bus_comp_attack_index_to_seconds.
 *
 * Rôle:
 * - Exécuter le traitement associé à bus_comp_attack_index_to_seconds.
 *
 * @param attack_index Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float bus_comp_release_index_to_seconds(uint8_t release_index)
{
    static const float release_s[5] = {0.1f, 0.3f, 0.6f, 1.2f, 1.2f};
    if(release_index > 4U) release_index = 4U;
    return release_s[release_index];
}

/**
 * @brief Point d'entrée bus_comp_release_index_to_seconds.
 *
 * Rôle:
 * - Exécuter le traitement associé à bus_comp_release_index_to_seconds.
 *
 * @param release_index Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_threshold_db(float threshold_db)
{
    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_set_threshold_db(comp, threshold_db);
}

/** Voir audio_float.h */
void audio_float_set_postgain(float gain)
{
    if(gain <= 0.0f)
        gain = 1.0f;

    postgain = gain;
    postgain_recip = 1.0f / postgain;

    output_adjust = postgain * output_comp;
}

/** Voir audio_float.h */
void audio_float_set_output_compensation(float comp)
{
    output_comp = comp;
    output_adjust = postgain * output_comp;
}

void audio_float_init_gain_staging(float boot_postgain,
                                   float boot_output_compensation)
{
    if (boot_postgain <= 0.0f)
        boot_postgain = 1.0f;

    postgain = boot_postgain;
    postgain_recip = 1.0f / boot_postgain;
    output_comp = boot_output_compensation;
    output_adjust = boot_postgain * boot_output_compensation;

    /* Master remains silent until the physical pot value is published. */
    master_gain = 0.0f;
    master_gain_target = 0.0f;
    master_gain_smoothed = 0.0f;
}

/**
 * @brief Règle le ratio du compresseur de bus.
 *
 * Rôle:
 * - Transmettre le ratio à l'instance DSP canonique.
 *
 * @param ratio Ratio demandé.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_ratio(float ratio)
{
    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_set_ratio(comp, ratio);
}

/**
 * @brief Règle l'attaque du compresseur de bus.
 *
 * Rôle:
 * - Convertir puis transmettre l'index d'attaque.
 *
 * @param attack_index Index d'attaque.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_attack_index(uint8_t attack_index)
{
    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_set_attack_s(comp, bus_comp_attack_index_to_seconds(attack_index));
}

/**
 * @brief Règle le relâchement du compresseur de bus.
 *
 * Rôle:
 * - Convertir puis transmettre l'index de relâchement.
 *
 * @param release_index Index de relâchement.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_release_index(uint8_t release_index)
{
    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_set_release_s(comp, bus_comp_release_index_to_seconds(release_index));
}

/**
 * @brief Règle le gain de compensation du compresseur de bus.
 *
 * Rôle:
 * - Transmettre le gain à l'instance DSP canonique.
 *
 * @param makeup_db Gain en dB.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_makeup_db(float makeup_db)
{
    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_set_makeup_db(comp, makeup_db);
}

/**
 * @brief Configure l'option auto-makeup du compresseur de bus.
 *
 * Rôle:
 * - Conserver le contrat API; l'implémentation actuelle est neutre.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_bus_comp_auto_makeup(uint8_t enabled)
{
    (void)enabled;
}

/**
 * @brief Point d'entrée audio_float_set_saturation_tone.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_saturation_tone.
 *
 * @param tone_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_saturation_tone(float tone)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_tone(sat, tone);
}

/**
 * @brief Point d'entrée audio_float_set_saturation_bias.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_saturation_bias.
 *
 * @param bias_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_saturation_bias(float bias)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_bias(sat, bias);
}

/**
 * @brief Point d'entrée audio_float_set_saturation_drive.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_saturation_drive.
 *
 * @param drive_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_saturation_drive(float drive)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_drive(sat, drive);
}

/**
 * @brief Point d'entrée audio_float_set_saturation_mix.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_saturation_mix.
 *
 * @param mix_0_127 Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void audio_float_set_saturation_mix(float mix)
{
    fx_saturation_t *sat = fx_pool_sat_state();
    if(sat) fx_saturation_set_mix(sat, mix);
}


/**
 * @brief Point d'entrée audio_float_set_bus_comp_threshold_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_threshold_db.
 *
 * @param threshold_db Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée audio_float_set_bus_comp_ratio.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_ratio.
 *
 * @param ratio Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée audio_float_set_bus_comp_attack_index.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_attack_index.
 *
 * @param attack_index Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée audio_float_set_bus_comp_release_index.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_release_index.
 *
 * @param release_index Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée audio_float_set_bus_comp_makeup_db.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_makeup_db.
 *
 * @param makeup_db Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée audio_float_set_bus_comp_auto_makeup.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_float_set_bus_comp_auto_makeup.
 *
 * @param enabled Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/* ============================================================
   TRACK + MIX STATE
   ============================================================ */

/* État persistant des tracks (buffers bloc + enabled). */
AUDIO_HOT ALIGN32 static StereoTrack tracks[MAX_TRACKS];

volatile uint32_t g_audio_block_counter = 0U;
volatile uint32_t g_audio_dsp_frames_counter = 0U;

/* Gain master global (après somme des tracks). */
static AUDIO_HOT uint32_t g_audio_tracks_enabled_mask;

/* ============================================================
   USER CALLBACK
   ============================================================ */

/** Voir audio_float.h */
void audio_set_float_callback(audio_dsp_cb cb)
{
    dsp_engine_set_callback(cb);
}

/** Voir audio_float.h */
void audio_tracks_init(void)
{
    g_audio_tracks_enabled_mask = 0U;
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        tracks[t].enabled = 0U;
        memset(tracks[t].L, 0, sizeof(tracks[t].L));
        memset(tracks[t].R, 0, sizeof(tracks[t].R));
    }

    fx_saturation_t *sat = fx_pool_sat_state();

    if(sat) fx_saturation_init(sat);

    fx_comp_lab_t *comp = fx_pool_comp_lab_state();
    if(comp) fx_comp_lab_init(comp, 48000.0f);
}

/** Voir audio_float.h */
void track_enable(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MAX_TRACKS)
        return;

    const uint8_t prev = tracks[track_id].enabled;
    const uint8_t next = enabled ? 1U : 0U;

    tracks[track_id].enabled = next;
    const uint32_t bit = (uint32_t)(1UL << track_id);
    if (next != 0U)
        g_audio_tracks_enabled_mask |= bit;
    else
        g_audio_tracks_enabled_mask &= ~bit;

    (void)prev;
}

/** Voir audio_float.h */
uint32_t track_is_enabled(uint32_t track_id)
{
    if(track_id >= MAX_TRACKS)
        return 0U;

    return (uint32_t)tracks[track_id].enabled;
}

uint32_t audio_tracks_enabled_mask(void)
{
    return g_audio_tracks_enabled_mask;
}

/** Voir audio_float.h */
void track_set_gain(uint32_t track_id, float gain)
{
    (void)track_id;
    (void)gain;
}

/** Voir audio_float.h */
void audio_float_set_master_gain(float gain)
{
    if(gain < 0.0f)
        gain = 0.0f;
    if(gain > 1.0f)
        gain = 1.0f;

    master_gain = gain;
    master_gain_target = gain;
}

/** Voir audio_float.h */
float audio_float_get_master_gain(void)
{
    return master_gain;
}

/* ============================================================
   INTERNAL AUDIO PIPELINE HELPERS

   audio_dsp_process():
   - Appelle le callback DSP utilisateur
   - Réalise la somme tracks -> bus_main
   - Le mix final est émis sur la paire MAIN stéréo
   ============================================================ */

/**
 * @brief Point d'entrée audio_dsp_process.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_dsp_process.
 *
 * @param track_buf Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static inline void audio_dsp_process(StereoTrack *AUDIO_RESTRICT track_buf,
                                     uint32_t frames)
{
    (void)frames;
    dsp_engine_process_block(track_buf, MAX_TRACKS, frames);
}

/* ============================================================
   MAIN DSP BLOCK PROCESSOR

   Pipeline temps réel (IRQ):
   1) audio_io_unpack()
   2) audio_dsp_process()
   3) audio_io_pack()
   ============================================================ */

/** Voir audio_float.h */
void audio_process_block_int32(int32_t *AUDIO_RESTRICT rx,
                               int32_t *AUDIO_RESTRICT tx,
                               uint32_t frames)
{
    g_audio_block_counter++;
    g_audio_dsp_frames_counter += frames;

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    const float out_gain_start = output_adjust * master_gain_smoothed;
    master_gain_smoothed += (master_gain_target - master_gain_smoothed) * 0.25f;
    const float out_gain_end = output_adjust * master_gain_smoothed;

    audio_io_unpack(rx, frames, postgain_recip * (1.0f / 8388608.0f));
    audio_dsp_process(tracks, frames);
    audio_io_pack_ramped(tx,
                         tracks[0].L,
                         tracks[0].R,
                         frames,
                         out_gain_start,
                         out_gain_end);
}

/**
 * @brief Point d'entrée audio_get_frame_counter.
 *
 * Rôle:
 * - Exécuter le traitement associé à audio_get_frame_counter.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t audio_get_frame_counter(void)
{
    return g_audio_dsp_frames_counter;
}
