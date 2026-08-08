#include "Sampler/multi_pitch_trace.h"

#include <math.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

#if BRICK6_MULTI_PITCH_TRACE

_Static_assert(sizeof(brick6_multi_pitch_trace_header_t) == 64U,
               "Multi pitch trace header ABI changed");
_Static_assert(sizeof(brick6_multi_pitch_trace_event_t) == 176U,
               "Multi pitch trace event ABI changed");

SDRAM_RECORDER volatile brick6_multi_pitch_trace_header_t
    g_brick6_multi_pitch_trace_header;
SDRAM_RECORDER volatile brick6_multi_pitch_trace_event_t
    g_brick6_multi_pitch_trace_ring[BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT];

static uint32_t g_trace_block_sequence;
static uint32_t g_trace_block_start_cycle;
static uint32_t g_trace_block_frames;
static uint32_t g_trace_block_voice_count;
static uint32_t g_trace_block_pitched_count;
static uint32_t g_trace_block_render_order;
static uint32_t g_trace_voice_order_current;
static uint32_t g_trace_block_scratch[4];

static uint32_t trace_cycle_now(void)
{
    return DWT->CYCCNT;
}

static uint32_t trace_float_bits(float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t trace_q16(float value)
{
    if (!isfinite(value) || (value <= 0.0f))
    {
        return (value < 0.0f) ? 0U : UINT32_MAX;
    }
    const float scaled = value * 65536.0f;
    if (scaled >= 4294967295.0f)
    {
        return UINT32_MAX;
    }
    return (uint32_t)(scaled + 0.5f);
}

static uint32_t trace_page_for_position(const sample_play_plan_t *plan, float position)
{
    if ((plan == NULL) || !isfinite(position) || (position < 0.0f))
    {
        return UINT32_MAX;
    }
    return sample_audio_format_page_index_from_frame(
        sample_audio_format_or_stereo(plan->format),
        (uint32_t)position);
}

static uint32_t trace_expected_after(const sample_play_plan_t *plan,
                                     float position,
                                     uint32_t frames)
{
    if ((plan == NULL) || !isfinite(position))
    {
        return UINT32_MAX;
    }

    float result = position;
    const float step = (float)plan->step_q16 / 65536.0f;
    const uint8_t reverse = (plan->direction != 0U) ? 1U : 0U;
    if ((plan->loop_mode == SAMPLE_PLAY_LOOP_PINGPONG) || (frames > 4096U))
    {
        return UINT32_MAX;
    }

    result += (reverse != 0U) ? (-step * (float)frames) : (step * (float)frames);
    if ((plan->loop_mode == SAMPLE_PLAY_LOOP_FORWARD)
        && (plan->loop_end > plan->loop_begin))
    {
        const float length = (float)(plan->loop_end - plan->loop_begin);
        while (result >= (float)plan->loop_end)
        {
            result = (float)plan->loop_begin + (result - (float)plan->loop_end);
        }
        (void)length;
    }
    if (result < 0.0f)
    {
        return 0U;
    }
    return trace_q16(result);
}

static const float *trace_source_at(const sample_audio_segment_t *segment,
                                    uint32_t frame,
                                    uint32_t *out_bits)
{
    if ((segment == NULL) || (out_bits == NULL))
    {
        return NULL;
    }
    const float *base = NULL;
    if ((frame >= segment->source_start_frame)
        && (frame < (segment->source_start_frame + segment->source_frame_count)))
    {
        base = segment->l + ((frame - segment->source_start_frame) * segment->frame_stride);
    }
    else if ((frame >= segment->neighbor_start_frame)
             && (frame < (segment->neighbor_start_frame + segment->neighbor_frame_count))
             && (segment->neighbor_l != NULL))
    {
        base = segment->neighbor_l + ((frame - segment->neighbor_start_frame) * segment->frame_stride);
    }
    if (base != NULL)
    {
        *out_bits = trace_float_bits(*base);
    }
    return base;
}

static uint32_t trace_source_checksum(const sample_audio_segment_t *segment,
                                      uint32_t base_frame,
                                      uint32_t *out0,
                                      uint32_t *out1,
                                      uint32_t *out_neighbor,
                                      uint8_t *out_source_range,
                                      uint8_t *out_nan_inf)
{
    uint32_t checksum = 2166136261UL;
    uint32_t bits0 = 0U;
    uint32_t bits1 = 0U;
    uint32_t bits_neighbor = 0U;
    const float *source0 = trace_source_at(segment, base_frame, &bits0);
    const float *source1 = trace_source_at(segment, base_frame + 1U, &bits1);
    const float *source_prev = (base_frame > 0U)
                                   ? trace_source_at(segment, base_frame - 1U, &bits_neighbor)
                                   : NULL;
    if ((source0 == NULL) || (source1 == NULL))
    {
        *out_source_range = 1U;
    }
    if (((source0 != NULL) && !isfinite(*source0))
        || ((source1 != NULL) && !isfinite(*source1))
        || ((source_prev != NULL) && !isfinite(*source_prev)))
    {
        *out_nan_inf = 1U;
    }
    const uint32_t values[3] = {bits0, bits1, bits_neighbor};
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        checksum ^= values[i];
        checksum *= 16777619UL;
    }
    *out0 = bits0;
    *out1 = bits1;
    *out_neighbor = bits_neighbor;
    return checksum;
}

static volatile brick6_multi_pitch_trace_event_t *trace_next_event(void)
{
    brick6_multi_pitch_trace_header_t *const header =
        (brick6_multi_pitch_trace_header_t *)&g_brick6_multi_pitch_trace_header;
    const uint32_t sequence = header->write_index + 1U;
    const uint32_t slot = header->write_index % BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT;
    volatile brick6_multi_pitch_trace_event_t *const event =
        &g_brick6_multi_pitch_trace_ring[slot];
    memset((void *)event, 0, sizeof(*event));
    event->sequence = sequence;
    event->block_sequence = g_trace_block_sequence;
    header->write_index = sequence;
    if (header->valid_count < BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT)
    {
        header->valid_count++;
    }
    else
    {
        header->dropped_count++;
    }
    return event;
}

void brick6_multi_pitch_trace_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    memset((void *)&g_brick6_multi_pitch_trace_header, 0,
           sizeof(g_brick6_multi_pitch_trace_header));
    g_brick6_multi_pitch_trace_header.magic = BRICK6_MULTI_PITCH_TRACE_MAGIC;
    g_brick6_multi_pitch_trace_header.abi_version = BRICK6_MULTI_PITCH_TRACE_ABI_VERSION;
    g_brick6_multi_pitch_trace_header.header_size = sizeof(g_brick6_multi_pitch_trace_header);
    g_brick6_multi_pitch_trace_header.entry_size = sizeof(brick6_multi_pitch_trace_event_t);
    g_brick6_multi_pitch_trace_header.capacity = BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT;
    g_brick6_multi_pitch_trace_header.reset_count++;
    g_trace_block_sequence = 0U;
    g_trace_block_start_cycle = 0U;
    g_trace_block_frames = 0U;
    g_trace_block_voice_count = 0U;
    g_trace_block_pitched_count = 0U;
    g_trace_block_render_order = 0U;
    g_trace_voice_order_current = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void brick6_multi_pitch_trace_block_begin(uint32_t frames,
                                          const void *scratch0,
                                          const void *scratch1,
                                          const void *scratch2,
                                          const void *scratch3)
{
    g_trace_block_sequence++;
    g_trace_block_start_cycle = trace_cycle_now();
    g_trace_block_frames = frames;
    g_trace_block_voice_count = 0U;
    g_trace_block_pitched_count = 0U;
    g_trace_block_render_order = 0U;
    g_trace_voice_order_current = 0U;
    g_trace_block_scratch[0] = (uint32_t)(uintptr_t)scratch0;
    g_trace_block_scratch[1] = (uint32_t)(uintptr_t)scratch1;
    g_trace_block_scratch[2] = (uint32_t)(uintptr_t)scratch2;
    g_trace_block_scratch[3] = (uint32_t)(uintptr_t)scratch3;
}

void brick6_multi_pitch_trace_block_end(void)
{
    volatile brick6_multi_pitch_trace_event_t *const event = trace_next_event();
    event->event_kind = BRICK6_MULTI_PITCH_TRACE_EVENT_BLOCK;
    event->frames_rendered = g_trace_block_frames;
    event->cycles = trace_cycle_now() - g_trace_block_start_cycle;
    event->scratch0 = g_trace_block_scratch[0];
    event->scratch1 = g_trace_block_scratch[1];
    event->scratch2 = g_trace_block_scratch[2];
    event->scratch3 = g_trace_block_scratch[3];
    event->multi_voice_count = g_trace_block_voice_count;
    event->pitched_voice_count = g_trace_block_pitched_count;
    event->render_order = g_trace_block_render_order;
    g_brick6_multi_pitch_trace_header.block_count++;
    g_brick6_multi_pitch_trace_header.last_block_cycles = event->cycles;
}

uint32_t brick6_multi_pitch_trace_voice_begin(uint8_t voice_id,
                                              uint8_t owner_track,
                                              uint16_t sample_id,
                                              sample_audio_key_t key,
                                              uint32_t voice_generation,
                                              const sample_play_plan_t *plan)
{
    (void)voice_id;
    (void)owner_track;
    (void)sample_id;
    (void)key;
    (void)voice_generation;
    g_trace_block_voice_count++;
    if ((plan != NULL)
        && ((plan->kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            || (plan->kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)))
    {
        g_trace_block_pitched_count++;
    }
    g_trace_block_render_order++;
    g_trace_voice_order_current = g_trace_block_render_order;
    return trace_cycle_now();
}

void brick6_multi_pitch_trace_voice_end(uint32_t start_cycle,
                                        uint8_t voice_id,
                                        uint8_t owner_track,
                                        uint16_t sample_id,
                                        sample_audio_key_t key,
                                        uint32_t voice_generation,
                                        const void *scratch0,
                                        const void *scratch1)
{
    volatile brick6_multi_pitch_trace_event_t *const event = trace_next_event();
    event->event_kind = BRICK6_MULTI_PITCH_TRACE_EVENT_VOICE;
    event->voice_id = voice_id;
    event->owner_track = owner_track;
    event->sample_id = sample_id;
    event->key_domain = (uint32_t)key.domain;
    event->key_object_id = key.object_id;
    event->voice_generation = voice_generation;
    event->cycles = trace_cycle_now() - start_cycle;
    event->render_order = g_trace_voice_order_current;
    event->scratch0 = (uint32_t)(uintptr_t)scratch0;
    event->scratch1 = (uint32_t)(uintptr_t)scratch1;
}

void brick6_multi_pitch_trace_segment(uint8_t voice_id,
                                     uint8_t owner_track,
                                     uint16_t sample_id,
                                     sample_audio_key_t key,
                                     uint32_t voice_generation,
                                     const sample_play_plan_t *plan,
                                     const sample_audio_cursor_t *cursor_before,
                                     const sample_audio_segment_t *segment,
                                     float position_after,
                                     uint8_t direction_before,
                                     uint8_t direction_after)
{
    if ((plan == NULL) || (cursor_before == NULL) || (segment == NULL)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK)
        || ((segment->kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            && (segment->kernel_type != SAMPLE_KERNEL_PITCH_REV_LINEAR)))
    {
        return;
    }

    const float position_before = segment->source_position;
    const uint32_t before_q16 = trace_q16(position_before);
    const uint32_t after_q16 = trace_q16(position_after);
    const uint32_t base_frame = (uint32_t)position_before;
    const uint32_t expected_page = trace_page_for_position(plan, position_before);
    const uint8_t reverse = (segment->kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR) ? 1U : 0U;
    uint32_t neighbor_frame = UINT32_MAX;
    if ((reverse == 0U) && ((base_frame + 1U) < segment->source_limit_frame))
    {
        neighbor_frame = base_frame + 1U;
    }
    else if ((reverse != 0U) && (base_frame > segment->source_region_begin))
    {
        neighbor_frame = base_frame - 1U;
    }
    else if ((reverse == 0U) && (segment->source_region_begin < segment->source_limit_frame))
    {
        neighbor_frame = segment->source_region_begin;
    }
    const uint32_t expected_neighbor_page =
        (neighbor_frame == UINT32_MAX) ? UINT32_MAX :
        sample_audio_format_page_index_from_frame(sample_audio_format_or_stereo(plan->format),
                                                  neighbor_frame);
    const uint32_t actual_page = (cursor_before->current_acquired != 0U)
                                     ? cursor_before->current_page_ref.page_index : UINT32_MAX;
    uint32_t actual_neighbor_page = (cursor_before->neighbor_acquired != 0U)
                                         ? cursor_before->neighbor_page_ref.page_index
                                         : UINT32_MAX;
    if ((actual_neighbor_page == UINT32_MAX) && (expected_neighbor_page == actual_page))
    {
        actual_neighbor_page = actual_page;
    }
    const uint32_t offset = (base_frame >= cursor_before->current_start_frame)
                               ? (base_frame - cursor_before->current_start_frame) : UINT32_MAX;
    const uint32_t expected_after_page = trace_page_for_position(plan, position_after);
    uint32_t flags = 0U;
    if ((actual_page != expected_page)
        || ((expected_neighbor_page != UINT32_MAX)
            && (actual_neighbor_page != expected_neighbor_page)))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_PAGE_MISMATCH;
    }
    if ((cursor_before->current_acquired == 0U)
        || (offset >= cursor_before->current_frame_count))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_OFFSET_RANGE;
    }
    if ((cursor_before->current_acquired != 0U)
        && ((cursor_before->current_page_ref.slot_index == UINT16_MAX)
            || (cursor_before->current_page_ref.page_generation == 0U)))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_REF_INVALID;
    }
    if ((cursor_before->neighbor_acquired != 0U)
        && ((cursor_before->neighbor_page_ref.slot_index == UINT16_MAX)
            || (cursor_before->neighbor_page_ref.page_generation == 0U)))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_REF_INVALID;
    }
    if ((cursor_before->current_acquired != 0U)
        && (cursor_before->current_page_ref.registration_epoch != plan->registration_epoch))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_EPOCH_INCOHERENT;
    }
    if ((cursor_before->neighbor_acquired != 0U)
        && (cursor_before->neighbor_page_ref.registration_epoch != plan->registration_epoch))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_EPOCH_INCOHERENT;
    }
    const uint32_t expected_after_q16 = trace_expected_after(plan, position_before, segment->frames);
    if ((expected_after_q16 != UINT32_MAX) && (after_q16 != UINT32_MAX)
        && ((after_q16 > expected_after_q16 + 8U) || (expected_after_q16 > after_q16 + 8U)))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_POSITION;
    }
    if (!isfinite(position_before) || !isfinite(position_after))
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_NAN_INF;
    }
    uint32_t source0 = 0U;
    uint32_t source1 = 0U;
    uint32_t neighbor = 0U;
    uint8_t source_range = 0U;
    uint8_t nan_inf = 0U;
    const uint32_t checksum = trace_source_checksum(segment,
                                                     base_frame,
                                                     &source0,
                                                     &source1,
                                                     &neighbor,
                                                     &source_range,
                                                     &nan_inf);
    if (source_range != 0U)
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_SOURCE_RANGE;
    }
    if (nan_inf != 0U)
    {
        flags |= BRICK6_MULTI_PITCH_TRACE_ANOM_NAN_INF;
    }
    volatile brick6_multi_pitch_trace_event_t *const event = trace_next_event();
    event->event_kind = BRICK6_MULTI_PITCH_TRACE_EVENT_SEGMENT;
    event->event_flags = flags;
    event->voice_id = voice_id;
    event->owner_track = owner_track;
    event->sample_id = sample_id;
    event->key_domain = (uint32_t)key.domain;
    event->key_object_id = key.object_id;
    event->voice_generation = voice_generation;
    event->registration_epoch = plan->registration_epoch;
    event->position_before_q16 = before_q16;
    event->position_after_q16 = after_q16;
    event->step_q16 = plan->step_q16;
    event->frames_rendered = segment->frames;
    event->expected_page = expected_page;
    event->actual_page = actual_page;
    event->expected_neighbor_page = expected_neighbor_page;
    event->actual_neighbor_page = actual_neighbor_page;
    event->offset_frames = offset;
    event->current_slot = (cursor_before->current_acquired != 0U)
                              ? cursor_before->current_page_ref.slot_index : UINT32_MAX;
    event->neighbor_slot = (cursor_before->neighbor_acquired != 0U)
                              ? cursor_before->neighbor_page_ref.slot_index : UINT32_MAX;
    event->current_page_generation = (cursor_before->current_acquired != 0U)
                                         ? cursor_before->current_page_ref.page_generation : 0U;
    event->neighbor_page_generation = (cursor_before->neighbor_acquired != 0U)
                                         ? cursor_before->neighbor_page_ref.page_generation : 0U;
    event->current_epoch = (cursor_before->current_acquired != 0U)
                               ? cursor_before->current_page_ref.registration_epoch : 0U;
    event->neighbor_epoch = (cursor_before->neighbor_acquired != 0U)
                                ? cursor_before->neighbor_page_ref.registration_epoch : 0U;
    event->fraction_begin_q16 = before_q16 & 0xFFFFU;
    event->fraction_end_q16 = after_q16 & 0xFFFFU;
    event->loop_mode = plan->loop_mode;
    event->direction_before = direction_before;
    event->direction_after = direction_after;
    event->page_changed = (expected_page != expected_after_page) ? 1U : 0U;
    event->source_checksum = checksum;
    event->source0_bits = source0;
    event->source1_bits = source1;
    event->neighbor_bits = neighbor;
    if (flags != 0U)
    {
        g_brick6_multi_pitch_trace_header.anomaly_count++;
    }
    g_brick6_multi_pitch_trace_header.segment_count++;
}

#else

void brick6_multi_pitch_trace_reset(void) {}
void brick6_multi_pitch_trace_block_begin(uint32_t frames, const void *scratch0,
                                          const void *scratch1, const void *scratch2,
                                          const void *scratch3)
{
    (void)frames; (void)scratch0; (void)scratch1; (void)scratch2; (void)scratch3;
}
void brick6_multi_pitch_trace_block_end(void) {}
uint32_t brick6_multi_pitch_trace_voice_begin(uint8_t voice_id, uint8_t owner_track,
                                              uint16_t sample_id, sample_audio_key_t key,
                                              uint32_t voice_generation,
                                              const sample_play_plan_t *plan)
{
    (void)voice_id; (void)owner_track; (void)sample_id; (void)key;
    (void)voice_generation; (void)plan; return 0U;
}
void brick6_multi_pitch_trace_voice_end(uint32_t start_cycle, uint8_t voice_id,
                                        uint8_t owner_track, uint16_t sample_id,
                                        sample_audio_key_t key, uint32_t voice_generation,
                                        const void *scratch0, const void *scratch1)
{
    (void)start_cycle; (void)voice_id; (void)owner_track; (void)sample_id;
    (void)key; (void)voice_generation; (void)scratch0; (void)scratch1;
}
void brick6_multi_pitch_trace_segment(uint8_t voice_id, uint8_t owner_track, uint16_t sample_id,
                                     sample_audio_key_t key, uint32_t voice_generation,
                                     const sample_play_plan_t *plan,
                                     const sample_audio_cursor_t *cursor_before,
                                     const sample_audio_segment_t *segment,
                                     float position_after, uint8_t direction_before,
                                     uint8_t direction_after)
{
    (void)voice_id; (void)owner_track; (void)sample_id; (void)key;
    (void)voice_generation; (void)plan; (void)cursor_before; (void)segment;
    (void)position_after; (void)direction_before; (void)direction_after;
}

#endif
