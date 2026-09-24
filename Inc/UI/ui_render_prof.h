#ifndef UI_RENDER_PROF_H
#define UI_RENDER_PROF_H

#include <stdint.h>

typedef struct
{
    uint32_t count;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
} ui_render_prof_cycles_t;

typedef struct
{
    uint32_t count;
    uint32_t min_value;
    uint32_t max_value;
    uint64_t total_value;
} ui_render_prof_value_t;

typedef struct
{
    ui_render_prof_cycles_t value_context;
    ui_render_prof_cycles_t value_format;
    ui_render_prof_cycles_t label_format;
    ui_render_prof_cycles_t text_measure;
    ui_render_prof_cycles_t text_draw;
    ui_render_prof_cycles_t background_frame;
    ui_render_prof_cycles_t widget_graphic;
    ui_render_prof_cycles_t inversion_flash_feedback;
    ui_render_prof_cycles_t page_callback;
} ui_render_prof_slot_detail_t;

typedef struct
{
    ui_render_prof_cycles_t virtual_slot;
    ui_render_prof_cycles_t empty;
    ui_render_prof_cycles_t switch_widget;
    ui_render_prof_cycles_t enum_text;
    ui_render_prof_cycles_t enum_fallback;
    ui_render_prof_cycles_t jack_icon;
    ui_render_prof_cycles_t keyboard_icon;
    ui_render_prof_cycles_t wave_icon;
    ui_render_prof_cycles_t filter_icon;
    ui_render_prof_cycles_t algorithm_bitmap;
    ui_render_prof_cycles_t bargraph;
    ui_render_prof_cycles_t bipolar_bargraph;
    ui_render_prof_cycles_t custom_lfo;
    ui_render_prof_cycles_t custom_text;
    ui_render_prof_cycles_t custom_waveform;
    ui_render_prof_cycles_t custom_track_cfg;
    ui_render_prof_cycles_t track_cfg_text;
    ui_render_prof_cycles_t track_cfg_bitmap;
    ui_render_prof_cycles_t custom_filter;
    ui_render_prof_cycles_t custom_adsr;
    ui_render_prof_cycles_t grouped_adsr;
    ui_render_prof_cycles_t grouped_filter;
    ui_render_prof_cycles_t grouped_lfo;
    ui_render_prof_cycles_t grouped_spectral;
    ui_render_prof_cycles_t grouped_fm_pitch;
    ui_render_prof_cycles_t audio_fx;
} ui_render_prof_widget_t;

typedef struct
{
    ui_render_prof_cycles_t header;
    ui_render_prof_cycles_t prepare;
    ui_render_prof_cycles_t special;
    ui_render_prof_cycles_t special_sampler_ram;
    ui_render_prof_cycles_t special_synth_live;
    ui_render_prof_cycles_t special_wave_wavetable;
    ui_render_prof_cycles_t special_wave_classic;
    ui_render_prof_cycles_t audio_fx_phase;
    ui_render_prof_cycles_t audio_fx_used;
    ui_render_prof_cycles_t slot_0;
    ui_render_prof_cycles_t slot_1;
    ui_render_prof_cycles_t slot_2;
    ui_render_prof_cycles_t slot_3;
    ui_render_prof_cycles_t group;
    ui_render_prof_cycles_t footer;
    ui_render_prof_cycles_t finalize;
    uint32_t header_cache_hit_count;
    uint32_t header_cache_miss_count;
    uint32_t footer_cache_hit_count;
    uint32_t footer_cache_miss_count;
    ui_render_prof_slot_detail_t slot_detail;
    ui_render_prof_widget_t widget;
    ui_render_prof_value_t frame_cpu_cycles;
    ui_render_prof_value_t frame_wall_ticks;
    uint32_t frame_wall_tick_hz;
    uint32_t frame_cancel_count;
} ui_render_prof_renderer_t;

typedef struct
{
    uint32_t complete_count;
    uint32_t failed_count;
    uint32_t current_update_calls;
    uint32_t current_service_polls;
    uint32_t unchanged_count;
    uint32_t full_window_count;
    uint32_t partial_window_count;
    ui_render_prof_cycles_t snapshot_memcpy;
    ui_render_prof_cycles_t dirty_scan;
    ui_render_prof_cycles_t dirty_pack;
    ui_render_prof_cycles_t page_prepare_launch;
    ui_render_prof_cycles_t full_prepare_launch;
    ui_render_prof_value_t bytes_per_flush;
    ui_render_prof_value_t update_calls_per_flush;
    ui_render_prof_value_t service_polls_per_flush;
    ui_render_prof_value_t wall_ticks;
    uint32_t wall_tick_hz;
} ui_render_prof_flush_t;

typedef struct
{
    ui_render_prof_renderer_t renderer;
    ui_render_prof_flush_t flush;
} ui_render_prof_t;

extern volatile ui_render_prof_t g_ui_render_prof;

void ui_render_prof_reset(void);
void ui_render_prof_note_flush_service_poll(void);
void ui_render_prof_flush_reset_tracking(void);

#endif
