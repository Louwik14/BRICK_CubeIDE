/**
 * @file font4x6.h
 * @brief Police 4x6 compacte utilisée pour les textes secondaires de l’interface.
 *
 * @ingroup ui
 *
 * Police bitmap minimaliste de 4x6 pixels par caractère, stockée sous forme
 * de 3 octets par glyphe. Chaque bit correspond à un pixel (1 = allumé).
 * Elle couvre le jeu de caractères ASCII 32 à 127.
 *
 * Utilisée par `ui_renderer.c` pour les labels de pages, menus ou petites annotations.
 *
 * Exemple :
 * ```c
 * const uint8_t *glyph = font4x6['A'];
 * for (int byte = 0; byte < 3; byte++) {
 *     uint8_t bits = glyph[byte];
 *     ...
 * }
 * ```
 */

#ifndef BRICK_UI_FONT4X6_H
#define BRICK_UI_FONT4X6_H

#include <stdint.h>

/** @brief Table des glyphes 4x6, 3 octets par caractère. */
extern const uint8_t font4x6[][3];

/** @brief Largeur en pixels de chaque caractère (4). */
extern const uint8_t font4x6_width;

/** @brief Hauteur en pixels de chaque caractère (6). */
extern const uint8_t font4x6_height;

#endif /* BRICK_UI_FONT4X6_H */
