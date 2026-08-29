/**
 * @file brick6_master_control.h
 * @brief Runtime master control API.
 *
 * Rôle du module:
 * - Exposer la capture/publication boot puis le traitement runtime du master.
 *
 * Frontière:
 * - N'initialise pas l'audio.
 * - Ne gère pas d'autres contrôles UI.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t brick6_master_control_boot_capture(void);
void brick6_master_control_boot_publish(void);
void brick6_master_control_process(void);

#ifdef __cplusplus
}
#endif
