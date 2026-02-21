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

static float track_gain_mirror[MAX_TRACKS] = {1.0f, 1.0f, 1.0f};

/** Voir mixer.h */
void mixer_init(void)
{
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        track_gain_mirror[t] = 1.0f;
        track_set_gain(t, 1.0f);
    }
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
void mixer_set_track_gain(uint32_t track_id, float gain)
{
    if(track_id >= MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    track_set_gain(track_id, gain);
    track_gain_mirror[track_id] = gain;
}

/** Voir mixer.h */
float mixer_get_track_gain(uint32_t track_id)
{
    if(track_id >= MAX_TRACKS)
        return 0.0f;

    return track_gain_mirror[track_id];
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
