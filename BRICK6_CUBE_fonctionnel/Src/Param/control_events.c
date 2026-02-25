/**
 * @file control_events.c
 * @brief File d'événements de contrôle lock-free simple producteur/consommateur.
 *
 * Rôle du module:
 * - Bufferiser des événements contrôle entre contexte non-IRQ et audio IRQ.
 * - Offrir push/pop bornés sans allocation dynamique.
 *
 * Architecture:
 * - Appelé par: UI/contrôles (push), audio_float.c (pop).
 * - Appelle: aucun module applicatif.
 *
 * Contraintes temps réel:
 * - IRQ: oui (consommation en chemin audio).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - La longueur de queue est une puissance de 2 pour masquage rapide.
 */

#include "control_events.h"
#include "stm32h7xx_hal.h"

#define CONTROL_EVT_Q_LEN 64U

/** Queue circulaire statique d'événements. */
static control_event_t g_evt_q[CONTROL_EVT_Q_LEN];
static volatile uint32_t g_evt_write;
static volatile uint32_t g_evt_read;

/**
 * @brief Initialise la file d'événements.
 *
 * Rôle:
 * - Réinitialiser les index lecture/écriture.
 *
 * Contexte d'appel:
 * - Init application.
 */
void control_event_init(void)
{
    g_evt_write = 0U;
    g_evt_read = 0U;
}

/**
 * @brief Empile un événement dans la queue si de la place est disponible.
 *
 * @param evt Pointeur sur l'événement source.
 *
 * @return true si l'événement a été ajouté, false sinon.
 *
 * Contexte d'appel:
 * - Tasklet/main loop typiquement.
 *
 * Contraintes:
 * - Non bloquant, O(1), sans malloc.
 */
bool control_event_push(const control_event_t *evt)
{
    if(evt == 0)
        return false;

    uint32_t next = (g_evt_write + 1U) & (CONTROL_EVT_Q_LEN - 1U);
    if(next == g_evt_read)
        return false;

    g_evt_q[g_evt_write] = *evt;
    __DMB();
    g_evt_write = next;
    return true;
}

/**
 * @brief Dépile un événement de la queue si disponible.
 *
 * @param evt Destination de copie.
 *
 * @return true si un événement a été lu, false sinon.
 *
 * Contexte d'appel:
 * - IRQ audio (chemin DSP) ou tasklet selon usage.
 */
bool control_event_pop(control_event_t *evt)
{
    if(evt == 0)
        return false;

    if(g_evt_read == g_evt_write)
        return false;

    *evt = g_evt_q[g_evt_read];
    g_evt_read = (g_evt_read + 1U) & (CONTROL_EVT_Q_LEN - 1U);
    return true;
}
