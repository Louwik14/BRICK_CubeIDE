/**
 * @file font5x7.h
 * @brief Police ASCII 5x7 classique utilisée pour les rendus UI texte.
 *
 * @ingroup ui
 *
 * Police monospacée à 5 colonnes et 7 lignes de pixels,
 * compatible avec les routines d’affichage `drv_display_*`.
 *
 * - Domaine ASCII : 32 → 126 (95 caractères imprimables)
 * - Chaque caractère est défini par 5 octets :
 *   - 1 octet = 1 colonne verticale (bit 0 = pixel du bas, bit 6 = pixel du haut)
 *   - Le rendu se fait colonne par colonne
 *
 * Exemple d’utilisation :
 * ```c
 * // Affiche les colonnes binaires du caractère 'A'
 * for (int col = 0; col < 5; col++) {
 *     uint8_t pattern = font5x7['A' - 32][col];
 *     ...
 * }
 * ```
 */

#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

/**
 * @brief Table ASCII 5x7 (95 caractères, de 32 ' ' à 126 '~').
 *
 * Chaque entrée contient 5 colonnes de bits verticaux.
 * Utilisée pour le texte standard dans l’interface (titres, labels, etc.).
 */
extern const uint8_t font5x7[95][5];

#endif // FONT5X7_H
