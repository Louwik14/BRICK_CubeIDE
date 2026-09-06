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
void brick6_app_control_process_causes(uint32_t wake_flags);
void brick6_app_storage_dispatch_once(void);
void brick6_app_usb_process(void);
void brick6_app_ui_process_input(void);
void brick6_app_ui_process_presentation(uint8_t deadline_due);

#endif /* BRICK6_APP_INIT_H */
