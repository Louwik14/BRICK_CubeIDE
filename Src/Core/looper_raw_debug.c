#include "Core/looper_raw_debug.h"

#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <string.h>

static looper_raw_debug_snapshot_t g_looper_raw_debug;
static uint32_t g_looper_raw_debug_take_generation;
static uint32_t g_looper_raw_debug_uart_play_take_printed;
static uint32_t g_looper_raw_debug_uart_wrap_print_count;
static uint32_t g_looper_raw_debug_uart_preroll_used_printed;
static uint32_t g_looper_raw_debug_uart_preroll_underrun_printed;
static uint32_t g_looper_raw_debug_uart_preroll_reused_after_wrap_printed;
static uint32_t g_looper_raw_debug_uart_raw_relay_done_printed;

static int64_t looper_raw_debug_delta(uint64_t a, uint64_t b)
{
    return (a >= b) ? (int64_t)(a - b) : -(int64_t)(b - a);
}

static void looper_raw_debug_u64_to_dec(uint64_t value, char *out, uint32_t out_len)
{
    char tmp[21];
    uint32_t count = 0U;

    if((out == 0) || (out_len == 0U))
    {
        return;
    }

    if(value == 0U)
    {
        if(out_len > 1U)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while((value != 0U) && (count < (uint32_t)sizeof(tmp)))
    {
        tmp[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    }

    uint32_t dst = 0U;
    while((count > 0U) && ((dst + 1U) < out_len))
    {
        out[dst++] = tmp[--count];
    }
    out[dst] = '\0';
}

static void looper_raw_debug_s64_to_dec(int64_t value, char *out, uint32_t out_len)
{
    if((out == 0) || (out_len == 0U))
    {
        return;
    }

    if(value < 0)
    {
        out[0] = '-';
        if(out_len > 1U)
        {
            const uint64_t magnitude = (uint64_t)(-(value + 1)) + 1U;
            looper_raw_debug_u64_to_dec(magnitude, &out[1], out_len - 1U);
        }
        return;
    }

    looper_raw_debug_u64_to_dec((uint64_t)value, out, out_len);
}

static const char *looper_raw_debug_u64_field(uint64_t value,
                                              uint8_t valid,
                                              char *buf,
                                              uint32_t buf_len)
{
    if(valid == 0U)
    {
        return "NA";
    }

    looper_raw_debug_u64_to_dec(value, buf, buf_len);
    return buf;
}

static const char *looper_raw_debug_s64_field(int64_t value,
                                              uint8_t valid,
                                              char *buf,
                                              uint32_t buf_len)
{
    if(valid == 0U)
    {
        return "NA";
    }

    looper_raw_debug_s64_to_dec(value, buf, buf_len);
    return buf;
}

static void looper_raw_debug_print_u32_or_na(uint32_t value, uint8_t valid)
{
    if(valid == 0U)
    {
        printf("NA");
        return;
    }

    printf("%lu", (unsigned long)value);
}

static const char *looper_raw_debug_source_name(uint8_t source)
{
    switch(source)
    {
        case LOOPER_RAW_DEBUG_SOURCE_PREROLL_RAM:
            return "PREROLL_RAM";
        case LOOPER_RAW_DEBUG_SOURCE_RAW_PAGE_CACHE:
            return "RAW_PAGE_CACHE";
        case LOOPER_RAW_DEBUG_SOURCE_RAW_PAGE_MISS:
            return "RAW_PAGE_MISS";
        default:
            return "NONE";
    }
}

static void looper_raw_debug_print_play_start(const looper_raw_debug_play_t *play, uint8_t valid)
{
    char abs_buf[24];
    char delta_buf[24];

    printf("LDBG PLAY_START take=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->take_id : 0U, valid);
    printf(" tr=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->logical_track : 0U, valid);
    printf(" slot=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->raw_slot : 0U, valid);
    printf(" abs=%s bid=",
           looper_raw_debug_u64_field((play != 0) ? play->playback_start_sample_abs : 0U,
                                      valid,
                                      abs_buf,
                                      sizeof(abs_buf)));
    looper_raw_debug_print_u32_or_na((play != 0) ? play->playback_start_boundary_id : 0U, valid);
    printf(" delta=%s playhead=",
           looper_raw_debug_s64_field((play != 0) ? play->delta_start_to_boundary : 0,
                                      valid,
                                      delta_buf,
                                      sizeof(delta_buf)));
    looper_raw_debug_print_u32_or_na((play != 0) ? play->start_playhead : 0U, valid);
    printf(" frames=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frames_total : 0U, valid);
    printf(" source=");
    printf("%s", (valid != 0U && play != 0) ? looper_raw_debug_source_name(play->playback_start_source) : "NA");
    printf(" hit=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->playback_start_cache_hit : 0U, valid);
    printf("\n");
}

static void looper_raw_debug_print_wrap(const looper_raw_debug_play_t *play, uint8_t valid)
{
    char abs_buf[24];
    char delta_buf[24];

    printf("LDBG WRAP take=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->take_id : 0U, valid);
    printf(" tr=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->logical_track : 0U, valid);
    printf(" slot=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->raw_slot : 0U, valid);
    printf(" wrap=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->wrap_count : 0U, valid);
    printf(" mod4=");
    looper_raw_debug_print_u32_or_na((play != 0) ? (play->wrap_count & 3U) : 0U, valid);
    printf(" abs=%s before=",
           looper_raw_debug_u64_field((play != 0) ? play->sample_abs_at_wrap : 0U,
                                      valid,
                                      abs_buf,
                                      sizeof(abs_buf)));
    looper_raw_debug_print_u32_or_na((play != 0) ? play->playhead_before_wrap : 0U, valid);
    printf(" after=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->playhead_after_wrap : 0U, valid);
    printf(" src=");
    printf("%s", (valid != 0U && play != 0) ? looper_raw_debug_source_name(play->source_before_wrap) : "NA");
    printf("->");
    printf("%s", (valid != 0U && play != 0) ? looper_raw_debug_source_name(play->source_after_wrap) : "NA");
    printf(" frame=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frame_read_before_wrap : 0U, valid);
    printf("->");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frame_read_after_wrap : 0U, valid);
    printf(" page=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->page_index_before_wrap : 0U, valid);
    printf("->");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->page_index_after_wrap : 0U, valid);
    printf(" hit=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->cache_hit_before_wrap : 0U, valid);
    printf("->");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->cache_hit_after_wrap : 0U, valid);
    printf(" frac=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frac_before_wrap : 0U, valid);
    printf("->");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frac_after_wrap : 0U, valid);
    printf(" interp=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->interpolation_active : 0U, valid);
    printf(" fade=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->gain_fade_active : 0U, valid);
    printf(" declick=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->declick_active : 0U, valid);
    printf(" bid=");
    looper_raw_debug_print_u32_or_na((play != 0) ? play->nearest_boundary_id : 0U, valid);
    printf(" delta=%s frames=",
           looper_raw_debug_s64_field((play != 0) ? play->delta_wrap_vs_boundary : 0,
                                      valid,
                                      delta_buf,
                                      sizeof(delta_buf)));
    looper_raw_debug_print_u32_or_na((play != 0) ? play->frames_total : 0U, valid);
    printf("\n");
}

static void looper_raw_debug_push_event(looper_raw_debug_event_type_t type,
                                        uint8_t logical_track,
                                        uint8_t raw_slot,
                                        uint64_t sample_abs,
                                        uint32_t value0,
                                        uint32_t value1,
                                        uint8_t writer_state)
{
    looper_raw_debug_event_t *const event =
        &g_looper_raw_debug.events[g_looper_raw_debug.event_write_index % LOOPER_RAW_DEBUG_RING_CAP];

    *event = (looper_raw_debug_event_t){
        .type = type,
        .sequence = ++g_looper_raw_debug.event_sequence,
        .boundary_id = g_looper_raw_debug.last_boundary.boundary_id,
        .take_id = g_looper_raw_debug.last_rec.take_id,
        .sample_abs = sample_abs,
        .value0 = value0,
        .value1 = value1,
        .logical_track = logical_track,
        .raw_slot = raw_slot,
        .writer_state = writer_state
    };
    g_looper_raw_debug.event_write_index++;
}

void looper_raw_debug_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    memset(&g_looper_raw_debug, 0, sizeof(g_looper_raw_debug));
    g_looper_raw_debug_take_generation = 0U;
    g_looper_raw_debug_uart_play_take_printed = 0U;
    g_looper_raw_debug_uart_wrap_print_count = 0U;
    g_looper_raw_debug_uart_preroll_used_printed = 0U;
    g_looper_raw_debug_uart_preroll_underrun_printed = 0U;
    g_looper_raw_debug_uart_preroll_reused_after_wrap_printed = 0U;
    g_looper_raw_debug_uart_raw_relay_done_printed = 0U;
    __set_PRIMASK(primask);
}

void looper_raw_debug_note_boundary(uint8_t logical_track,
                                    uint8_t step,
                                    uint64_t audio_sample_abs,
                                    uint16_t sample_offset_in_block,
                                    uint32_t samples_per_step_q16)
{
    g_looper_raw_debug.last_boundary.boundary_id++;
    g_looper_raw_debug.last_boundary.logical_track = logical_track;
    g_looper_raw_debug.last_boundary.step = step;
    g_looper_raw_debug.last_boundary.audio_sample_abs = audio_sample_abs;
    g_looper_raw_debug.last_boundary.sample_offset_in_block = sample_offset_in_block;
    g_looper_raw_debug.last_boundary.samples_per_step_q16 = samples_per_step_q16;

    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_BOUNDARY,
                                logical_track,
                                0xFFU,
                                audio_sample_abs,
                                step,
                                sample_offset_in_block,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_rec_start(uint8_t logical_track,
                                     uint8_t raw_slot,
                                     uint8_t len_mode,
                                     uint64_t rec_request_sample_abs,
                                     uint64_t rec_actual_start_sample_abs,
                                     uint32_t expected_frames)
{
    g_looper_raw_debug_take_generation++;
    g_looper_raw_debug.last_rec = (looper_raw_debug_rec_t){
        .take_id = g_looper_raw_debug_take_generation,
        .rec_request_sample_abs = rec_request_sample_abs,
        .rec_actual_start_sample_abs = rec_actual_start_sample_abs,
        .rec_start_boundary_sample_abs = g_looper_raw_debug.last_boundary.audio_sample_abs,
        .delta_start_to_boundary =
            looper_raw_debug_delta(rec_actual_start_sample_abs,
                                   g_looper_raw_debug.last_boundary.audio_sample_abs),
        .rec_start_boundary_id = g_looper_raw_debug.last_boundary.boundary_id,
        .expected_frames = expected_frames,
        .logical_track = logical_track,
        .raw_slot = raw_slot,
        .len_mode = len_mode,
        .writer_state = g_looper_raw_debug.last_writer_state
    };
    g_looper_raw_debug.rec_start_count++;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_REC_START,
                                logical_track,
                                raw_slot,
                                rec_actual_start_sample_abs,
                                expected_frames,
                                len_mode,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_rec_stop(uint8_t logical_track,
                                    uint8_t raw_slot,
                                    uint64_t rec_request_sample_abs,
                                    uint64_t rec_actual_stop_sample_abs)
{
    g_looper_raw_debug.last_rec.rec_request_sample_abs = rec_request_sample_abs;
    g_looper_raw_debug.last_rec.rec_actual_stop_sample_abs = rec_actual_stop_sample_abs;
    g_looper_raw_debug.last_rec.rec_stop_boundary_sample_abs =
        g_looper_raw_debug.last_boundary.audio_sample_abs;
    g_looper_raw_debug.last_rec.rec_stop_boundary_id =
        g_looper_raw_debug.last_boundary.boundary_id;
    g_looper_raw_debug.last_rec.delta_stop_to_boundary =
        looper_raw_debug_delta(rec_actual_stop_sample_abs,
                               g_looper_raw_debug.last_boundary.audio_sample_abs);
    g_looper_raw_debug.last_rec.boundary_span_frames =
        (g_looper_raw_debug.last_rec.rec_stop_boundary_sample_abs
         >= g_looper_raw_debug.last_rec.rec_start_boundary_sample_abs)
        ? (g_looper_raw_debug.last_rec.rec_stop_boundary_sample_abs
           - g_looper_raw_debug.last_rec.rec_start_boundary_sample_abs)
        : 0U;
    g_looper_raw_debug.rec_stop_count++;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_REC_STOP,
                                logical_track,
                                raw_slot,
                                rec_actual_stop_sample_abs,
                                g_looper_raw_debug.last_rec.rec_stop_boundary_id,
                                0U,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_rec_final(uint8_t logical_track,
                                     uint8_t raw_slot,
                                     uint32_t recorded_frames,
                                     uint8_t writer_state)
{
    g_looper_raw_debug.last_rec.recorded_frames = recorded_frames;
    g_looper_raw_debug.last_rec.writer_state = writer_state;
    g_looper_raw_debug.last_writer_state = writer_state;
    g_looper_raw_debug.last_rec.delta_recorded_vs_boundary_span =
        (int64_t)recorded_frames - (int64_t)g_looper_raw_debug.last_rec.boundary_span_frames;
    const uint8_t preserve_playback =
        (uint8_t)((g_looper_raw_debug.playback_watch_take_id == g_looper_raw_debug.last_rec.take_id)
            && ((g_looper_raw_debug.play_start_seen != 0U)
                || (g_looper_raw_debug.wrap_capture_count != 0U)));
    if(preserve_playback == 0U)
    {
        memset(&g_looper_raw_debug.last_play, 0, sizeof(g_looper_raw_debug.last_play));
        memset(g_looper_raw_debug.first_wraps, 0, sizeof(g_looper_raw_debug.first_wraps));
        g_looper_raw_debug.wrap_capture_count = 0U;
        g_looper_raw_debug.play_start_seen = 0U;
        g_looper_raw_debug_uart_play_take_printed = 0U;
        g_looper_raw_debug_uart_wrap_print_count = 0U;
        g_looper_raw_debug_uart_preroll_used_printed = g_looper_raw_debug.preroll_used_count;
        g_looper_raw_debug_uart_preroll_underrun_printed = g_looper_raw_debug.preroll_underrun_count;
        g_looper_raw_debug_uart_preroll_reused_after_wrap_printed =
            g_looper_raw_debug.preroll_reused_after_wrap_count;
        g_looper_raw_debug_uart_raw_relay_done_printed =
            g_looper_raw_debug.raw_relay_done_count;
    }
    g_looper_raw_debug.playback_watch_take_id = g_looper_raw_debug.last_rec.take_id;
    g_looper_raw_debug.playback_watch_active = 1U;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_REC_FINAL,
                                logical_track,
                                raw_slot,
                                g_looper_raw_debug.last_rec.rec_actual_stop_sample_abs,
                                recorded_frames,
                                (uint32_t)g_looper_raw_debug.last_rec.boundary_span_frames,
                                writer_state);
}

void looper_raw_debug_note_play_start(uint8_t logical_track,
                                      uint8_t raw_slot,
                                      uint32_t frames_total,
                                      uint64_t playback_start_sample_abs,
                                      uint32_t start_playhead,
                                      uint8_t source,
                                      uint8_t cache_hit)
{
    g_looper_raw_debug.last_play.take_id = g_looper_raw_debug.last_rec.take_id;
    g_looper_raw_debug.last_play.logical_track = logical_track;
    g_looper_raw_debug.last_play.raw_slot = raw_slot;
    g_looper_raw_debug.last_play.frames_total = frames_total;
    g_looper_raw_debug.last_play.playback_start_sample_abs = playback_start_sample_abs;
    g_looper_raw_debug.last_play.playback_start_boundary_id =
        g_looper_raw_debug.last_boundary.boundary_id;
    g_looper_raw_debug.last_play.playback_start_boundary_sample_abs =
        g_looper_raw_debug.last_boundary.audio_sample_abs;
    g_looper_raw_debug.last_play.delta_start_to_boundary =
        looper_raw_debug_delta(playback_start_sample_abs,
                               g_looper_raw_debug.last_boundary.audio_sample_abs);
    g_looper_raw_debug.last_play.start_playhead = start_playhead;
    g_looper_raw_debug.last_play.playback_start_source = source;
    g_looper_raw_debug.last_play.playback_start_cache_hit = cache_hit;
    g_looper_raw_debug.last_play.wrap_count = 0U;
    g_looper_raw_debug.playback_start_count++;
    if((g_looper_raw_debug.playback_watch_active != 0U)
            && (g_looper_raw_debug.playback_watch_take_id == g_looper_raw_debug.last_rec.take_id))
    {
        g_looper_raw_debug.play_start_seen = 1U;
    }
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_PLAY_START,
                                logical_track,
                                raw_slot,
                                playback_start_sample_abs,
                                frames_total,
                                start_playhead,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_wrap(uint8_t logical_track,
                                uint8_t raw_slot,
                                uint32_t frames_total,
                                uint64_t sample_abs_at_wrap,
                                uint32_t playhead_before_wrap,
                                uint32_t playhead_after_wrap)
{
    looper_raw_debug_note_wrap_ex(logical_track,
                                  raw_slot,
                                  frames_total,
                                  sample_abs_at_wrap,
                                  playhead_before_wrap,
                                  playhead_after_wrap,
                                  LOOPER_RAW_DEBUG_SOURCE_NONE,
                                  LOOPER_RAW_DEBUG_SOURCE_NONE,
                                  (frames_total != 0U) ? (frames_total - 1U) : 0U,
                                  playhead_after_wrap,
                                  0U,
                                  0U,
                                  0U,
                                  0U,
                                  0U,
                                  0U,
                                  0U,
                                  0U,
                                  0U);
}

void looper_raw_debug_note_wrap_ex(uint8_t logical_track,
                                   uint8_t raw_slot,
                                   uint32_t frames_total,
                                   uint64_t sample_abs_at_wrap,
                                   uint32_t playhead_before_wrap,
                                   uint32_t playhead_after_wrap,
                                   uint8_t source_before_wrap,
                                   uint8_t source_after_wrap,
                                   uint32_t frame_read_before_wrap,
                                   uint32_t frame_read_after_wrap,
                                   uint32_t page_index_before_wrap,
                                   uint32_t page_index_after_wrap,
                                   uint8_t cache_hit_before_wrap,
                                   uint8_t cache_hit_after_wrap,
                                   uint16_t frac_before_wrap,
                                   uint16_t frac_after_wrap,
                                   uint8_t interpolation_active,
                                   uint8_t gain_fade_active,
                                   uint8_t declick_active)
{
    g_looper_raw_debug.last_play.take_id = g_looper_raw_debug.last_rec.take_id;
    g_looper_raw_debug.last_play.logical_track = logical_track;
    g_looper_raw_debug.last_play.raw_slot = raw_slot;
    g_looper_raw_debug.last_play.frames_total = frames_total;
    g_looper_raw_debug.last_play.sample_abs_at_wrap = sample_abs_at_wrap;
    g_looper_raw_debug.last_play.playhead_before_wrap = playhead_before_wrap;
    g_looper_raw_debug.last_play.playhead_after_wrap = playhead_after_wrap;
    g_looper_raw_debug.last_play.frame_read_before_wrap = frame_read_before_wrap;
    g_looper_raw_debug.last_play.frame_read_after_wrap = frame_read_after_wrap;
    g_looper_raw_debug.last_play.page_index_before_wrap = page_index_before_wrap;
    g_looper_raw_debug.last_play.page_index_after_wrap = page_index_after_wrap;
    g_looper_raw_debug.last_play.source_before_wrap = source_before_wrap;
    g_looper_raw_debug.last_play.source_after_wrap = source_after_wrap;
    g_looper_raw_debug.last_play.cache_hit_before_wrap = cache_hit_before_wrap;
    g_looper_raw_debug.last_play.cache_hit_after_wrap = cache_hit_after_wrap;
    g_looper_raw_debug.last_play.frac_before_wrap = frac_before_wrap;
    g_looper_raw_debug.last_play.frac_after_wrap = frac_after_wrap;
    g_looper_raw_debug.last_play.interpolation_active = interpolation_active;
    g_looper_raw_debug.last_play.gain_fade_active = gain_fade_active;
    g_looper_raw_debug.last_play.declick_active = declick_active;
    g_looper_raw_debug.last_play.nearest_boundary_id = g_looper_raw_debug.last_boundary.boundary_id;
    const uint64_t expected_wrap_sample =
        g_looper_raw_debug.last_play.playback_start_sample_abs
        + ((uint64_t)(g_looper_raw_debug.last_play.wrap_count + 1U) * (uint64_t)frames_total);
    g_looper_raw_debug.last_play.delta_wrap_vs_boundary =
        (g_looper_raw_debug.last_play.playback_start_sample_abs != 0U)
            ? looper_raw_debug_delta(sample_abs_at_wrap, expected_wrap_sample)
            : looper_raw_debug_delta(sample_abs_at_wrap, g_looper_raw_debug.last_boundary.audio_sample_abs);
    g_looper_raw_debug.last_play.wrap_count++;
    g_looper_raw_debug.wrap_count++;
    if((g_looper_raw_debug.playback_watch_active != 0U)
            && (g_looper_raw_debug.playback_watch_take_id == g_looper_raw_debug.last_rec.take_id)
            && (g_looper_raw_debug.wrap_capture_count < LOOPER_RAW_DEBUG_WRAP_CAPTURE_CAP))
    {
        g_looper_raw_debug.first_wraps[g_looper_raw_debug.wrap_capture_count] =
            g_looper_raw_debug.last_play;
        g_looper_raw_debug.wrap_capture_count++;
    }
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_WRAP,
                                logical_track,
                                raw_slot,
                                sample_abs_at_wrap,
                                playhead_before_wrap,
                                playhead_after_wrap,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_cache_miss(uint8_t logical_track, uint8_t raw_slot)
{
    g_looper_raw_debug.cache_miss_count++;
    g_looper_raw_debug.last_cache_miss_track = logical_track;
    g_looper_raw_debug.last_cache_miss_raw_slot = raw_slot;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_CACHE_MISS,
                                logical_track,
                                raw_slot,
                                0U,
                                g_looper_raw_debug.cache_miss_count,
                                0U,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_preroll_used(uint8_t logical_track, uint8_t raw_slot)
{
    g_looper_raw_debug.preroll_used_count++;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_PREROLL_USED,
                                logical_track,
                                raw_slot,
                                0U,
                                g_looper_raw_debug.preroll_used_count,
                                0U,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_preroll_underrun(uint8_t logical_track, uint8_t raw_slot)
{
    g_looper_raw_debug.preroll_underrun_count++;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_PREROLL_UNDERRUN,
                                logical_track,
                                raw_slot,
                                0U,
                                g_looper_raw_debug.preroll_underrun_count,
                                0U,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_preroll_reused_after_wrap(uint8_t logical_track, uint8_t raw_slot)
{
    g_looper_raw_debug.preroll_reused_after_wrap_count++;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_PREROLL_REUSED_AFTER_WRAP,
                                logical_track,
                                raw_slot,
                                0U,
                                g_looper_raw_debug.preroll_reused_after_wrap_count,
                                0U,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_raw_relay_done(uint8_t logical_track,
                                          uint8_t raw_slot,
                                          uint32_t playhead,
                                          uint32_t page_index)
{
    g_looper_raw_debug.raw_relay_done_count++;
    g_looper_raw_debug.raw_relay_track = logical_track;
    g_looper_raw_debug.raw_relay_raw_slot = raw_slot;
    g_looper_raw_debug.raw_relay_playhead = playhead;
    g_looper_raw_debug.raw_relay_page_index = page_index;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_RAW_RELAY_DONE,
                                logical_track,
                                raw_slot,
                                0U,
                                playhead,
                                page_index,
                                g_looper_raw_debug.last_writer_state);
}

void looper_raw_debug_note_writer_state(uint8_t writer_state)
{
    g_looper_raw_debug.last_writer_state = writer_state;
    looper_raw_debug_push_event(LOOPER_RAW_DEBUG_EVENT_WRITER_STATE,
                                g_looper_raw_debug.last_rec.logical_track,
                                g_looper_raw_debug.last_rec.raw_slot,
                                0U,
                                writer_state,
                                0U,
                                writer_state);
}

void looper_raw_debug_get_snapshot(looper_raw_debug_snapshot_t *out_snapshot)
{
    if(out_snapshot == 0)
    {
        return;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *out_snapshot = g_looper_raw_debug;
    __set_PRIMASK(primask);
}

void looper_raw_debug_dump_uart(void)
{
    if(__get_IPSR() != 0U)
    {
        return;
    }

    looper_raw_debug_snapshot_t snapshot;
    looper_raw_debug_get_snapshot(&snapshot);

    const uint8_t snapshot_valid =
        (uint8_t)((snapshot.rec_start_count != 0U)
                  || (snapshot.playback_start_count != 0U)
                  || (snapshot.wrap_count != 0U)
                  || (snapshot.cache_miss_count != 0U));
    const uint8_t boundary_valid = (snapshot.last_boundary.boundary_id != 0U) ? 1U : 0U;
    const uint8_t rec_start_valid = (snapshot.rec_start_count != 0U) ? 1U : 0U;
    const uint8_t rec_stop_valid = (snapshot.rec_stop_count != 0U) ? 1U : 0U;
    const uint8_t rec_final_valid = (snapshot.last_rec.recorded_frames != 0U) ? 1U : 0U;
    const uint8_t play_start_valid = (snapshot.play_start_seen != 0U) ? 1U : 0U;
    const uint8_t wrap_valid = (snapshot.wrap_capture_count != 0U) ? 1U : 0U;

    char abs_buf[24];
    char delta_buf[24];
    char span_buf[24];
    char diff_buf[24];

    printf("LDBG SNAP valid=%u take=",
           (unsigned)snapshot_valid);
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.take_id, rec_start_valid);
    printf(" events=%lu\n", (unsigned long)snapshot.event_sequence);

    printf("LDBG BOUNDARY id=");
    looper_raw_debug_print_u32_or_na(snapshot.last_boundary.boundary_id, boundary_valid);
    printf(" step=");
    looper_raw_debug_print_u32_or_na(snapshot.last_boundary.step, boundary_valid);
    printf(" abs=%s offset=",
           looper_raw_debug_u64_field(snapshot.last_boundary.audio_sample_abs,
                                      boundary_valid,
                                      abs_buf,
                                      sizeof(abs_buf)));
    looper_raw_debug_print_u32_or_na(snapshot.last_boundary.sample_offset_in_block, boundary_valid);
    printf(" sps_q16=");
    looper_raw_debug_print_u32_or_na(snapshot.last_boundary.samples_per_step_q16, boundary_valid);
    printf("\n");

    printf("LDBG REC_START take=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.take_id, rec_start_valid);
    printf(" tr=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.logical_track, rec_start_valid);
    printf(" slot=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.raw_slot, rec_start_valid);
    printf(" abs=%s bid=",
           looper_raw_debug_u64_field(snapshot.last_rec.rec_actual_start_sample_abs,
                                      rec_start_valid,
                                      abs_buf,
                                      sizeof(abs_buf)));
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.rec_start_boundary_id, rec_start_valid);
    printf(" delta=%s\n",
           looper_raw_debug_s64_field(snapshot.last_rec.delta_start_to_boundary,
                                      rec_start_valid,
                                      delta_buf,
                                      sizeof(delta_buf)));

    printf("LDBG REC_STOP take=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.take_id, rec_stop_valid);
    printf(" tr=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.logical_track, rec_stop_valid);
    printf(" slot=");
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.raw_slot, rec_stop_valid);
    printf(" abs=%s bid=",
           looper_raw_debug_u64_field(snapshot.last_rec.rec_actual_stop_sample_abs,
                                      rec_stop_valid,
                                      abs_buf,
                                      sizeof(abs_buf)));
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.rec_stop_boundary_id, rec_stop_valid);
    printf(" delta=%s frames=",
           looper_raw_debug_s64_field(snapshot.last_rec.delta_stop_to_boundary,
                                      rec_stop_valid,
                                      delta_buf,
                                      sizeof(delta_buf)));
    looper_raw_debug_print_u32_or_na(snapshot.last_rec.recorded_frames, rec_final_valid);
    printf(" span=%s diff=%s\n",
           looper_raw_debug_u64_field(snapshot.last_rec.boundary_span_frames,
                                      rec_stop_valid,
                                      span_buf,
                                      sizeof(span_buf)),
           looper_raw_debug_s64_field(snapshot.last_rec.delta_recorded_vs_boundary_span,
                                      (uint8_t)(rec_stop_valid & rec_final_valid),
                                      diff_buf,
                                      sizeof(diff_buf)));

    if((play_start_valid == 0U) && (snapshot.playback_watch_active != 0U))
    {
        printf("LDBG PLAY_START pending take=%lu\n",
               (unsigned long)snapshot.playback_watch_take_id);
    }
    else
    {
        looper_raw_debug_print_play_start(&snapshot.last_play, play_start_valid);
    }

    if((wrap_valid == 0U) && (snapshot.playback_watch_active != 0U))
    {
        printf("LDBG WRAP pending take=%lu\n",
               (unsigned long)snapshot.playback_watch_take_id);
    }
    else
    {
        looper_raw_debug_print_wrap(&snapshot.first_wraps[0], wrap_valid);
    }

    printf("LDBG MISS count=%lu PREROLL_USED=%lu PREROLL_UNDERRUN=%lu PREROLL_REUSED_AFTER_WRAP=%lu RAW_RELAY_DONE=%lu\n",
           (unsigned long)snapshot.cache_miss_count,
           (unsigned long)snapshot.preroll_used_count,
           (unsigned long)snapshot.preroll_underrun_count,
           (unsigned long)snapshot.preroll_reused_after_wrap_count,
           (unsigned long)snapshot.raw_relay_done_count);
}

void looper_raw_debug_service_uart(void)
{
    if(__get_IPSR() != 0U)
    {
        return;
    }

    looper_raw_debug_snapshot_t snapshot;
    looper_raw_debug_get_snapshot(&snapshot);

    if(snapshot.playback_watch_active == 0U)
    {
        return;
    }

    if((snapshot.preroll_used_count != g_looper_raw_debug_uart_preroll_used_printed)
            || (snapshot.preroll_underrun_count != g_looper_raw_debug_uart_preroll_underrun_printed)
            || (snapshot.preroll_reused_after_wrap_count
                != g_looper_raw_debug_uart_preroll_reused_after_wrap_printed))
    {
        printf("LDBG PREROLL_USED=%lu PREROLL_UNDERRUN=%lu PREROLL_REUSED_AFTER_WRAP=%lu\n",
               (unsigned long)(snapshot.preroll_used_count - g_looper_raw_debug_uart_preroll_used_printed),
               (unsigned long)(snapshot.preroll_underrun_count - g_looper_raw_debug_uart_preroll_underrun_printed),
               (unsigned long)(snapshot.preroll_reused_after_wrap_count
                               - g_looper_raw_debug_uart_preroll_reused_after_wrap_printed));
        g_looper_raw_debug_uart_preroll_used_printed = snapshot.preroll_used_count;
        g_looper_raw_debug_uart_preroll_underrun_printed = snapshot.preroll_underrun_count;
        g_looper_raw_debug_uart_preroll_reused_after_wrap_printed =
            snapshot.preroll_reused_after_wrap_count;
    }

    if(snapshot.raw_relay_done_count != g_looper_raw_debug_uart_raw_relay_done_printed)
    {
        printf("LDBG RAW_RELAY_DONE tr=%lu slot=%lu playhead=%lu page=%lu source=RAW_PAGE_CACHE\n",
               (unsigned long)snapshot.raw_relay_track,
               (unsigned long)snapshot.raw_relay_raw_slot,
               (unsigned long)snapshot.raw_relay_playhead,
               (unsigned long)snapshot.raw_relay_page_index);
        g_looper_raw_debug_uart_raw_relay_done_printed = snapshot.raw_relay_done_count;
    }

    if((snapshot.play_start_seen != 0U)
            && (g_looper_raw_debug_uart_play_take_printed != snapshot.playback_watch_take_id))
    {
        looper_raw_debug_print_play_start(&snapshot.last_play, 1U);
        g_looper_raw_debug_uart_play_take_printed = snapshot.playback_watch_take_id;
    }

    while((g_looper_raw_debug_uart_wrap_print_count < snapshot.wrap_capture_count)
            && (g_looper_raw_debug_uart_wrap_print_count < LOOPER_RAW_DEBUG_WRAP_CAPTURE_CAP))
    {
        looper_raw_debug_print_wrap(
            &snapshot.first_wraps[g_looper_raw_debug_uart_wrap_print_count],
            1U);
        g_looper_raw_debug_uart_wrap_print_count++;
    }
}
