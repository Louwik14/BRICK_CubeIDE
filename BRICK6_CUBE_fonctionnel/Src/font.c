/**
 * @file font.c
 * @brief Implémentation des polices bitmap et fonctions d’accès associées.
 *
 * @ingroup ui
 *
 * Ce module regroupe les fonctions d’accès aux différentes polices disponibles :
 * - `FONT_5X7` : police classique lisible, utilisée pour les textes principaux.
 * - `FONT_4X6` : police compacte, utile pour les labels secondaires.
 *
 * Chaque police fournit un callback `get_col()` qui extrait les bits d’une
 * colonne verticale à partir de la table binaire source (`font5x7` ou `font4x6`).
 *
 * ## Notes d’implémentation :
 * - La police 5x7 est stockée directement sous forme de 5 colonnes par caractère.
 * - La police 4x6 est compressée (3 octets par glyphe) et est décompressée à la
 *   volée dans `font4x6_expanded` (lazy-init via `expand_font4x6()`).
 */

#include "font.h"
#include "font5x7.h"
#include "font4x6.h"
#include "font5x8_elektron.h"
#include <stdbool.h>

/* =======================================================================
   Helpers GET_COL pour chaque police
   ======================================================================= */

/**
 * @brief Retourne une colonne de la police 5x7.
 *
 * @param c   Caractère ASCII (32..126)
 * @param col Index de colonne (0..4)
 * @return Octet contenant les 7 bits verticaux du glyphe.
 */
static uint8_t get_col_5x7(char c, uint8_t col) {
    if (c < 32 || c > 126) return 0;
    if (col >= 5) return 0;
    return font5x7[(uint8_t)c - 32][col];
}

/* -----------------------------------------------------------------------
   4x6 : packée sur 3 octets par glyphe (cf. README police)
   - Chaque byte encode 2 lignes de 4 colonnes.
   - Ligne paire   → nibble haut (>>4)
   - Ligne impaire → nibble bas (&0x0F)
   - Dans un nibble : LSB = col0 (pixel gauche)
   ----------------------------------------------------------------------- */

/** @internal Table décompressée : 95 glyphes (32..126), 4 colonnes chacune. */
static uint8_t font4x6_expanded[95][4];
static bool font4x6_ready = false;
/* Colonne Elektron: on expose 5 colonnes "dessin" (0..4) ; la 6e est l'espace (gérée via spacing) */
static uint8_t get_col_5x8_elektron(char c, uint8_t col) {
    if ((uint8_t)c < FONT5X8_FIRST_CHAR || (uint8_t)c >= FONT5X8_LAST_CHAR) return 0;
    if (col >= 5) return 0; /* width logique = 5 */
    return font5x8_elektron[(uint8_t)c - FONT5X8_FIRST_CHAR][col];
}

/**
 * @brief Décompresse la table `font4x6` en RAM pour accès direct colonne/ligne.
 *
 * Appelée automatiquement au premier accès (`lazy init`).
 */
static void expand_font4x6(void) {
    for (int c = 32; c <= 126; c++) {
        const uint8_t *g = font4x6[c];  // 3 octets = 6 lignes
        for (uint8_t row = 0; row < 6; row++) {
            uint8_t byte   = g[row >> 1]; // 0..2
            uint8_t nibble = (row & 1)
                ? (byte & 0x0F)   // ligne impaire
                : (byte >> 4);    // ligne paire

            for (uint8_t col = 0; col < 4; col++) {
                uint8_t bit = (nibble >> col) & 1U;
                if (bit)
                    font4x6_expanded[c - 32][col] |= (1U << row);
            }
        }
    }
    font4x6_ready = true;
}

/**
 * @brief Retourne une colonne de la police 4x6 (décompressée à la volée).
 *
 * @param c   Caractère ASCII (32..126)
 * @param col Index de colonne (0..3)
 * @return Octet contenant les 6 bits verticaux du glyphe.
 */
static uint8_t get_col_4x6(char c, uint8_t col) {
    if (c < 32 || c > 126) return 0;
    if (col >= 4) return 0;
    if (!font4x6_ready) expand_font4x6(); // lazy init
    return font4x6_expanded[(uint8_t)c - 32][col];
}

/* =======================================================================
   Descripteurs globaux
   ======================================================================= */

/** @brief Police standard 5x7 monospace. */
const font_t FONT_5X7 = {
    .name     = "5x7",
    .width    = 5,
    .height   = 7,
    .first    = 32,
    .last     = 126,
    .get_col  = get_col_5x7,
    .spacing  = 1
};

/** @brief Police compacte 4x6, plus serrée pour économiser de l’espace. */
const font_t FONT_4X6 = {
    .name     = "4x6",
    .width    = 4,
    .height   = 6,
    .first    = 32,
    .last     = 126,
    .get_col  = get_col_4x6,
    .spacing  = 0
};

/** @brief Police Elektron 5x8 (5 px + spacing 1 px) */
const font_t FONT_5X8_ELEKTRON = {
    .name    = "5x8_elektron",
    .width   = 5,
    .height  = 8,
    .first   = FONT5X8_FIRST_CHAR,
    .last    = FONT5X8_LAST_CHAR,
    .spacing = 1,
    .get_col = get_col_5x8_elektron
};
