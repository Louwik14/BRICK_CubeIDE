#include "ui_widgets.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ===== Implémentations ===== */

void uiw_draw_knob(int x, int y, int w, int h, int v, int vmin, int vmax) {
    int cx = x + w/2;
    int cy = y + h/2;
    int r  = (w < h ? w : h) / 2 - 2;
    if (r < 6) r = 6;
    ui_draw_knob(cx, cy, r, v, vmin, vmax);
}

/* Exemple: carré plein 12x12 (ancien test) */
static const uint8_t square12_bits[12 * 2] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F,
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F,
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F
};

void uiw_draw_square12_bitmap(int x, int y, int w, int h, const uint8_t *bits) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    int size = 12;
    int x0 = cx - size / 2;
    int y0 = cy - size / 2;
    ui_blit_mono(x0, y0, size, size, bits, 2);
}

/* Redéfinition du switch → carré 12x12 basé sur la table */
void uiw_draw_switch(int x, int y, int w, int h, bool on) {
    (void)on;
    uiw_draw_square12_bitmap(x, y, w, h, square12_bits);
}

/* ===== Fonction de dessin générique (paramétrable) ===== */
static void uiw_draw_bitmap_bin(int x, int y, int w, int h,
                                const uint32_t *rows, int width, int height) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    int x0 = cx - width / 2;
    int y0 = cy - height / 2;

    for (int yy = 0; yy < height; ++yy) {
        uint32_t line = rows[yy];
        for (int xx = 0; xx < width; ++xx) {
            if (line & (1u << (width - 1 - xx))) {
                ui_px(x0 + xx, y0 + yy, true);
            }
        }
    }
}

/* ===== Macros de gestion des icônes (20x14) ===== */
#define UIW_ICON_DEFINE(name, ...) \
    static const uint32_t icon_##name[14] = { __VA_ARGS__ }

/* ===== Icônes binaires (20x14) ===== */

/* Onde sinus */
UIW_ICON_DEFINE(sine,
    0b00011110000000000000,
    0b00110011000000000000,
    0b01100001100000000000,
    0b01000000100000000000,
    0b11000000110000000000,
    0b10000000010000000000,
    0b10000000010000000000,
    0b00000000001000000001,
    0b00000000001000000001,
    0b00000000001100000011,
    0b00000000000100000010,
    0b00000000000110000110,
    0b00000000000011001100,
    0b00000000000001111000
);

/* Carré */
UIW_ICON_DEFINE(square,
    0b11111111110000000000,
    0b10000000010000000000,
    0b10000000010000000000,
    0b10000000010000000000,
    0b10000000010000000000,
    0b10000000010000000000,
    0b10000000010000000010,
    0b00000000010000000010,
    0b00000000010000000010,
    0b00000000010000000010,
    0b00000000010000000010,
    0b00000000010000000010,
    0b00000000010000000010,
    0b00000000011111111110
);

/* Dent de scie générique (descendante à droite) */
UIW_ICON_DEFINE(saw,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000111,
    0b00000000000000011101,
    0b00000000000001110001,
    0b00000000000111000001,
    0b00000000011100000001,
    0b00000001110000000001,
    0b00000111000000000001,
    0b00011100000000000001,
    0b01110000000000000001,
    0b11000000000000000001,
    0b00000000000000000000,
    0b00000000000000000000
);

/* Saw Up (monte vers la droite) */
UIW_ICON_DEFINE(sawD,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b11000000000000000001,
    0b01110000000000000001,
    0b00011100000000000001,
    0b00000111000000000001,
    0b00000001110000000001,
    0b00000000011100000001,
    0b00000000000111000001,
    0b00000000000001110001,
    0b00000000000000011101,
    0b00000000000000000111
);

/* Saw Down (descend vers la droite) */
UIW_ICON_DEFINE(sawU,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000111,
    0b00000000000000011101,
    0b00000000000001110001,
    0b00000000000111000001,
    0b00000000011100000001,
    0b00000001110000000001,
    0b00000111000000000001,
    0b00011100000000000001,
    0b01110000000000000001,
    0b11000000000000000001,
    0b00000000000000000000,
    0b00000000000000000000
);

/* Triangle */
UIW_ICON_DEFINE(triangle,
    0b00000000010000000000,
    0b00000000101000000000,
    0b00000001000100000000,
    0b00000010000010000000,
    0b00000100000001000000,
    0b00001000000000100000,
    0b00010000000000010000,
    0b00100000000000001000,
    0b01000000000000000100,
    0b10000000000000000010,
    0b00000000000000000001,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000
);

/* Bruit (damier) */
UIW_ICON_DEFINE(noise,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101,
    0b10101010101010101010,
    0b01010101010101010101
);

/* Filtres */
UIW_ICON_DEFINE(lp,
    0b11111111111111111111,
    0b10000000000000000000,
    0b10000000000000000000,
    0b10000000000000000000,
    0b10000000000000000000,
    0b10000000000000000000,
    0b11111111111111111111,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000
);

UIW_ICON_DEFINE(hp,
    0b11111111111111111111,
    0b00000000000000000001,
    0b00000000000000000001,
    0b00000000000000000001,
    0b00000000000000000001,
    0b00000000000000000001,
    0b11111111111111111111,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000,
    0b00000000000000000000
);

UIW_ICON_DEFINE(bp,
    0b00000000000000000000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b11111111111111111111,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000111111111111000,
    0b00000000000000000000,
    0b00000000000000000000
);

UIW_ICON_DEFINE(notch,
    0b11111111111111111111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11110000000000011111,
    0b11111111111111111111,
    0b00000000000000000000,
    0b00000000000000000000
);


/* ===== Utilitaires ===== */
static int ci_find(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n) {
            char a = p[i];
            char b = needle[i];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            ++i;
        }
        if (i == n) return 1;
    }
    return 0;
}

/* égalité insensible à la casse */
static int ci_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return (*a == '\0' && *b == '\0');
}

/* ===== Icônes basées sur labels ===== */
void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label) {
    if (!label) return;

    /* Spécifiques d'abord (évite “saw” qui capture “SawD/SawU”) */
    if (ci_eq(label, "sawd") || ci_eq(label, "sawdn"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_sawD, 20, 14); return; }

    if (ci_eq(label, "sawu") || ci_eq(label, "sawup"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_sawU, 20, 14); return; }

    /* Formes simples & alias */
    if (ci_eq(label, "sqr") || ci_eq(label, "square"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_square, 20, 14); return; }

    if (ci_eq(label, "tri") || ci_eq(label, "triangle"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_triangle, 20, 14); return; }

    if (ci_eq(label, "sine") || ci_eq(label, "sin"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_sine, 20, 14); return; }

    if (ci_eq(label, "noise") || ci_eq(label, "rnd") || ci_eq(label, "random"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_noise, 20, 14); return; }

    /* Générique (contient “saw”, au cas où d’autres variantes apparaissent) */
    if (ci_find(label, "saw"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_saw, 20, 14); return; }

    /* Fallback */
    uiw_draw_bitmap_bin(x, y, w, h, icon_noise, 20, 14);
}

void uiw_draw_filter_icon(int x, int y, int w, int h, const char *label) {
    if (!label) return;

    /* La table filterTypes contient des chaînes comme “1pLP”, “2pHP”, “2pBR”, etc. */
    if (ci_find(label, "br") || ci_find(label, "notch") || ci_find(label, "peak"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_notch, 20, 14); return; }

    if (ci_find(label, "bp"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_bp, 20, 14); return; }

    if (ci_find(label, "hp") || ci_find(label, "high"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_hp, 20, 14); return; }

    if (ci_find(label, "lp") || ci_find(label, "low"))
        { uiw_draw_bitmap_bin(x, y, w, h, icon_lp, 20, 14); return; }

    /* Fallback */
    uiw_draw_bitmap_bin(x, y, w, h, icon_notch, 20, 14);
}

/* ===== Router heuristique (inchangé) ===== */
ui_widget_type_t uiw_pick_from_kind_label_only(ui_param_kind_for_router_t kind, const char *label) {
    if (kind == UI_PARAM_BOOL) return UIW_SWITCH;
    if (kind == UI_PARAM_ENUM) {
        if (ci_find(label, "wave") || ci_find(label, "osc") || ci_find(label, "forme")
            || ci_find(label, "waveform")) return UIW_ENUM_ICON_WAVE;
        if (ci_find(label, "filter") || ci_find(label, "filtre") || ci_find(label, "type"))
            return UIW_ENUM_ICON_FILTER;
    }
    if (kind == UI_PARAM_CONT) return UIW_KNOB;
    return UIW_NONE;
}

ui_widget_type_t uiw_pick_from_labels(ui_param_kind_for_router_t kind,
                                      const char *label,
                                      const char *const *enum_labels,
                                      int enum_count) {
    (void)label;
    if (kind == UI_PARAM_BOOL) return UIW_SWITCH;
    if (kind == UI_PARAM_ENUM && enum_count > 0) {
        int wave_hits = 0, filt_hits = 0;
        for (int i = 0; i < enum_count; ++i) {
            const char *s = enum_labels[i];
            if (!s) continue;
            if (ci_find(s, "sine") || ci_find(s, "sin") || ci_find(s, "triangle")
             || ci_find(s, "square") || ci_find(s, "saw")
             || ci_find(s, "noise") || ci_find(s, "bruit"))
                wave_hits++;
            if (ci_find(s, "lp") || ci_find(s, "hp") || ci_find(s, "bp")
             || ci_find(s, "notch") || ci_find(s, "peak") || ci_find(s, "low") || ci_find(s, "high"))
                filt_hits++;
        }
        if (wave_hits >= filt_hits && wave_hits) return UIW_ENUM_ICON_WAVE;
        if (filt_hits) return UIW_ENUM_ICON_FILTER;
    }
    if (kind == UI_PARAM_CONT) return UIW_KNOB;
    return UIW_NONE;
}
