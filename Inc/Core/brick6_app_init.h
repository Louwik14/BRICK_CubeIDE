#ifndef BRICK6_APP_INIT_H
#define BRICK6_APP_INIT_H

/**
 * @file brick6_app_init.h
 * @brief Point d'entrée d'initialisation applicative BRICK6.
 *
 * Rôle du module:
 * - Déclarer l'API d'init applicative hors CubeMX.
 */

void brick6_platform_shared_init(void);
void brick6_audio_domain_init(void);
void brick6_system_domain_init(void);
void brick6_audio_domain_start(void);
void brick6_system_domain_start(void);
void brick6_system_domain_process(void);

void brick6_app_init(void);
void brick6_app_process(void);

#endif /* BRICK6_APP_INIT_H */
