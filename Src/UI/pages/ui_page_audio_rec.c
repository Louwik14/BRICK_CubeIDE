#include "pages/ui_page_audio_rec.h"

#include "buttons.h"
#include "drv_display.h"
#include "font.h"
#include "Storage/sample_capture.h"
#include "ui_event.h"
#include "ui_page_manager.h"

#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
#include "stm32h7xx_hal.h"
#endif
#include <stdio.h>

typedef enum
{
    UI_AUDIO_REC_WAVE_VERTICAL_FIXED_FULL_SCALE = 0,
    UI_AUDIO_REC_WAVE_VERTICAL_LOCAL_VIEW_NORMALIZED
} ui_audio_rec_wave_vertical_mode_t;

#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
static uint32_t g_ui_page_audio_rec_last_waveform_ms;
#endif

static void ui_page_audio_rec_enter(void)
{
    sample_capture_model_set_view(SAMPLE_CAPTURE_VIEW_AUDIO_REC);
}

static void ui_page_rec_edit_enter(void)
{
    sample_capture_model_set_view(SAMPLE_CAPTURE_VIEW_REC_EDIT);
}

static void ui_page_audio_rec_handle_event(const ui_event_t *ev)
{
    if(ev == 0)
    {
        return;
    }

    if((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SAMPLE_CAPTURE_TRACK_COUNT))
    {
        (void)sample_capture_model_toggle_route(ev->id);
    }
}

static void ui_page_rec_edit_handle_event(const ui_event_t *ev)
{
    if(ev == 0)
    {
        return;
    }

    if(ev->type == UI_EVENT_HALL_PRESS)
    {
        sample_capture_state_t state;
        sample_capture_model_get_state(&state);
        if((state.take_valid != 0U) && (state.edit_end_frame > state.edit_start_frame))
        {
            (void)sample_capture_model_audition_trimmed();
        }
        return;
    }

    if(ev->type != UI_EVENT_BUTTON_PRESS)
    {
        return;
    }

    switch((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            (void)sample_capture_model_return_to_audio_rec();
            ui_page_set(UI_PAGE_AUDIO_REC);
            break;

        case BTN_PAGE_2:
            (void)sample_capture_model_save_trimmed();
            break;

        case BTN_PAGE_3:
            (void)sample_capture_model_assign_trimmed();
            break;

        default:
            break;
    }
}

static void ui_page_audio_rec_tick(void)
{
    sample_capture_model_service();
    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT && ui_page_get_id() != UI_PAGE_REC_EDIT)
    {
        ui_page_set(UI_PAGE_REC_EDIT);
    }
}

static const char *ui_page_audio_rec_phase_label(sample_capture_phase_t phase)
{
    switch(phase)
    {
        case SAMPLE_CAPTURE_PHASE_IDLE: return "IDLE";
        case SAMPLE_CAPTURE_PHASE_ARMED: return "ARMED";
        case SAMPLE_CAPTURE_PHASE_WAIT_QUANT: return "WAIT";
        case SAMPLE_CAPTURE_PHASE_RECORDING: return "REC";
        case SAMPLE_CAPTURE_PHASE_STOPPING: return "STOP";
        case SAMPLE_CAPTURE_PHASE_REC_EDIT: return "EDIT";
        case SAMPLE_CAPTURE_PHASE_SAVED: return "SAVED";
        case SAMPLE_CAPTURE_PHASE_ERROR: return "ERROR";
        default: return "?";
    }
}

static const char *ui_page_audio_rec_quant_label(sample_capture_quant_t quant)
{
    switch(quant)
    {
        case SAMPLE_CAPTURE_QUANT_NOW: return "NOW";
        case SAMPLE_CAPTURE_QUANT_BAR: return "BAR";
        case SAMPLE_CAPTURE_QUANT_PATTERN: return "PATTERN";
        default: return "?";
    }
}

static const char *ui_page_audio_rec_error_label(sample_capture_error_t error)
{
    switch(error)
    {
        case SAMPLE_CAPTURE_ERROR_NONE: return "";
        case SAMPLE_CAPTURE_ERROR_INVALID_ARG: return "INVALID";
        case SAMPLE_CAPTURE_ERROR_NO_ROUTE: return "NO ROUTE";
        case SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE: return "LOOPER REC";
        case SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE: return "REC BUSY";
        case SAMPLE_CAPTURE_ERROR_SD_BUSY: return "SD BUSY";
        case SAMPLE_CAPTURE_ERROR_SD_IO: return "SD IO";
        case SAMPLE_CAPTURE_ERROR_NO_TAKE: return "NO TAKE";
        case SAMPLE_CAPTURE_ERROR_NO_SLOT: return "POOL FULL";
        case SAMPLE_CAPTURE_ERROR_LOAD_FAIL: return "LOAD FAIL";
        case SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL: return "PREVIEW";
        default: return "ERROR";
    }
}

static uint32_t ui_page_audio_rec_frame_to_wave_idx(const sample_capture_state_t *state,
                                                    uint32_t frame)
{
    if((state->waveform_bucket_frames == 0U) || (state->waveform_count == 0U))
    {
        return 0U;
    }

    uint32_t idx = frame / state->waveform_bucket_frames;
    if(idx > state->waveform_count)
    {
        idx = state->waveform_count;
    }
    return idx;
}

static void ui_page_audio_rec_minmax_in_range(const sample_capture_state_t *state,
                                              uint32_t idx0,
                                              uint32_t idx1,
                                              int16_t *out_min,
                                              int16_t *out_max)
{
    int16_t min_v = 0;
    int16_t max_v = 0;

    if(idx0 >= state->waveform_count)
    {
        if(out_min != 0)
        {
            *out_min = 0;
        }
        if(out_max != 0)
        {
            *out_max = 0;
        }
        return;
    }
    if(idx1 <= idx0)
    {
        idx1 = idx0 + 1U;
    }
    if(idx1 > state->waveform_count)
    {
        idx1 = state->waveform_count;
    }

    min_v = state->waveform[idx0].min;
    max_v = state->waveform[idx0].max;
    for(uint32_t idx = idx0; idx < idx1; ++idx)
    {
        if(state->waveform[idx].min < min_v)
        {
            min_v = state->waveform[idx].min;
        }
        if(state->waveform[idx].max > max_v)
        {
            max_v = state->waveform[idx].max;
        }
    }
    if(out_min != 0)
    {
        *out_min = min_v;
    }
    if(out_max != 0)
    {
        *out_max = max_v;
    }
}

static uint16_t ui_page_audio_rec_abs_i16(int16_t v)
{
    if(v >= 0)
    {
        return (uint16_t)v;
    }
    if(v == (int16_t)-32768)
    {
        return 32768U;
    }
    return (uint16_t)(-v);
}

static uint16_t ui_page_audio_rec_take_peak(const sample_capture_state_t *state)
{
    uint16_t peak = 0U;
    if(state == 0)
    {
        return 0U;
    }
    for(uint16_t idx = 0U; idx < state->waveform_count; ++idx)
    {
        const uint16_t abs_min = ui_page_audio_rec_abs_i16(state->waveform[idx].min);
        const uint16_t abs_max = ui_page_audio_rec_abs_i16(state->waveform[idx].max);
        if(abs_min > peak)
        {
            peak = abs_min;
        }
        if(abs_max > peak)
        {
            peak = abs_max;
        }
    }
    if(state->line_peak > peak)
    {
        peak = state->line_peak;
    }
    const uint16_t global_peak = sample_capture_model_global_overview_peak();
    if(global_peak > peak)
    {
        peak = global_peak;
    }
    return peak;
}

static uint16_t ui_page_audio_rec_edit_vertical_ref(const sample_capture_state_t *state)
{
    uint16_t ref = ui_page_audio_rec_take_peak(state);
    if(ref < 4096U)
    {
        ref = 4096U;
    }
    if(ref > SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE)
    {
        ref = SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE;
    }
    return ref;
}

static void ui_page_audio_rec_draw_minmax_column(int x,
                                                 int inner_y,
                                                 int inner_h,
                                                 int cy,
                                                 int16_t min_v,
                                                 int16_t max_v,
                                                 uint16_t vertical_ref)
{
    const int max_amp = (inner_h / 2) - 2;
    if((max_amp <= 0) || (vertical_ref == 0U))
    {
        return;
    }

    int y0 = cy - (int)(((int32_t)max_v * (int32_t)max_amp) / (int32_t)vertical_ref);
    int y1 = cy - (int)(((int32_t)min_v * (int32_t)max_amp) / (int32_t)vertical_ref);
    if(y0 > y1)
    {
        const int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    if(y0 < inner_y)
    {
        y0 = inner_y;
    }
    if(y1 > (inner_y + inner_h - 1))
    {
        y1 = inner_y + inner_h - 1;
    }
    drv_display_draw_line(x, y0, x, y1);
}

static void ui_page_audio_rec_draw_zero_line(int x, int y, int w, int h)
{
    if((w <= 2) || (h <= 2))
    {
        return;
    }
    const int cy = y + (h / 2);
    drv_display_draw_line(x + 1, cy, x + w - 2, cy);
}

static int ui_page_audio_rec_sample_to_y(int16_t sample,
                                         int inner_y,
                                         int inner_h,
                                         int cy,
                                         uint16_t vertical_ref)
{
    const int max_amp = (inner_h / 2) - 3;
    if((max_amp <= 0) || (vertical_ref == 0U))
    {
        return cy;
    }

    int y = cy - (int)(((int32_t)sample * (int32_t)max_amp) / (int32_t)vertical_ref);
    if(y < inner_y)
    {
        y = inner_y;
    }
    if(y > (inner_y + inner_h - 1))
    {
        y = inner_y + inner_h - 1;
    }
    return y;
}

static void ui_page_audio_rec_draw_line_waveform(const sample_capture_state_t *state,
                                                 int x,
                                                 int y,
                                                 int w,
                                                 int h,
                                                 uint16_t vertical_ref,
                                                 uint16_t *out_segments)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 1) || (inner_h <= 2) || (state->line_valid == 0U)
            || (state->line_count < 2U) || (vertical_ref == 0U))
    {
        if(out_segments != 0)
        {
            *out_segments = 0U;
        }
        return;
    }

    uint16_t segments = 0U;
    int prev_x = inner_x;
    int prev_y = ui_page_audio_rec_sample_to_y(state->line[0],
                                               inner_y,
                                               inner_h,
                                               cy,
                                               vertical_ref);

    for(uint16_t point = 1U; point < state->line_count; ++point)
    {
        const int draw_x = inner_x
            + (int)(((uint32_t)point * (uint32_t)(inner_w - 1))
                / (uint32_t)(state->line_count - 1U));
        const int draw_y = ui_page_audio_rec_sample_to_y(state->line[point],
                                                         inner_y,
                                                         inner_h,
                                                         cy,
                                                         vertical_ref);
        if(draw_x != prev_x || draw_y != prev_y)
        {
            drv_display_draw_line(prev_x, prev_y, draw_x, draw_y);
            segments++;
        }
        prev_x = draw_x;
        prev_y = draw_y;
    }
    if(out_segments != 0)
    {
        *out_segments = segments;
    }
}

static void ui_page_audio_rec_draw_waveform_range(const sample_capture_state_t *state,
                                                  int x,
                                                  int y,
                                                  int w,
                                                  int h,
                                                  uint32_t view_start_frame,
                                                  uint32_t view_frames,
                                                  ui_audio_rec_wave_vertical_mode_t vertical_mode,
                                                  uint16_t vertical_ref_override)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 0) || (inner_h <= 2) || (state->waveform_count == 0U))
    {
        return;
    }
    if(view_frames == 0U)
    {
        view_frames = 1U;
    }

    const uint32_t view_end_frame = view_start_frame + view_frames;
    uint16_t vertical_ref = (vertical_ref_override != 0U)
        ? vertical_ref_override
        : SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE;
    if(vertical_mode == UI_AUDIO_REC_WAVE_VERTICAL_LOCAL_VIEW_NORMALIZED)
    {
        uint32_t norm_idx0 = ui_page_audio_rec_frame_to_wave_idx(state, view_start_frame);
        uint32_t norm_idx1 = ui_page_audio_rec_frame_to_wave_idx(state, view_end_frame);
        if(state->recorded_frames == 0U)
        {
            norm_idx0 = 0U;
            norm_idx1 = state->waveform_count;
        }
        if(norm_idx1 <= norm_idx0)
        {
            norm_idx1 = norm_idx0 + 1U;
        }
        if(norm_idx1 > state->waveform_count)
        {
            norm_idx1 = state->waveform_count;
        }

        int16_t norm_min = 0;
        int16_t norm_max = 0;
        ui_page_audio_rec_minmax_in_range(state, norm_idx0, norm_idx1, &norm_min, &norm_max);
        const uint16_t abs_min = ui_page_audio_rec_abs_i16(norm_min);
        const uint16_t abs_max = ui_page_audio_rec_abs_i16(norm_max);
        vertical_ref = (abs_min > abs_max) ? abs_min : abs_max;
        if(vertical_ref < 2048U)
        {
            vertical_ref = 2048U;
        }
    }

    for(int col = 0; col < inner_w; ++col)
    {
        uint32_t idx0 = 0U;
        uint32_t idx1 = 0U;
        if(state->recorded_frames != 0U)
        {
            const uint32_t frame0 = view_start_frame
                + (uint32_t)(((uint64_t)col * (uint64_t)view_frames) / (uint64_t)inner_w);
            uint32_t frame1 = view_start_frame
                + (uint32_t)(((uint64_t)(col + 1) * (uint64_t)view_frames) / (uint64_t)inner_w);
            if(frame1 <= frame0)
            {
                frame1 = frame0 + 1U;
            }
            idx0 = ui_page_audio_rec_frame_to_wave_idx(state, frame0);
            idx1 = ui_page_audio_rec_frame_to_wave_idx(state, frame1);
        }
        else
        {
            idx0 = (uint32_t)(((uint64_t)col * (uint64_t)state->waveform_count)
                    / (uint64_t)inner_w);
            idx1 = (uint32_t)(((uint64_t)(col + 1) * (uint64_t)state->waveform_count)
                    / (uint64_t)inner_w);
        }

        int16_t min_v = 0;
        int16_t max_v = 0;
        ui_page_audio_rec_minmax_in_range(state, idx0, idx1, &min_v, &max_v);
        ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                             inner_y,
                                             inner_h,
                                             cy,
                                             min_v,
                                             max_v,
                                             vertical_ref);
    }
}

static uint8_t ui_page_audio_rec_draw_global_overview(int x,
                                                      int y,
                                                      int w,
                                                      int h,
                                                      uint32_t view_start_frame,
                                                      uint32_t view_frames,
                                                      uint16_t vertical_ref)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 0) || (inner_h <= 2) || (vertical_ref == 0U)
            || (sample_capture_model_global_overview_ready() == 0U))
    {
        return 0U;
    }
    if(view_frames == 0U)
    {
        view_frames = 1U;
    }

    for(int col = 0; col < inner_w; ++col)
    {
        const uint32_t frame0 = view_start_frame
            + (uint32_t)(((uint64_t)col * (uint64_t)view_frames) / (uint64_t)inner_w);
        uint32_t frame1 = view_start_frame
            + (uint32_t)(((uint64_t)(col + 1) * (uint64_t)view_frames) / (uint64_t)inner_w);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }

        int16_t min_v = 0;
        int16_t max_v = 0;
        if(sample_capture_model_global_overview_minmax(frame0,
                                                       frame1 - frame0,
                                                       &min_v,
                                                       &max_v) == 0U)
        {
            continue;
        }
        ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                             inner_y,
                                             inner_h,
                                             cy,
                                             min_v,
                                             max_v,
                                             vertical_ref);
    }
    return 1U;
}

static uint8_t ui_page_audio_rec_wavecache_request_visible(const waveform_cache_handle_t *handle,
                                                           waveform_cache_level_id_t level,
                                                           uint32_t view_start_frame,
                                                           uint32_t view_frames,
                                                           uint16_t inner_w,
                                                           uint32_t *out_tile_start,
                                                           uint32_t *out_tile_count)
{
    uint32_t frames_per_column = 0U;
    if((handle == 0)
            || (inner_w == 0U)
            || (waveform_cache_level_frames_per_column(level, &frames_per_column) == 0U)
            || (frames_per_column == 0U))
    {
        return 0U;
    }

    const uint32_t view_end = view_start_frame + view_frames;
    uint32_t col0 = view_start_frame / frames_per_column;
    uint32_t col1 = (view_end + frames_per_column - 1U) / frames_per_column;
    if(col1 <= col0)
    {
        col1 = col0 + 1U;
    }
    const uint32_t max_cols =
        (handle->frame_count + frames_per_column - 1U) / frames_per_column;
    if(max_cols == 0U)
    {
        return 0U;
    }
    if(col1 > max_cols)
    {
        col1 = max_cols;
    }
    const uint32_t tile0 = col0 / WAVEFORM_CACHE_TILE_COLUMNS;
    uint32_t tile1 = (col1 + WAVEFORM_CACHE_TILE_COLUMNS - 1U)
        / WAVEFORM_CACHE_TILE_COLUMNS;
    if(tile1 <= tile0)
    {
        tile1 = tile0 + 1U;
    }
    const uint32_t max_tiles =
        (max_cols + WAVEFORM_CACHE_TILE_COLUMNS - 1U) / WAVEFORM_CACHE_TILE_COLUMNS;
    if(tile1 > max_tiles)
    {
        tile1 = max_tiles;
    }

    uint32_t req_start = tile0;
    if(req_start > 0U)
    {
        req_start--;
    }
    uint32_t req_end = tile1;
    if(req_end < max_tiles)
    {
        req_end++;
    }
    const uint32_t req_count = req_end - req_start;

    (void)waveform_cache_request_tiles(handle,
                                       level,
                                       req_start,
                                       req_count,
                                       WAVEFORM_CACHE_REASON_EDITOR_VISIBLE);
    if(out_tile_start != 0)
    {
        *out_tile_start = tile0;
    }
    if(out_tile_count != 0)
    {
        *out_tile_count = tile1 - tile0;
    }
    return waveform_cache_tiles_ready(handle, level, tile0, tile1 - tile0);
}

static uint8_t ui_page_audio_rec_draw_wavecache(int x,
                                                int y,
                                                int w,
                                                int h,
                                                uint32_t view_start_frame,
                                                uint32_t view_frames,
                                                uint32_t frames_per_pixel,
                                                uint16_t vertical_ref,
                                                uint32_t *out_frames_per_column)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 0) || (inner_h <= 2) || (vertical_ref == 0U))
    {
        return 0U;
    }

    waveform_cache_handle_t handle;
    if(sample_capture_model_waveform_cache_get_handle(&handle) == 0U)
    {
        return 0U;
    }

    waveform_cache_level_id_t level = WAVEFORM_CACHE_LEVEL_L3_FINE;
    if(waveform_cache_choose_level(frames_per_pixel, &level) == 0U)
    {
        return 0U;
    }
    uint32_t visible_tile_start = 0U;
    uint32_t visible_tile_count = 0U;
    if(ui_page_audio_rec_wavecache_request_visible(&handle,
                                                   level,
                                                   view_start_frame,
                                                   view_frames,
                                                   (uint16_t)inner_w,
                                                   &visible_tile_start,
                                                   &visible_tile_count) == 0U)
    {
        for(int fallback = (int)level - 1; fallback >= 0; --fallback)
        {
            waveform_cache_level_id_t fb_level = (waveform_cache_level_id_t)fallback;
            uint32_t fb_tile_start = 0U;
            uint32_t fb_tile_count = 0U;
            if(ui_page_audio_rec_wavecache_request_visible(&handle,
                                                           fb_level,
                                                           view_start_frame,
                                                           view_frames,
                                                           (uint16_t)inner_w,
                                                           &fb_tile_start,
                                                           &fb_tile_count) != 0U)
            {
                level = fb_level;
                visible_tile_start = fb_tile_start;
                visible_tile_count = fb_tile_count;
                break;
            }
        }
        if(waveform_cache_tiles_ready(&handle, level, visible_tile_start, visible_tile_count) == 0U)
        {
            return 0U;
        }
    }

    uint32_t frames_per_column = 0U;
    if((waveform_cache_level_frames_per_column(level, &frames_per_column) == 0U)
            || (frames_per_column == 0U))
    {
        return 0U;
    }
    if(out_frames_per_column != 0)
    {
        *out_frames_per_column = frames_per_column;
    }

    for(int col = 0; col < inner_w; ++col)
    {
        uint32_t frame0 = view_start_frame
            + (uint32_t)(((uint64_t)col * (uint64_t)view_frames) / (uint64_t)inner_w);
        uint32_t frame1 = view_start_frame
            + (uint32_t)(((uint64_t)(col + 1) * (uint64_t)view_frames) / (uint64_t)inner_w);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }
        uint32_t column0 = frame0 / frames_per_column;
        uint32_t column1 = (frame1 + frames_per_column - 1U) / frames_per_column;
        if(column1 <= column0)
        {
            column1 = column0 + 1U;
        }
        int16_t min_v = 0;
        int16_t max_v = 0;
        if(waveform_cache_minmax_from_ram(&handle,
                                          level,
                                          column0,
                                          column1 - column0,
                                          &min_v,
                                          &max_v) == 0U)
        {
            return 0U;
        }
        ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                             inner_y,
                                             inner_h,
                                             cy,
                                             min_v,
                                             max_v,
                                             vertical_ref);
    }
    return 1U;
}

static void ui_page_audio_rec_draw_marker_bar(const sample_capture_state_t *state)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t waveform_draw_start_ms = HAL_GetTick();
#endif
    const int x = 0;
    const int y = 21;
    const int w = 128;
    const int h = 31;
    drv_display_clear_rect(x, y, w, h);
    drv_display_draw_rect(x, y, w, h);
    ui_page_audio_rec_draw_zero_line(x, y, w, h);
    if((state->waveform_count == 0U)
            && !((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
                && (state->take_valid != 0U)
                && (state->recorded_frames != 0U)))
    {
        if((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT) && (state->take_valid != 0U))
        {
            drv_display_draw_text(52U, 34U, "BUILD");
        }
        return;
    }

    uint32_t view_start_frame = 0U;
    uint32_t view_frames = state->recorded_frames;
    if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        if(view_frames == 0U)
        {
            view_frames = 1U;
        }
        view_frames =
            sample_capture_model_visible_frames_for_zoom(state->recorded_frames, state->edit_zoom);
        if(view_frames >= state->recorded_frames)
        {
            view_start_frame = 0U;
            view_frames = state->recorded_frames;
        }
        else
        {
            view_start_frame = state->edit_scroll_frame;
            if(view_start_frame > (state->recorded_frames - view_frames))
            {
                view_start_frame = state->recorded_frames - view_frames;
            }
        }
    }
    if(view_frames == 0U)
    {
        view_frames = 1U;
    }

    uint8_t waveform_drawn = 0U;
    sample_capture_renderer_debug_t renderer = SAMPLE_CAPTURE_RENDERER_EMPTY;
    uint16_t draw_line_segments = 0U;
    uint32_t wavecache_frames_per_column = 0U;
    uint8_t fallback_reason = 0U;
    if((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
            && (state->take_valid != 0U)
            && (state->recorded_frames != 0U))
    {
        const uint16_t line_columns = (uint16_t)((w > 2) ? (w - 2) : 0);
        uint32_t samples_per_pixel = view_frames;
        if(line_columns != 0U)
        {
            samples_per_pixel = (view_frames + (uint32_t)line_columns - 1U)
                / (uint32_t)line_columns;
        }
        const uint8_t prefer_old_audio_tile =
            (uint8_t)((samples_per_pixel < 128U)
                && (sample_capture_model_view_uses_tile_cache(view_frames) != 0U));

        sample_capture_state_t draw_state = *state;
        if(prefer_old_audio_tile != 0U)
        {
            sample_capture_model_request_line_waveform(view_start_frame,
                                                       view_frames,
                                                       line_columns);
            sample_capture_model_get_state(&draw_state);
        }

        if((prefer_old_audio_tile != 0U)
                && (draw_state.line_valid != 0U)
                && (draw_state.line_start_frame == view_start_frame)
                && (draw_state.line_frames == view_frames))
        {
            ui_page_audio_rec_draw_line_waveform(&draw_state, x, y, w, h,
                                                 ui_page_audio_rec_edit_vertical_ref(&draw_state),
                                                 &draw_line_segments);
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_OLD_AUDIO_TILE;
        }

        if((waveform_drawn == 0U)
                && ui_page_audio_rec_draw_wavecache(x,
                                                    y,
                                                    w,
                                                    h,
                                                    view_start_frame,
                                                    view_frames,
                                                    samples_per_pixel,
                                                    ui_page_audio_rec_edit_vertical_ref(state),
                                                    &wavecache_frames_per_column) != 0U)
        {
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_BRKWAVE_TILE;
        }

        if((waveform_drawn == 0U)
                && (prefer_old_audio_tile != 0U)
                && (draw_state.line_valid != 0U)
                && (((draw_state.line_start_frame <= view_start_frame)
                     && ((draw_state.line_start_frame + draw_state.line_frames)
                         >= (view_start_frame + view_frames)))
                    || ((draw_state.line_frames == view_frames)
                        && (((draw_state.line_start_frame > view_start_frame)
                                ? (draw_state.line_start_frame - view_start_frame)
                                : (view_start_frame - draw_state.line_start_frame))
                            <= (view_frames / 4U)))))
        {
            ui_page_audio_rec_draw_line_waveform(&draw_state, x, y, w, h,
                                                 ui_page_audio_rec_edit_vertical_ref(&draw_state),
                                                 &draw_line_segments);
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_OLD_LINE;
            fallback_reason = 2U;
        }

        if(waveform_drawn == 0U
                && ui_page_audio_rec_draw_global_overview(x, y, w, h,
                                                          view_start_frame,
                                                          view_frames,
                                                          ui_page_audio_rec_edit_vertical_ref(state)) != 0U)
        {
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_GLOBAL_OVERVIEW;
            fallback_reason = (prefer_old_audio_tile != 0U) ? 3U : 1U;
        }
    }

    if(waveform_drawn == 0U)
    {
        const ui_audio_rec_wave_vertical_mode_t vertical_mode =
            (state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
                ? UI_AUDIO_REC_WAVE_VERTICAL_FIXED_FULL_SCALE
                : UI_AUDIO_REC_WAVE_VERTICAL_FIXED_FULL_SCALE;
        const uint16_t vertical_ref =
            (state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
                ? ui_page_audio_rec_edit_vertical_ref(state)
                : 0U;

        ui_page_audio_rec_draw_waveform_range(state, x, y, w, h,
                                              view_start_frame, view_frames,
                                              vertical_mode,
                                              vertical_ref);
        if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
        {
            renderer = SAMPLE_CAPTURE_RENDERER_EMPTY;
            fallback_reason = 4U;
        }
    }

    if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
        const uint32_t waveform_ms = HAL_GetTick() - waveform_draw_start_ms;
        g_ui_page_audio_rec_last_waveform_ms = waveform_ms;
#else
        const uint32_t waveform_ms = 0U;
#endif
        if(state->error != SAMPLE_CAPTURE_ERROR_NONE)
        {
            renderer = SAMPLE_CAPTURE_RENDERER_ERROR;
        }
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
        const uint16_t inner_w_dbg = (uint16_t)((w > 2) ? (w - 2) : 0);
        uint32_t samples_per_pixel = view_frames;
        if(inner_w_dbg != 0U)
        {
            samples_per_pixel = (view_frames + (uint32_t)inner_w_dbg - 1U) / (uint32_t)inner_w_dbg;
        }
        sample_capture_model_debug_note_renderer(renderer,
                                                 state->edit_zoom,
                                                 view_start_frame,
                                                 view_frames,
                                                 inner_w_dbg,
                                                 samples_per_pixel,
                                                 wavecache_frames_per_column,
                                                 state->line_valid,
                                                 state->line_count,
                                                 draw_line_segments,
                                                 fallback_reason);
#endif
        (void)waveform_ms;
#if !(SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS)
        (void)renderer;
        (void)fallback_reason;
#endif
    }

    if((state->take_valid != 0U) && (state->recorded_frames != 0U))
    {
        if((state->edit_start_frame >= view_start_frame)
                && (state->edit_start_frame <= (view_start_frame + view_frames)))
        {
            int start_x = x + 1 + (int)(((uint64_t)(state->edit_start_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            drv_display_draw_line(start_x, y, start_x, y + h - 1);
        }
        if((state->edit_end_frame >= view_start_frame)
                && (state->edit_end_frame <= (view_start_frame + view_frames)))
        {
            int end_x = x + 1 + (int)(((uint64_t)(state->edit_end_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            if(end_x >= x + w)
            {
                end_x = x + w - 1;
            }
            drv_display_draw_line(end_x, y, end_x, y + h - 1);
        }
    }
}

static void ui_page_audio_rec_render(void)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t page_draw_start_ms = HAL_GetTick();
#endif
    sample_capture_state_t state;
    char line[32];
    sample_capture_model_get_state(&state);

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "AUDIO REC");
    drv_display_draw_text(80U, 0U, ui_page_audio_rec_phase_label(state.phase));

    drv_display_set_font(&FONT_4X6);
    (void)snprintf(line, sizeof(line), "ARM %s", (state.arm == SAMPLE_CAPTURE_ARM_REC) ? "REC" : "OFF");
    drv_display_draw_text(0U, 13U, line);
    if(state.len_bars == 0U)
    {
        (void)snprintf(line, sizeof(line), "LEN FREE");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "LEN %u", (unsigned)state.len_bars);
    }
    drv_display_draw_text(43U, 13U, line);
    (void)snprintf(line, sizeof(line), "Q %s", ui_page_audio_rec_quant_label(state.quant));
    drv_display_draw_text(86U, 13U, line);
    ui_page_audio_rec_draw_marker_bar(&state);

    if(state.error != SAMPLE_CAPTURE_ERROR_NONE)
    {
        drv_display_draw_text(46U, 34U, ui_page_audio_rec_error_label(state.error));
    }
    drv_display_draw_text(0U, 54U, "RETURN");
    drv_display_draw_text(36U, 54U, "ARM");
    drv_display_draw_text(60U, 54U, "LEN");
    drv_display_draw_text(84U, 54U, "QUANT");
    drv_display_draw_text(120U, 54U, "-");
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        sample_capture_model_debug_note_draw_cost(HAL_GetTick() - page_draw_start_ms,
                                                  g_ui_page_audio_rec_last_waveform_ms);
    }
#endif
}

static void ui_page_rec_edit_render(void)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t page_draw_start_ms = HAL_GetTick();
#endif
    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    sample_capture_model_note_rec_edit_first_render();

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "REC EDIT");
    drv_display_draw_text(78U, 0U, ui_page_audio_rec_phase_label(state.phase));

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 13U, "ZOOM");
    drv_display_draw_text(30U, 13U, "SCROLL");
    drv_display_draw_text(72U, 13U, "START");
    drv_display_draw_text(108U, 13U, "END");

    ui_page_audio_rec_draw_marker_bar(&state);
    if(state.error != SAMPLE_CAPTURE_ERROR_NONE)
    {
        drv_display_draw_text(46U, 34U, ui_page_audio_rec_error_label(state.error));
    }
    drv_display_draw_text(0U, 54U, "RETURN");
    drv_display_draw_text(34U, 54U, "SAVE");
    drv_display_draw_text(62U, 54U, "ASSIGN");
    drv_display_draw_text(120U, 54U, "-");
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    sample_capture_model_debug_note_draw_cost(HAL_GetTick() - page_draw_start_ms,
                                              g_ui_page_audio_rec_last_waveform_ms);
#endif
}

uint8_t ui_page_audio_rec_handle_encoder(uint8_t encoder, int16_t delta)
{
    if(delta == 0)
    {
        return 1U;
    }

    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        return sample_capture_model_step_edit(encoder, delta);
    }

    switch(encoder)
    {
        case 0U:
            return sample_capture_model_step_arm(delta);
        case 1U:
            return sample_capture_model_step_len(delta);
        case 2U:
            return sample_capture_model_step_quant(delta);
        default:
            return 1U;
    }
}

uint8_t ui_page_audio_rec_is_open(void)
{
    const uint8_t page = ui_page_get_id();
    return (uint8_t)((page == UI_PAGE_AUDIO_REC) || (page == UI_PAGE_REC_EDIT));
}

const ui_page_t g_ui_page_audio_rec = {
    .enter = ui_page_audio_rec_enter,
    .leave = 0,
    .handle_event = ui_page_audio_rec_handle_event,
    .tick = ui_page_audio_rec_tick,
    .sync_active_context = 0,
    .render = ui_page_audio_rec_render,
    .context = 0,
};

const ui_page_t g_ui_page_rec_edit = {
    .enter = ui_page_rec_edit_enter,
    .leave = 0,
    .handle_event = ui_page_rec_edit_handle_event,
    .tick = ui_page_audio_rec_tick,
    .sync_active_context = 0,
    .render = ui_page_rec_edit_render,
    .context = 0,
};
