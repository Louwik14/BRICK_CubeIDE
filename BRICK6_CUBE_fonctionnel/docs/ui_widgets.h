#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "display.h"
#include "ui_knob.h"
#include "ui_primitives.h"
#include "ui_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UIW_NONE = 0,
    UIW_KNOB,
    UIW_SWITCH,
    UIW_ENUM_ICON_WAVE,
    UIW_ENUM_ICON_FILTER
    // plus tard: UIW_ENV_ICON, UIW_LFO_ICON, etc.
} ui_widget_type_t;

/* Dessins “centrales” pour les cadres paramètres */
void uiw_draw_knob(int x, int y, int w, int h, int v, int vmin, int vmax);
void uiw_draw_switch(int x, int y, int w, int h, bool on);

/* Variante: blit d’un motif 12x12 défini en bitmap (ancien test) */
void uiw_draw_square12_bitmap(int x, int y, int w, int h, const uint8_t *bits);

/* Dessin générique d’un motif 14x14 en binaire (uint16_t[14]) */
void uiw_draw_bitmap14_bin(int x, int y, int w, int h, const uint16_t *rows);

/* Icônes “enum” → uniquement basées sur le texte du label */
void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label);
void uiw_draw_filter_icon(int x, int y, int w, int h, const char *label);

/* Router heuristique basique selon kind+label+enum labels */
typedef ui_param_kind_t ui_param_kind_for_router_t;

ui_widget_type_t uiw_pick_from_kind_label_only(ui_param_kind_for_router_t kind, const char *label);
ui_widget_type_t uiw_pick_from_labels(ui_param_kind_for_router_t kind,
                                      const char *label,
                                      const char *const *enum_labels,
                                      int enum_count);

#ifdef __cplusplus
}
#endif
