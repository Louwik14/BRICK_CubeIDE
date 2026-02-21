/**
 * @file mixer.c
 * @brief Module mixer (routage uniquement, sans traitement d'amplitude).
 *
 * Rôle du module:
 * - Point d'extension pour futur routage matriciel / inserts / sends.
 * - API de compatibilité pour le contrôle du master depuis l'application.
 *
 * Architecture:
 * - Appelé par my_dsp() dans brick6_app_init.c.
 * - Délègue le gain master à audio_float.c (source unique de vérité).
 *
 * Contraintes temps réel:
 * - mixer_process() peut être appelé en IRQ audio.
 * - Implémentation actuelle neutre: aucune opération sample-par-sample.
 */

#include "mixer.h"

/** Voir mixer.h */
void mixer_init(void)
{
}

/** Voir mixer.h */
void mixer_set_master(float gain)
{
    audio_float_set_master_gain(gain);
}

/** Voir mixer.h */
float mixer_get_master(void)
{
    return audio_float_get_master_gain();
}

/** Voir mixer.h */
void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    (void)tracks;
    (void)track_count;
    (void)frames;

    /* Routing-only stage:
       - ne modifie pas les samples,
       - pas de gain ici,
       - réservé à l'intégration de routages futurs. */
}
