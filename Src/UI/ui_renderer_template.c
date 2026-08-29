#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "Platform/cpu_load.h"
#include "drv_display.h"
#include "font.h"
#include "Keyboard/keyboard_runtime.h"
#include "mixer.h"
#include "param_registry.h"
#include "ui_core.h"
#include "ui_macro_interaction.h"
#include "ui_param.h"
#include "ui_widgets.h"
#include "pages/ui_page_template_play.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/brick6_stack_waveform.h"
#include "Storage/project_control.h"
#include "IPC/live_clock.h"
#include "UI/ui_sampler_playhead.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_model.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Audio/spectral_window.h"
#include "Audio/audio_waveform_capture.h"
#include "Audio/synth_waveform_snapshot.h"
#include "Board/board_audio_format.h"

#define UI_TEMPLATE_FRAME_W          32
#define UI_TEMPLATE_FRAME_H          38
#define UI_TEMPLATE_FRAME_Y          17
#define UI_TEMPLATE_FOOTER_Y         55
#define UI_TEMPLATE_FOOTER_H         9
#define UI_TEMPLATE_FOOTER_TEXT_Y    57
#define UI_TEMPLATE_CARD_TEXT_Y      (UI_TEMPLATE_FRAME_H - 7)
#define UI_TEMPLATE_CARD_LABEL_Y     UI_TEMPLATE_CARD_TEXT_Y
#define UI_TEMPLATE_CARD_LABEL_H     7
#define UI_TEMPLATE_CARD_WIDGET_X_PAD 1
#define UI_TEMPLATE_CARD_WIDGET_Y    1
#define UI_TEMPLATE_CARD_WIDGET_W    (UI_TEMPLATE_FRAME_W - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD))
#define UI_TEMPLATE_CARD_WIDGET_H    (UI_TEMPLATE_CARD_LABEL_Y - UI_TEMPLATE_CARD_WIDGET_Y - 1)
#define UI_TEMPLATE_GROUP_WIDGET_X   1
#define UI_TEMPLATE_GROUP_WIDGET_Y   (UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y)
#define UI_TEMPLATE_GROUP_WIDGET_W   126
#define UI_TEMPLATE_GROUP_WIDGET_H   UI_TEMPLATE_CARD_WIDGET_H
#define UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST 0U
#define UI_TEMPLATE_FILTER_GROUP_SLOT_COUNT 2U
#define UI_TEMPLATE_LFO_GROUP_SLOT_FIRST 1U
#define UI_TEMPLATE_LFO_GROUP_SLOT_COUNT 2U
#define UI_TEMPLATE_CARD_LABEL_MAX_PX 28U
#define UI_TEMPLATE_HEADER_TITLE_X   43
#define UI_TEMPLATE_HEADER_TITLE_W   42
#define UI_TEMPLATE_SAMPLER_NAME_Y   17
#define UI_TEMPLATE_SAMPLER_WAVE_X   1
#define UI_TEMPLATE_SAMPLER_WAVE_Y   25
#define UI_TEMPLATE_SAMPLER_WAVE_W   126
#define UI_TEMPLATE_SAMPLER_WAVE_H   17
#define UI_TEMPLATE_SAMPLER_LABEL_Y  (UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_LABEL_Y)
#define UI_TEMPLATE_SAMPLER_TEXT_MAX_PX UI_TEMPLATE_CARD_LABEL_MAX_PX
#define UI_TEMPLATE_SAMPLER_WAVE_INNER_W (UI_TEMPLATE_SAMPLER_WAVE_W - 2)
#define UI_TEMPLATE_SAMPLER_WAVE_INNER_H (UI_TEMPLATE_SAMPLER_WAVE_H - 2)
#define UI_TEMPLATE_WAVE_WT_X       1
#define UI_TEMPLATE_WAVE_WT_Y       12
#define UI_TEMPLATE_WAVE_WT_W       126
#define UI_TEMPLATE_WAVE_WT_H       36
#define UI_TEMPLATE_WAVE_WT_INNER_W (UI_TEMPLATE_WAVE_WT_W - 2)
#define UI_TEMPLATE_WAVE_WT_INNER_H (UI_TEMPLATE_WAVE_WT_H - 2)
#define UI_TEMPLATE_WAVE_WT_LAYER_TARGET 8U
#define UI_TEMPLATE_WAVE_WT_MAX_LAYERS   8U
#define UI_TEMPLATE_WAVE_WT_TRACE_POINTS 96U
#define UI_TEMPLATE_WAVE_WT_DEPTH_X_PX   10
#define UI_TEMPLATE_WAVE_WT_DEPTH_Y_PX   18
#define UI_TEMPLATE_WAVE_WT_CONTEXT_BASE_Y 9
#define UI_TEMPLATE_WAVE_WT_CONTEXT_AMP_HALF_PX 2
#define UI_TEMPLATE_WAVE_WT_POS_AMP_HALF_PX 3
#define UI_TEMPLATE_STACK_WAVE_CACHE_MAX_W (OLED_WIDTH - 2)

typedef struct
{
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t locked[4];
} ui_renderer_template_adsr_shape_t;

typedef struct
{
    uint8_t valid;
    uint16_t global_slot;
    uint32_t table_generation;
    uint32_t preview_generation;
    uint32_t frame_count;
    uint16_t global_peak;
    uint8_t layer_count;
    float start_value;
    float end_value;
    uint8_t x_start[UI_TEMPLATE_WAVE_WT_MAX_LAYERS];
    int8_t center_y[UI_TEMPLATE_WAVE_WT_MAX_LAYERS];
    int8_t y[UI_TEMPLATE_WAVE_WT_MAX_LAYERS][UI_TEMPLATE_WAVE_WT_TRACE_POINTS];
    uint8_t pos_valid;
    float pos_value;
    uint8_t pos_x_start;
    int8_t pos_y[UI_TEMPLATE_WAVE_WT_TRACE_POINTS];
} ui_renderer_template_wavetable_cache_t;

static ui_renderer_template_wavetable_cache_t g_ui_renderer_template_wavetable_cache;


/* Template formatting, widget families and common chrome remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Renderer/ui_renderer_formatting.inc"

#include "Renderer/ui_renderer_mod_widgets.inc"

#include "Renderer/ui_renderer_config_widgets.inc"

#include "Renderer/ui_renderer_filter_adsr.inc"

#include "Renderer/ui_renderer_sampler_wavetable.inc"

#include "Renderer/ui_renderer_synth_widgets.inc"

#include "Renderer/ui_renderer_chrome.inc"
