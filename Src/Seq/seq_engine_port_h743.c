#include "Seq/seq_engine.h"
#include "NoteFx/note_fx_engine.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_media_clock.h"
#include "stm32h7xx.h"
#include <limits.h>
#include <string.h>

typedef enum { SLOT_FREE = 0, SLOT_WRITING, SLOT_READY, SLOT_READING } slot_state_t;
static seq_event_block_t g_output[SEQ_ENGINE_BLOCK_SLOTS];
static AUDIO_STATE_D3 seq_param_block_t g_params[SEQ_ENGINE_BLOCK_SLOTS];
static SEQ_STATE_D2 seq_engine_core_t g_core;
static volatile uint8_t g_slot_state[SEQ_ENGINE_BLOCK_SLOTS];
static volatile uint64_t g_service_now, g_publish_until;
static volatile uint64_t g_next_deadline = UINT64_MAX;
static volatile uint32_t g_wake_cycle;
static volatile uint8_t g_pending;
static volatile uint8_t g_urgent_pending;
static int8_t g_audio_slot = -1;
static uint16_t g_audio_cursor;
static volatile uint16_t g_disarmed_tracks;
static uint32_t g_disarm_generation;
static volatile uint8_t g_force_stopped;
static volatile uint64_t g_force_stop_sample;
static volatile uint32_t g_force_stop_epoch;
typedef struct {
    uint64_t capture_sample;
    uint32_t occurrence_id;
    uint8_t track,note,velocity,note_on,provenance;
} seq_ingress_entry_t;
static seq_ingress_entry_t g_ingress[SEQ_ENGINE_INGRESS_CAPACITY];
static volatile uint8_t g_ingress_head,g_ingress_tail,g_ingress_count;
static volatile uint8_t g_ingress_panic;

#define SEQ_DIAG_MAGIC UINT32_C(0x53455131)
#define SEQ_DIAG_VERSION 1U
CTRL_STATE __attribute__((used)) volatile seq_engine_diag_t g_seq_diag;

static void diag_max(volatile uint32_t *maximum, uint32_t value)
{ if (value > *maximum) *maximum = value; }

uint32_t seq_engine_port_cycles(void) { return DWT->CYCCNT; }

void seq_engine_control_disarm_track(uint8_t track)
{
    if (track < SEQ_LANE_CAPACITY) {
        g_disarmed_tracks |= (uint16_t)(1U << track); __DMB();
    }
}

static uint16_t active_track_mask(const seq_event_block_t *block)
{
    return block ? (uint16_t)(block->emitter_tracks
        & (uint16_t)~g_disarmed_tracks) : 0U;
}

void seq_engine_irq_init(void)
{
    memset(g_output, 0, sizeof(g_output));
    memset(g_params, 0, sizeof(g_params));
    memset((void *)g_slot_state, 0, sizeof(g_slot_state));
    memset((void *)&g_seq_diag, 0, sizeof(g_seq_diag));
    g_seq_diag.magic = SEQ_DIAG_MAGIC;
    g_seq_diag.version = SEQ_DIAG_VERSION;
    g_seq_diag.size = (uint16_t)sizeof(g_seq_diag);
    g_seq_diag.min_publish_slack_samples = UINT32_MAX;
    g_audio_slot = -1; g_audio_cursor = 0U; g_pending = 0U; g_urgent_pending=0U;
    g_disarmed_tracks = 0U; g_disarm_generation = 0U;
    g_force_stopped = 0U; g_force_stop_epoch=0U; g_next_deadline = UINT64_MAX;
    g_ingress_head=0U;g_ingress_tail=0U;g_ingress_count=0U;g_ingress_panic=0U;
    seq_engine_core_init(&g_core);
    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_SetPriority(TIM4_IRQn, 2U);
    NVIC_EnableIRQ(TIM4_IRQn);
}

void seq_engine_audio_boundary(uint64_t block_start_sample, uint8_t recovering)
{
    if (g_audio_slot >= 0) {
        g_slot_state[(uint8_t)g_audio_slot] = SLOT_FREE; g_audio_slot = -1;
    }
    if (recovering != 0U)
        for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
            if (g_slot_state[i] == SLOT_READY) g_slot_state[i] = SLOT_FREE;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i) {
        if ((g_slot_state[i] == SLOT_READY)
                && (g_output[i].start_sample == block_start_sample)) {
            g_slot_state[i] = SLOT_READING; g_audio_slot = (int8_t)i;
            g_audio_cursor = 0U; break;
        }
        if ((g_slot_state[i] == SLOT_READY)
                && (g_output[i].start_sample < block_start_sample)) {
            g_slot_state[i] = SLOT_FREE; ++g_seq_diag.missed_window_count;
        }
    }
    if ((g_audio_slot < 0) && (g_seq_diag.service_count != 0U))
        ++g_seq_diag.missed_window_count;
    g_service_now = block_start_sample;
    g_publish_until = block_start_sample + SEQ_ENGINE_H743_PERIOD_SAMPLES;
    g_wake_cycle = DWT->CYCCNT; __DMB();
    if (g_pending != 0U) ++g_seq_diag.missed_window_count;
    g_pending = 1U; NVIC_SetPendingIRQ(TIM4_IRQn);
}

static uint8_t event_is_audible(const seq_event_block_t *block,
                                const seq_event_t *event)
{
    if ((event == 0) || (event->track >= SEQ_LANE_CAPACITY)) return 0U;
    if (event->kind == SEQ_ENGINE_EVENT_PARAM)
        return (uint8_t)((block->lock_tracks
            & (uint16_t)(1U << event->track)) != 0U);
    return (uint8_t)((active_track_mask(block)
        & (uint16_t)(1U << event->track)) != 0U);
}

uint16_t seq_engine_audio_frames_until_due(uint64_t sample, uint16_t maximum)
{
    if ((g_audio_slot < 0) || (maximum == 0U)) return maximum;
    const seq_event_block_t *const block = &g_output[(uint8_t)g_audio_slot];
    while (g_audio_cursor < block->event_count) {
        const seq_event_t *const event = &block->events[g_audio_cursor];
        if (event_is_audible(block, event) == 0U) { ++g_audio_cursor; continue; }
        const uint64_t due = block->start_sample + event->offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)) {
            ++g_audio_cursor; continue;
        }
        if (due <= sample) return 0U;
        const uint64_t distance = due - sample;
        return (distance < maximum) ? (uint16_t)distance : maximum;
    }
    return maximum;
}

uint8_t seq_engine_audio_pop_due(uint64_t sample, seq_event_t *out_event)
{
    if ((out_event == 0) || (g_audio_slot < 0)) return 0U;
    seq_event_block_t *const block = &g_output[(uint8_t)g_audio_slot];
    while (g_audio_cursor < block->event_count) {
        const seq_event_t event = block->events[g_audio_cursor];
        if (event_is_audible(block, &event) == 0U) { ++g_audio_cursor; continue; }
        const uint64_t due = block->start_sample + event.offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)) {
            ++g_audio_cursor; continue;
        }
        if (due > sample) return 0U;
        ++g_audio_cursor; *out_event = event; return 1U;
    }
    return 0U;
}

void seq_engine_audio_retire_occurrence(uint32_t occurrence_id)
{ (void)occurrence_id; }

uint16_t seq_engine_audio_track_mask(void)
{
    return (g_audio_slot >= 0)
        ? active_track_mask(&g_output[(uint8_t)g_audio_slot]) : 0U;
}

void seq_engine_audio_force_stop(uint64_t effective_sample)
{ g_force_stop_sample = effective_sample; g_force_stop_epoch=g_core.transport_epoch;
  g_force_stopped = 1U; }

uint8_t seq_engine_playhead_view(uint8_t track,uint8_t *out_running,
    uint8_t *out_step)
{
    if((track>=SEQ_LANE_CAPACITY)||(out_running==0)||(out_step==0))return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    *out_running=g_core.running;*out_step=g_core.play_step[track];
    __set_PRIMASK(primask);return 1U;
}

uint64_t seq_next_deadline(void) { return g_next_deadline; }

uint8_t seq_ingress_note(uint8_t track,uint8_t note,uint8_t velocity,
    uint8_t note_on,uint32_t occurrence_id,uint8_t provenance,uint64_t capture_sample)
{
    if((track>=SEQ_LANE_CAPACITY)||(note>=128U)||(occurrence_id==0U))return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    if(g_ingress_count>=SEQ_ENGINE_INGRESS_CAPACITY){__set_PRIMASK(primask);return 0U;}
    g_ingress[g_ingress_head]=(seq_ingress_entry_t){capture_sample,occurrence_id,track,note,
        velocity,note_on,provenance};
    g_ingress_head=(uint8_t)((g_ingress_head+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
    ++g_ingress_count;
    if(g_ingress_count>g_seq_diag.ingress_peak)g_seq_diag.ingress_peak=g_ingress_count;
    g_urgent_pending=1U;g_wake_cycle=DWT->CYCCNT;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);return 1U;
}

void seq_ingress_panic(void)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    g_ingress_count=0U;g_ingress_head=0U;g_ingress_tail=0U;g_ingress_panic=1U;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);
}

void seq_service(uint64_t now_sample, uint64_t publish_until_sample)
{
    const uint32_t started = DWT->CYCCNT;
    const uint32_t response = started - g_wake_cycle;
    g_seq_diag.last_response_cycles = response;
    diag_max(&g_seq_diag.max_response_cycles, response);
    uint8_t slot = SEQ_ENGINE_BLOCK_SLOTS;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
        if (g_slot_state[i] == SLOT_FREE) { slot = i; break; }
    if ((slot == SEQ_ENGINE_BLOCK_SLOTS) || (publish_until_sample <= now_sample)
            || ((publish_until_sample - now_sample) > UINT16_MAX)) {
        ++g_seq_diag.fifo_invariant_failures; return;
    }
    g_slot_state[slot] = SLOT_WRITING;
    seq_event_block_t *const block = &g_output[slot];
    const uint32_t cutover_started = DWT->CYCCNT;
    const seq_pattern_t *const pattern = seq_engine_pattern_capture();
    if ((pattern != 0) && (pattern->generation != g_disarm_generation)) {
        g_disarmed_tracks = 0U; g_disarm_generation = pattern->generation;
    }
    diag_max(&g_seq_diag.cutover_max_cycles, DWT->CYCCNT-cutover_started);
    if((g_force_stopped!=0U)&&(pattern!=0)&&(pattern->running!=0U)
            &&(pattern->transport_epoch!=g_force_stop_epoch))
        g_force_stopped=0U;
    const uint64_t start = publish_until_sample;
    const uint16_t frames = SEQ_ENGINE_H743_PERIOD_SAMPLES;
    if ((g_force_stopped != 0U) && (start >= g_force_stop_sample)) {
        memset(block, 0, sizeof(*block)); block->start_sample = start;
        block->frames = frames;
    } else {
        g_force_stopped = 0U;
        seq_engine_core_process_block(&g_core, start, frames, pattern,
                                      block, &g_params[slot]);
        if(g_ingress_panic!=0U){g_ingress_panic=0U;
            seq_engine_core_init(&g_core);}
        while(g_ingress_count!=0U){
            const seq_ingress_entry_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            uint64_t captured=in.capture_sample;
            const uint64_t seq_delay=(now_sample>captured)?now_sample-captured:0U;
            diag_max(&g_seq_diag.max_ingress_capture_to_seq_samples,
                (seq_delay>UINT32_MAX)?UINT32_MAX:(uint32_t)seq_delay);
            const uint64_t due=(captured<start)?start:captured;
            const uint64_t publish_delay=(due>captured)?due-captured:0U;
            diag_max(&g_seq_diag.max_ingress_capture_to_publish_samples,
                (publish_delay>UINT32_MAX)?UINT32_MAX:(uint32_t)publish_delay);
            const note_event_t event={.sample_abs=due,
                .duration_samples=NOTE_EVENT_DURATION_OPEN,
                .source_id=in.occurrence_id,.intent_id=in.occurrence_id,
                .source_generation=pattern?pattern->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .destination_id=NOTE_EVENT_DESTINATION_DEFAULT,.note=in.note,
                .velocity=in.velocity,.kind=in.note_on?NOTE_EVENT_KIND_ON:NOTE_EVENT_KIND_OFF,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            if(seq_engine_core_submit_live(&g_core,&event,start,start+frames,block)==0U)
                ++g_seq_diag.fifo_invariant_failures;
        }
        const uint32_t publish_started=DWT->CYCCNT;
        seq_param_block_t *const params = &g_params[slot];
        if ((uint32_t)block->event_count + params->event_count
                > SEQ_ENGINE_EVENT_CAPACITY) {
            block->event_count = 0U; block->emitter_tracks = 0U;
            block->lock_tracks = 0U; ++g_seq_diag.fifo_invariant_failures;
        } else {
            for (uint16_t i = 0U; i < params->event_count; ++i) {
                const seq_param_event_t *const event = &params->events[i];
                block->events[block->event_count++] = (seq_event_t){
                    .offset=event->offset,.kind=SEQ_ENGINE_EVENT_PARAM,
                    .track=event->track,.occurrence_id=event->param_id,
                    .value=event->value16,.velocity=event->semantic};
            }
            seq_engine_event_order(block);
        }
        diag_max(&g_seq_diag.terminal_publish_max_cycles,
                 DWT->CYCCNT-publish_started);
    }
    block->block_id = (uint32_t)(start / frames);
    g_next_deadline = start + frames; __DMB(); g_slot_state[slot] = SLOT_READY;
    ++g_seq_diag.service_count;
    const uint32_t slack=(uint32_t)(start-now_sample);
    if(slack<g_seq_diag.min_publish_slack_samples)
        g_seq_diag.min_publish_slack_samples=slack;
    if (g_core.future_count > g_seq_diag.future_peak)
        g_seq_diag.future_peak = g_core.future_count;
    if (g_core.lifetime_count > g_seq_diag.lifetime_peak)
        g_seq_diag.lifetime_peak = g_core.lifetime_count;
    if (block->event_count > g_seq_diag.output_peak)
        g_seq_diag.output_peak = block->event_count;
    const uint32_t cycles = DWT->CYCCNT - started;
    g_seq_diag.last_service_cycles = cycles;
    if (cycles > g_seq_diag.max_service_cycles) {
        g_seq_diag.max_service_cycles = cycles;
        g_seq_diag.max_service_future_count = g_core.future_count;
        g_seq_diag.max_service_lifetime_count = g_core.lifetime_count;
        g_seq_diag.max_service_output_count = block->event_count;
    }
}

static void seq_service_urgent(uint64_t now_sample,uint64_t publish_until_sample)
{
    for(uint8_t slot=0U;slot<SEQ_ENGINE_BLOCK_SLOTS;++slot){
        if((g_slot_state[slot]!=SLOT_READY)
                ||(g_output[slot].start_sample!=publish_until_sample))continue;
        g_slot_state[slot]=SLOT_WRITING;
        seq_event_block_t *const block=&g_output[slot];
        const uint64_t end=block->start_sample+block->frames;
        while(g_ingress_count!=0U){
            const seq_ingress_entry_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            const uint64_t seq_delay=(now_sample>in.capture_sample)
                ?now_sample-in.capture_sample:0U;
            diag_max(&g_seq_diag.max_ingress_capture_to_seq_samples,
                (seq_delay>UINT32_MAX)?UINT32_MAX:(uint32_t)seq_delay);
            const uint64_t due=(in.capture_sample<block->start_sample)
                ?block->start_sample:in.capture_sample;
            const uint64_t publish_delay=due-in.capture_sample;
            diag_max(&g_seq_diag.max_ingress_capture_to_publish_samples,
                (publish_delay>UINT32_MAX)?UINT32_MAX:(uint32_t)publish_delay);
            const note_event_t event={.sample_abs=due,
                .duration_samples=NOTE_EVENT_DURATION_OPEN,
                .source_id=in.occurrence_id,.intent_id=in.occurrence_id,
                .source_generation=block->generation?block->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .destination_id=NOTE_EVENT_DESTINATION_DEFAULT,.note=in.note,
                .velocity=in.velocity,.kind=in.note_on?NOTE_EVENT_KIND_ON:NOTE_EVENT_KIND_OFF,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            if(seq_engine_core_submit_live(&g_core,&event,block->start_sample,end,block)==0U)
                ++g_seq_diag.fifo_invariant_failures;
        }
        seq_engine_event_order(block);
        __DMB();g_slot_state[slot]=SLOT_READY;return;
    }
}

void TIM4_IRQHandler(void)
{
    if ((g_pending == 0U)&&(g_urgent_pending==0U)) return;
    uint64_t now = g_service_now;const uint64_t until = g_publish_until;
    const uint8_t urgent=g_urgent_pending,periodic=g_pending;
    if(urgent!=0U)(void)brick_media_clock_now_sample(&now);
    if(now>g_service_now)diag_max(&g_seq_diag.max_wake_lateness_samples,
        (now-g_service_now>UINT32_MAX)?UINT32_MAX:(uint32_t)(now-g_service_now));
    g_pending = 0U;
    if(periodic!=0U)seq_service(now,until);else seq_service_urgent(now,until);
    g_urgent_pending=0U;
}

void seq_engine_get_diag(seq_engine_diag_t *out_diag)
{
    if (out_diag == 0) return;
    const uint32_t primask = __get_PRIMASK(); __disable_irq();
    *out_diag = g_seq_diag; __set_PRIMASK(primask);
}
