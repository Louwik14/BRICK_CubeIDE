#include "Seq/seq_rt_pass1.h"
#include "IPC/control_audio_command.h"
#include "Track/control_music_output.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

typedef enum { SLOT_FREE = 0, SLOT_WRITING, SLOT_READY, SLOT_READING } slot_state_t;
static seq_rt_event_block_t g_event_blocks[SEQ_RT_BLOCK_SLOTS];
static AUDIO_STATE_D3 seq_rt_param_block_t g_param_blocks[SEQ_RT_BLOCK_SLOTS];
static SEQ_STATE_D2 seq_rt_core_t g_seq_rt_core;
static volatile uint8_t g_slot_state[SEQ_RT_BLOCK_SLOTS];
static volatile uint64_t g_pending_start_sample;
static volatile uint8_t g_pending;
static volatile seq_rt_pass1_diag_t g_diag;
static int8_t g_compare_slot = -1;
static uint16_t g_audio_event_cursor;
static uint32_t g_compare_unexpected_start;
static volatile uint8_t g_force_stopped;
static volatile uint16_t g_control_disarm_tracks;
static uint32_t g_force_stop_epoch;
static uint64_t g_force_stop_sample;
static uint32_t g_disarm_clear_generation;

typedef struct {
    uint32_t output_id;
    uint32_t shadow_occurrence_id;
} compare_occurrence_t;

#define COMPARE_OCCURRENCE_CAPACITY 192U
static compare_occurrence_t g_compare_occurrence[COMPARE_OCCURRENCE_CAPACITY];
static uint32_t g_rt_audible_occurrence[COMPARE_OCCURRENCE_CAPACITY];
static uint32_t g_suppressed_legacy_output[COMPARE_OCCURRENCE_CAPACITY];

void seq_rt_pass1_control_disarm_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY) return;
    g_control_disarm_tracks |= (uint16_t)(1U << track);
    __DMB();
}

static uint16_t seq_rt_pass1_audio_effective_track_mask(
    const seq_rt_event_block_t *block)
{
    return (block != 0)
        ? (uint16_t)(block->rt_note_tracks & (uint16_t)~g_control_disarm_tracks)
        : 0U;
}

static uint16_t seq_rt_pass1_audio_effective_plock_mask(
    const seq_rt_event_block_t *block)
{
    return (block != 0)
        ? (uint16_t)(block->rt_plock_tracks & (uint16_t)~g_control_disarm_tracks)
        : 0U;
}

static uint8_t seq_rt_pass1_id_contains(const uint32_t *ids, uint32_t id)
{
    if (id == 0U) return 0U;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
        if (ids[i] == id) return 1U;
    return 0U;
}

static void seq_rt_pass1_id_add(uint32_t *ids, uint32_t id)
{
    if ((id == 0U) || (seq_rt_pass1_id_contains(ids, id) != 0U)) return;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
        if (ids[i] == 0U) { ids[i] = id; return; }
    ++g_diag.dropped_events;
}

static uint8_t seq_rt_pass1_id_take(uint32_t *ids, uint32_t id)
{
    if (id == 0U) return 0U;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
    {
        if (ids[i] != id) continue;
        ids[i] = 0U;
        return 1U;
    }
    return 0U;
}

static void seq_rt_pass1_compare_remember(uint32_t output_id,
                                         uint32_t shadow_occurrence_id)
{
    if (output_id == 0U) return;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
    {
        if ((g_compare_occurrence[i].output_id != 0U)
                && (g_compare_occurrence[i].output_id != output_id)) continue;
        g_compare_occurrence[i].output_id = output_id;
        g_compare_occurrence[i].shadow_occurrence_id = shadow_occurrence_id;
        return;
    }
}

static uint32_t seq_rt_pass1_compare_take(uint32_t output_id)
{
    if (output_id == 0U) return 0U;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
    {
        if (g_compare_occurrence[i].output_id != output_id) continue;
        const uint32_t occurrence = g_compare_occurrence[i].shadow_occurrence_id;
        g_compare_occurrence[i] = (compare_occurrence_t){0};
        return occurrence;
    }
    return 0U;
}

static void seq_rt_pass1_finish_compare(void)
{
    if (g_compare_slot < 0) return;
    seq_rt_event_block_t *const block = &g_event_blocks[(uint8_t)g_compare_slot];
    uint32_t missing = 0U;
    for (uint16_t i = 0U; i < block->event_count; ++i)
        if ((block->events[i].kind == SEQ_RT_EVENT_NOTE_ON
                || block->events[i].kind == SEQ_RT_EVENT_NOTE_OFF)
                && (block->events[i].reserved == 0U)) ++missing;
    if ((missing != 0U)
            || (g_diag.unexpected_legacy_events != g_compare_unexpected_start))
    {
        g_diag.missing_shadow_events += missing;
        ++g_diag.divergent_blocks;
    }
    g_slot_state[(uint8_t)g_compare_slot] = SLOT_FREE;
    g_compare_slot = -1;
}

void seq_rt_pass1_irq_init(void)
{
    for (uint8_t i = 0U; i < SEQ_RT_BLOCK_SLOTS; ++i)
        g_slot_state[i] = SLOT_FREE;
    g_pending = 0U;
    g_compare_slot = -1;
    g_audio_event_cursor = 0U;
    g_compare_unexpected_start = 0U;
    g_force_stopped = 0U;
    g_control_disarm_tracks = 0U;
    g_force_stop_epoch = 0U;
    g_force_stop_sample = 0U;
    g_disarm_clear_generation = 0U;
    seq_rt_core_init(&g_seq_rt_core);
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
    {
        g_compare_occurrence[i] = (compare_occurrence_t){0};
        g_rt_audible_occurrence[i] = 0U;
        g_suppressed_legacy_output[i] = 0U;
    }
    g_diag = (seq_rt_pass1_diag_t){0};
    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_SetPriority(TIM4_IRQn, 2U);
    NVIC_EnableIRQ(TIM4_IRQn);
}

void seq_rt_pass1_audio_boundary(uint64_t block_start_sample, uint8_t recovering)
{
    seq_rt_pass1_finish_compare();
    if (recovering != 0U)
    {
        for (uint8_t i = 0U; i < SEQ_RT_BLOCK_SLOTS; ++i)
            if (g_slot_state[i] == SLOT_READY)
                g_slot_state[i] = SLOT_FREE;
    }
    uint8_t found = 0U;
    for (uint8_t i = 0U; i < SEQ_RT_BLOCK_SLOTS; ++i)
    {
        if ((g_slot_state[i] == SLOT_READY)
                && (g_event_blocks[i].start_sample == block_start_sample)
                && (g_event_blocks[i].frames == SEQ_RT_BLOCK_FRAMES))
        {
            found = 1U;
            g_diag.ready++;
            g_diag.snapshot_generation = g_event_blocks[i].generation;
            for (uint16_t event = 0U;
                 event < g_event_blocks[i].event_count; ++event)
                g_event_blocks[i].events[event].reserved = 0U;
            g_diag.shadow_events += g_event_blocks[i].event_count;
            g_slot_state[i] = SLOT_READING;
            g_compare_slot = (int8_t)i;
            g_audio_event_cursor = 0U;
            g_compare_unexpected_start = g_diag.unexpected_legacy_events;
            if (seq_rt_pass1_audio_effective_track_mask(
                    &g_event_blocks[i]) != 0U)
                ++g_diag.rt_audible_blocks;
        }
        else if ((g_slot_state[i] == SLOT_READY)
                 && (g_event_blocks[i].start_sample < block_start_sample))
        {
            g_diag.late++;
            g_slot_state[i] = SLOT_FREE;
        }
    }
    if ((found == 0U) && (g_diag.wakes != 0U)) g_diag.missing++;
    g_diag.last_start_sample = block_start_sample;
    g_diag.last_block_id = (uint32_t)(block_start_sample / SEQ_RT_BLOCK_FRAMES);
    if (g_pending != 0U) g_diag.coalesced++;
    g_pending_start_sample = block_start_sample + SEQ_RT_BLOCK_FRAMES;
    __DMB();
    g_pending = 1U;
    g_diag.wakes++;
    NVIC_SetPendingIRQ(TIM4_IRQn);
}

static uint8_t seq_rt_pass1_audio_event_is_audible(
    const seq_rt_event_block_t *block, const seq_rt_event_t *event)
{
    if ((block == 0) || (event == 0) || (event->track >= SEQ_LANE_CAPACITY))
        return 0U;
    if ((g_force_stopped != 0U)
            && (block->start_sample + event->offset >= g_force_stop_sample))
        return 0U;
    if (event->kind == SEQ_RT_EVENT_PARAM)
        return ((seq_rt_pass1_audio_effective_plock_mask(block)
            & (uint16_t)(1U << event->track)) != 0U) ? 1U : 0U;
    if (event->kind == SEQ_RT_EVENT_NOTE_ON)
        return ((seq_rt_pass1_audio_effective_track_mask(block)
            & (uint16_t)(1U << event->track)) != 0U)
            ? 1U : 0U;
    if (event->kind == SEQ_RT_EVENT_NOTE_OFF)
        return seq_rt_pass1_id_contains(g_rt_audible_occurrence,
                                        event->occurrence_id);
    return 0U;
}

uint16_t seq_rt_pass1_audio_frames_until_due(uint64_t sample,
                                             uint16_t maximum)
{
    if ((g_compare_slot < 0) || (maximum == 0U)) return maximum;
    const seq_rt_event_block_t *const block =
        &g_event_blocks[(uint8_t)g_compare_slot];
    while (g_audio_event_cursor < block->event_count)
    {
        const seq_rt_event_t *const event =
            &block->events[g_audio_event_cursor];
        if (seq_rt_pass1_audio_event_is_audible(block, event) != 0U)
        {
            const uint64_t due = block->start_sample + event->offset;
            if (due <= sample) return 0U;
            const uint64_t distance = due - sample;
            return (distance < maximum) ? (uint16_t)distance : maximum;
        }
        ++g_audio_event_cursor;
    }
    return maximum;
}

uint8_t seq_rt_pass1_audio_pop_due(uint64_t sample,
                                   seq_rt_event_t *out_event)
{
    if ((out_event == 0) || (g_compare_slot < 0)) return 0U;
    seq_rt_event_block_t *const block = &g_event_blocks[(uint8_t)g_compare_slot];
    while (g_audio_event_cursor < block->event_count)
    {
        const seq_rt_event_t event = block->events[g_audio_event_cursor];
        if (seq_rt_pass1_audio_event_is_audible(block, &event) == 0U)
        {
            ++g_audio_event_cursor;
            continue;
        }
        if (block->start_sample + event.offset > sample) return 0U;
        ++g_audio_event_cursor;
        if (event.kind == SEQ_RT_EVENT_NOTE_ON)
            seq_rt_pass1_id_add(g_rt_audible_occurrence,
                                event.occurrence_id);
        else if (event.kind == SEQ_RT_EVENT_NOTE_OFF)
            (void)seq_rt_pass1_id_take(g_rt_audible_occurrence,
                                       event.occurrence_id);
        *out_event = event;
        ++g_diag.rt_applied_events;
        return 1U;
    }
    return 0U;
}

uint8_t seq_rt_pass1_audio_suppress_legacy(uint8_t kind, uint8_t track,
                                           uint32_t output_id)
{
    if (kind == 0U)
    {
        const uint8_t suppressed = seq_rt_pass1_id_take(
            g_suppressed_legacy_output, output_id);
        if (suppressed != 0U) ++g_diag.legacy_suppressed_events;
        return suppressed;
    }
    if ((g_compare_slot < 0) || (track >= SEQ_LANE_CAPACITY)
            || (control_music_output_handle_is_internal(output_id) == 0U))
        return 0U;
    const seq_rt_event_block_t *const block =
        &g_event_blocks[(uint8_t)g_compare_slot];
    if ((seq_rt_pass1_audio_effective_track_mask(block)
            & (uint16_t)(1U << track)) == 0U) return 0U;
    seq_rt_pass1_id_add(g_suppressed_legacy_output, output_id);
    ++g_diag.legacy_suppressed_events;
    return 1U;
}

uint8_t seq_rt_pass1_audio_suppress_legacy_param(uint8_t kind, uint8_t track)
{
    if ((kind < CONTROL_AUDIO_PARAM_KIND_SEQ_TEMP_TRACK)
            || (kind > CONTROL_AUDIO_PARAM_KIND_SEQ_RESTORE_BASE_TRACK)
            || (g_compare_slot < 0) || (track >= SEQ_LANE_CAPACITY)) return 0U;
    return ((seq_rt_pass1_audio_effective_plock_mask(
        &g_event_blocks[(uint8_t)g_compare_slot]) & (uint16_t)(1U<<track)) != 0U)
        ? 1U : 0U;
}

void seq_rt_pass1_audio_retire_legacy(uint32_t output_id)
{
    seq_rt_pass1_id_add(g_suppressed_legacy_output, output_id);
}

void seq_rt_pass1_audio_retire_occurrence(uint32_t occurrence_id)
{
    (void)seq_rt_pass1_id_take(g_rt_audible_occurrence, occurrence_id);
}

uint16_t seq_rt_pass1_audio_track_mask(void)
{
    return (g_compare_slot >= 0)
        ? seq_rt_pass1_audio_effective_track_mask(
            &g_event_blocks[(uint8_t)g_compare_slot]) : 0U;
}

void seq_rt_pass1_audio_force_stop(uint64_t effective_sample)
{
    const seq_rt_projection_t *const projection =
        seq_rt_pass1_projection_capture();
    g_force_stop_epoch = (projection != 0) ? projection->transport_epoch : 0U;
    g_force_stop_sample = effective_sample;
    g_force_stopped = 1U;
    for (uint16_t i = 0U; i < COMPARE_OCCURRENCE_CAPACITY; ++i)
    {
        g_rt_audible_occurrence[i] = 0U;
        g_suppressed_legacy_output[i] = 0U;
    }
}

void seq_rt_pass1_audio_observe_note(uint64_t sample, uint8_t kind,
                                     uint8_t track, uint8_t note,
                                     uint8_t velocity, uint32_t output_id)
{
    ++g_diag.legacy_events;
    if (g_compare_slot < 0)
    {
        ++g_diag.unexpected_legacy_events;
        return;
    }
    seq_rt_event_block_t *const block = &g_event_blocks[(uint8_t)g_compare_slot];
    if ((track >= SEQ_LANE_CAPACITY)
            || ((block->comparable_tracks & (uint16_t)(1U << track)) == 0U))
        return;
    if ((sample < block->start_sample)
            || (sample >= block->start_sample + block->frames))
    {
        ++g_diag.unexpected_legacy_events;
        return;
    }
    const uint16_t offset = (uint16_t)(sample - block->start_sample);
    const uint8_t shadow_kind = (kind != 0U)
        ? SEQ_RT_EVENT_NOTE_ON : SEQ_RT_EVENT_NOTE_OFF;
    const uint32_t expected_occurrence = (shadow_kind == SEQ_RT_EVENT_NOTE_OFF)
        ? seq_rt_pass1_compare_take(output_id) : 0U;
    if ((shadow_kind == SEQ_RT_EVENT_NOTE_OFF)
            && (expected_occurrence == 0U)) return;
    for (uint16_t i = 0U; i < block->event_count; ++i)
    {
        seq_rt_event_t *const event = &block->events[i];
        if ((event->reserved == 0U) && (event->offset == offset)
                && (event->kind == shadow_kind) && (event->track == track)
                && (event->note == note)
                && ((shadow_kind != SEQ_RT_EVENT_NOTE_OFF)
                    || (event->occurrence_id == expected_occurrence))
                && ((shadow_kind == SEQ_RT_EVENT_NOTE_OFF)
                    || (event->velocity == velocity)))
        {
            event->reserved = 1U;
            if (shadow_kind == SEQ_RT_EVENT_NOTE_ON)
                seq_rt_pass1_compare_remember(output_id,
                                               event->occurrence_id);
            ++g_diag.matched_events;
            return;
        }
    }
    ++g_diag.unexpected_legacy_events;
}

void TIM4_IRQHandler(void)
{
    if (g_pending == 0U) return;
    const uint64_t start = g_pending_start_sample;
    g_pending = 0U;
    const uint32_t cycle_start = DWT->CYCCNT;
    uint8_t slot = SEQ_RT_BLOCK_SLOTS;
    for (uint8_t i = 0U; i < SEQ_RT_BLOCK_SLOTS; ++i)
        if (g_slot_state[i] == SLOT_FREE) { slot = i; break; }
    if (slot == SEQ_RT_BLOCK_SLOTS)
    {
        g_diag.missing++;
        return;
    }
    g_slot_state[slot] = SLOT_WRITING;
    seq_rt_event_block_t *const block = &g_event_blocks[slot];
    const seq_rt_projection_t *const projection =
        seq_rt_pass1_projection_capture();
    if ((projection != 0)
            && (projection->generation != g_disarm_clear_generation))
    {
        g_control_disarm_tracks = 0U;
        g_disarm_clear_generation = projection->generation;
        __DMB();
    }
    if ((g_force_stopped != 0U)
            && ((projection == 0) || (projection->running != 0U)
                || (projection->transport_epoch == g_force_stop_epoch)))
    {
        block->start_sample = start;
        block->frames = SEQ_RT_BLOCK_FRAMES;
        block->generation = (projection != 0) ? projection->generation : 0U;
        block->comparable_tracks = 0U;
        block->rt_note_tracks = 0U;
        block->rt_plock_tracks = 0U;
        block->event_count = 0U;
    }
    else
    {
        g_force_stopped = 0U;
        seq_rt_core_process_block(&g_seq_rt_core, start, SEQ_RT_BLOCK_FRAMES,
            projection, block, &g_param_blocks[slot]);
        seq_rt_param_block_t *const params=&g_param_blocks[slot];
        if ((uint32_t)block->event_count+params->event_count
                > SEQ_RT_EVENT_CAPACITY)
        {
            block->rt_note_tracks=0U;block->rt_plock_tracks=0U;
            ++g_seq_rt_core.dropped_events;
        }
        else
        {
            for(uint16_t i=0U;i<params->event_count;++i){
                const seq_rt_param_event_t *e=&params->events[i];
                block->events[block->event_count++]=(seq_rt_event_t){
                    .offset=e->offset,.kind=SEQ_RT_EVENT_PARAM,.track=e->track,
                    .occurrence_id=e->param_id,.value=e->value16,
                    .velocity=e->semantic};}
            for(uint16_t a=1U;a<block->event_count;++a){
                const seq_rt_event_t key=block->events[a];uint16_t b=a;
                while((b!=0U)&&((block->events[b-1U].offset>key.offset)
                    ||((block->events[b-1U].offset==key.offset)
                    &&(block->events[b-1U].kind>key.kind)))){
                    block->events[b]=block->events[b-1U];--b;}
                block->events[b]=key;}
        }
    }
    g_diag.dropped_events = g_seq_rt_core.dropped_events;
    block->block_id = (uint32_t)(start / SEQ_RT_BLOCK_FRAMES);
    __DMB();
    g_slot_state[slot] = SLOT_READY;
    const uint32_t cycles = DWT->CYCCNT - cycle_start;
    if (cycles > g_diag.max_irq_cycles) g_diag.max_irq_cycles = cycles;
}

void seq_rt_pass1_get_diag(seq_rt_pass1_diag_t *out_diag)
{
    if (out_diag == 0) return;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *out_diag = g_diag;
    __set_PRIMASK(primask);
}
