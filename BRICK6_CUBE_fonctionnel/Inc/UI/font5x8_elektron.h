/**
 * @file font5x8_elektron.h
 * @brief Police bitmap 5x8 inspirée du style Elektron.
 *
 * @ingroup ui
 *
 * Police monospacée compacte : 5 pixels de large + 1 pixel d’espacement = 6 px par glyph.
 * Utilisée par le moteur de rendu OLED (`ui_renderer.c`).
 *
 * Chaque caractère est stocké sous forme de colonnes verticales de 8 bits :
 * - 1 octet = 1 colonne (bit 0 = pixel du bas, bit 7 = pixel du haut)
 * - Les colonnes sont indexées de gauche à droite.
 *
 * Exemple d’accès :
 * ```c
 * uint8_t col = font5x8_elektron['A' - FONT5X8_FIRST_CHAR][0];
 * ```
 */

#ifndef BRICK_UI_FONT5X8_ELEKTRON_H
#define BRICK_UI_FONT5X8_ELEKTRON_H

#include <stdint.h>

/** Largeur totale d’un caractère (5px + 1px d’espacement) */
#define FONT5X8_ELEKTRON_WIDTH   6

/** Hauteur en pixels d’un caractère */
#define FONT5X8_ELEKTRON_HEIGHT  8

/** Premier code ASCII inclus (espace ' ') */
#define FONT5X8_FIRST_CHAR       32

/** Dernier code ASCII inclus ('~') */
#define FONT5X8_LAST_CHAR        127

/**
 * @brief Table de police 5x8 Elektron, codée en colonnes (1 octet = 1 colonne verticale).
 *
 * Taille : 96 caractères (ASCII 32–127), 6 octets par caractère.
 *
 * - `font5x8_elektron[ch - FONT5X8_FIRST_CHAR][col]`
 *   → renvoie la colonne `col` du caractère `ch`.
 *
 * @note Le bit 0 de chaque colonne correspond au pixel **bas** de la lettre.
 */
extern const uint8_t font5x8_elektron[96][6];

#endif /* BRICK_UI_FONT5X8_ELEKTRON_H */

