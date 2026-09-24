#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "main.h"
#include "IPC/audio_boot_diagnostic_reader.h"
#include "drv_display.h"
#include "font.h"
#include "Keyboard/keyboard_runtime.h"
#include "param_registry.h"
#include "ui_core.h"
#include "ui_macro_interaction.h"
#include "ui_param.h"
#include "ui_widgets.h"
#include "pages/ui_page_template_play.h"
#include "pages/ui_page_template_mod.h"
#include "Param/engine_model_catalog.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_media_clock.h"
#include "Param/stack_waveform.h"
#include "Storage/project_control.h"
#include "UI/ui_sampler_playhead.h"
#include "UI/ui_render_prof.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_model.h"
#include "Mod/mod_destination_control.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Param/spectral_window.h"
#include "IPC/audio_waveform_reader.h"
#include "IPC/synth_waveform_reader.h"

volatile ui_render_prof_t g_ui_render_prof;
static uint32_t g_ui_render_prof_frame_cpu_cycles;
static uint32_t g_ui_render_prof_frame_wall_start;
static uint8_t g_ui_render_prof_frame_active;

static void ui_render_prof_add_cycles(volatile ui_render_prof_cycles_t *stats,
                                      uint32_t elapsed)
{
    stats->count++;
    stats->total_cycles += elapsed;
    if (elapsed < stats->min_cycles) stats->min_cycles = elapsed;
    if (elapsed > stats->max_cycles) stats->max_cycles = elapsed;
}

static void ui_render_prof_add_value(volatile ui_render_prof_value_t *stats,
                                     uint32_t value)
{
    stats->count++;
    stats->total_value += value;
    if (value < stats->min_value) stats->min_value = value;
    if (value > stats->max_value) stats->max_value = value;
}

static void ui_render_prof_reset_cycles(volatile ui_render_prof_cycles_t *stats)
{
    stats->min_cycles = UINT32_MAX;
}

static void ui_render_prof_reset_cycles_block(
    volatile ui_render_prof_cycles_t *stats, uint32_t count)
{
    for (uint32_t i = 0U; i < count; ++i)
        ui_render_prof_reset_cycles(&stats[i]);
}

static void ui_render_prof_reset_value(volatile ui_render_prof_value_t *stats)
{
    stats->min_value = UINT32_MAX;
}

__attribute__((used, noinline, externally_visible))
void ui_render_prof_reset(void)
{
    memset((void *)&g_ui_render_prof, 0, sizeof(g_ui_render_prof));
    g_ui_render_prof_frame_cpu_cycles = 0U;
    g_ui_render_prof_frame_wall_start = 0U;
    g_ui_render_prof_frame_active = 0U;
    ui_render_prof_flush_reset_tracking();
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.header);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.prepare);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.special);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.special_sampler_ram);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.special_synth_live);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.special_wave_wavetable);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.special_wave_classic);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.audio_fx_phase);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.audio_fx_used);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.slot_0);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.slot_1);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.slot_2);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.slot_3);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.group);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.footer);
    ui_render_prof_reset_cycles(&g_ui_render_prof.renderer.finalize);
    ui_render_prof_reset_cycles_block(
        &g_ui_render_prof.renderer.slot_detail.value_context,
        (uint32_t)(sizeof(g_ui_render_prof.renderer.slot_detail)
            / sizeof(ui_render_prof_cycles_t)));
    ui_render_prof_reset_cycles_block(
        &g_ui_render_prof.renderer.widget.virtual_slot,
        (uint32_t)(sizeof(g_ui_render_prof.renderer.widget)
            / sizeof(ui_render_prof_cycles_t)));
    ui_render_prof_reset_value(&g_ui_render_prof.renderer.frame_cpu_cycles);
    ui_render_prof_reset_value(&g_ui_render_prof.renderer.frame_wall_ticks);
    ui_render_prof_reset_cycles(&g_ui_render_prof.flush.snapshot_memcpy);
    ui_render_prof_reset_cycles(&g_ui_render_prof.flush.dirty_scan);
    ui_render_prof_reset_cycles(&g_ui_render_prof.flush.dirty_pack);
    ui_render_prof_reset_cycles(&g_ui_render_prof.flush.page_prepare_launch);
    ui_render_prof_reset_cycles(&g_ui_render_prof.flush.full_prepare_launch);
    ui_render_prof_reset_value(&g_ui_render_prof.flush.bytes_per_flush);
    ui_render_prof_reset_value(&g_ui_render_prof.flush.update_calls_per_flush);
    ui_render_prof_reset_value(&g_ui_render_prof.flush.service_polls_per_flush);
    ui_render_prof_reset_value(&g_ui_render_prof.flush.wall_ticks);
    g_ui_render_prof.renderer.frame_wall_tick_hz = brick_media_clock_tick_hz();
    g_ui_render_prof.flush.wall_tick_hz = brick_media_clock_tick_hz();
}

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

static CTRL_STATE ui_renderer_template_wavetable_cache_t
    g_ui_renderer_template_wavetable_cache;


/* Template formatting, widget families and common chrome remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Renderer/ui_renderer_formatting.inc"

#include "Renderer/ui_renderer_mod_widgets.inc"

#include "Renderer/ui_renderer_config_widgets.inc"

#include "Renderer/ui_renderer_filter_adsr.inc"

#include "Renderer/ui_renderer_sampler_wavetable.inc"

#include "Renderer/ui_renderer_synth_widgets.inc"

#include "Renderer/ui_renderer_chrome.inc"
