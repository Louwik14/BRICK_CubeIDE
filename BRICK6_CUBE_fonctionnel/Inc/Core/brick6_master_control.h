/**
 * @file brick6_master_control.h
 * @brief Runtime master control API.
 *
 * Rôle du module:
 * - Exposer le traitement runtime du master (potentiomètre).
 *
 * Frontière:
 * - N'initialise pas l'audio.
 * - Ne gère pas d'autres contrôles UI.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void brick6_master_control_process(void);

#ifdef __cplusplus
}
#endif
