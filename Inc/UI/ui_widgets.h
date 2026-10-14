#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdint.h>

#include "Audio/Publications/audio_waveform_contract.h"
#include "param_registry.h"

#define UIW_AUDIO_SCOPE_MAX_COLUMNS 126U

typedef struct
{
    uint32_t generation;
    uint8_t target;
    uint8_t entity_id;
    uint8_t valid;
    uint8_t triggered;
    uint8_t display_peak;
    int8_t top[UIW_AUDIO_SCOPE_MAX_COLUMNS];
    int8_t bottom[UIW_AUDIO_SCOPE_MAX_COLUMNS];
} uiw_audio_scope_cache_t;

typedef enum
{
    UIW_WIDGET_NONE = 0,
    UIW_WIDGET_EMPTY,
    UIW_WIDGET_BAR,
    UIW_WIDGET_BIPOLAR_BAR,
    UIW_WIDGET_SWITCH,
    UIW_WIDGET_ENUM_TEXT,
    UIW_WIDGET_WAVE_ICON,
    UIW_WIDGET_FILTER_ICON,
    UIW_WIDGET_ALGO_ICON,
    UIW_WIDGET_JACK,
    UIW_WIDGET_KEYBOARD,
} uiw_widget_type_t;

void uiw_draw_value_bar(int x, int y, int w, int h, float value, float vmin, float vmax);
void uiw_draw_bipolar_bar(int x, int y, int w, int h, float value, float vmin, float vmax);
void uiw_draw_switch(int x, int y, int w, int h, uint8_t on);
void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label);
void uiw_draw_filter_icon(int x, int y, int w, int h, const char *label);
void uiw_draw_algo_icon(int x, int y, int w, int h, uint8_t algorithm);
void uiw_draw_enum_text(int x, int y, int w, int h, const char *label);
void uiw_draw_jack_icon(int x, int y, int w, int h);
void uiw_draw_keyboard_icon(int x, int y, int w, int h);
void uiw_audio_scope_reset(uiw_audio_scope_cache_t *cache);
uint8_t uiw_draw_audio_scope(audio_waveform_target_t target,
                             uint8_t entity_id,
                             int x, int y, int w, int h,
                             uiw_audio_scope_cache_t *cache);

uiw_widget_type_t uiw_pick_widget_type(const param_desc_t *desc, const char *enum_label);

#endif /* UI_WIDGETS_H */
