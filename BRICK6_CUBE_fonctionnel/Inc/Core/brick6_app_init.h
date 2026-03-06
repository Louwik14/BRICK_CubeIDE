#ifndef BRICK6_APP_INIT_H
#define BRICK6_APP_INIT_H

#include <stdint.h>

/**
 * @file brick6_app_init.h
 * @brief Point d'entrée d'initialisation applicative BRICK6.
 *
 * Rôle du module:
 * - Déclarer l'API d'init applicative hors CubeMX.
 */

typedef struct
{
    uint32_t app_process_call_count;
} brick6_app_stats_t;

void brick6_app_init(void);
void brick6_app_process(void);
void brick6_app_get_stats(brick6_app_stats_t *out_stats);

#endif /* BRICK6_APP_INIT_H */
