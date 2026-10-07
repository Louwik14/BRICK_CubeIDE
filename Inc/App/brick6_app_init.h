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

void brick6_app_init(void);
void brick6_app_process(void);
uint8_t brick6_app_sd_bench_active(void);

#endif /* BRICK6_APP_INIT_H */
