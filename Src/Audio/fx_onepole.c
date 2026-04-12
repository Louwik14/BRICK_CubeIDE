/**
 * @file fx_onepole.c
 * @brief Module applicatif fx_onepole.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à fx_onepole.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "fx_onepole.h"
#include <math.h>

#define FX_ONEPOLE_PI_F 3.14159265358979323846f

/**
 * @brief Point d'entrée fx_onepole_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_onepole_init.
 *
 * @param f Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_onepole_init(fx_onepole_t *f)
{
    if(!f)
        return;

    f->g = 0.0f;
    f->gi = 1.0f;
    f->state = 0.0f;
    f->mode = 0U;
}

/**
 * @brief Point d'entrée fx_onepole_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_onepole_reset.
 *
 * @param f Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_onepole_reset(fx_onepole_t *f)
{
    if(!f)
        return;

    f->state = 0.0f;
}

/**
 * @brief Point d'entrée fx_onepole_set_freq.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_onepole_set_freq.
 *
 * @param f Paramètre d'entrée de l'API.
 * @param freq_norm Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_onepole_set_freq(fx_onepole_t *f, float freq_norm)
{
    if(!f)
        return;

    if(freq_norm < 0.0f)
        freq_norm = 0.0f;
    if(freq_norm > 0.497f)
        freq_norm = 0.497f;

    const float g = tanf(FX_ONEPOLE_PI_F * freq_norm);
    f->g = g;
    f->gi = 1.0f / (1.0f + g);
}

/**
 * @brief Point d'entrée fx_onepole_set_mode.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_onepole_set_mode.
 *
 * @param f Paramètre d'entrée de l'API.
 * @param mode Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_onepole_set_mode(fx_onepole_t *f, uint8_t mode)
{
    if(!f)
        return;

    f->mode = (mode == 0U) ? 0U : 1U;
}
