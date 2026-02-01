/**
 * @file routing.c
 * @brief Sélection compile-time de la source audio principale.
 *
 * Définit la source active (SAI/USB/SD/MIX) via une constante de build.
 *
 * Rôle dans le système:
 * - Routing statique du moteur audio.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - IRQ: non.
 * - Tasklet: non.
 * - Borné: oui.
 *
 * Architecture:
 * - Appelé par: audio_core.
 * - Appelle: aucun module externe.
 * - Consommé par: moteur audio.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans routing.h.
 */

#include "routing.h"

#define ROUTE_DEFAULT ROUTE_SRC_USB

route_source_t routing_get_source(void)
{
  return ROUTE_DEFAULT;
}
