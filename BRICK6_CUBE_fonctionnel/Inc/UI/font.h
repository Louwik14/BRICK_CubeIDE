/**
 * @file font.h
 * @brief Interface générique pour la gestion des polices bitmap dans l’UI.
 *
 * @ingroup ui
 *
 * Ce module définit la structure abstraite `font_t`, utilisée pour représenter
 * n’importe quelle police à largeur fixe (5x7, 4x6, etc.).
 *
 * Chaque police fournit une fonction `get_col(c, col)` qui renvoie un octet
 * représentant une colonne de pixels d’un caractère donné.
 * Le moteur de rendu se charge ensuite de dessiner ces bits sur l’écran.
 *
 * ## Convention binaire
 * - `bit0` = pixel du haut
 * - `bit(height-1)` = pixel du bas
 *
 * ## Exemples
 * ```c
 * uint8_t col = FONT_5X7.get_col('A', 0);
 * // -> colonne 0 du caractère 'A' (bit 0 = pixel du haut)
 * ```
 */

#ifndef BRICK_UI_FONT_H
#define BRICK_UI_FONT_H

#include <stdint.h>

/**
 * @brief Structure décrivant une police bitmap à largeur fixe.
 *
 * Chaque police est décrite par sa géométrie, son encodage ASCII, et
 * une fonction de lecture de colonne (`get_col`).
 */
typedef struct font_s {
    const char *name;   /**< Nom lisible de la police (ex: "5x7", "4x6"). */
    uint8_t width;      /**< Largeur du glyphe (en colonnes). */
    uint8_t height;     /**< Hauteur utile (en pixels verticaux). */
    uint8_t first;      /**< Premier caractère ASCII couvert (souvent 32 = espace). */
    uint8_t last;       /**< Dernier caractère ASCII couvert (souvent 126 = '~'). */
    uint8_t spacing;    /**< Colonnes vides ajoutées après chaque glyphe (ex: 1). */
    /**
     * @brief Fonction de lecture d’une colonne de glyphe.
     * @param c   Caractère ASCII.
     * @param col Index de colonne (0 → width-1).
     * @return Octet contenant les bits de la colonne (bit0 = pixel haut).
     */
    uint8_t (*get_col)(char c, uint8_t col);
} font_t;

/**
 * @brief Police standard 5x7 monospace (lecture directe).
 *
 * Basée sur `font5x7[95][5]`, couvre les caractères ASCII 32 à 126.
 */
extern const font_t FONT_5X7;

/**
 * @brief Police compacte 4x6 (format packé, décompressée à la volée).
 *
 * Basée sur `font4x6[][3]`, couvre les caractères ASCII 32 à 126.
 */
extern const font_t FONT_4X6;
extern const font_t FONT_5X8_ELEKTRON;
#endif /* BRICK_UI_FONT_H */
