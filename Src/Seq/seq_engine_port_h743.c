#include "Seq/seq_engine.h"
#include "NoteFx/note_fx_engine.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_media_clock.h"
#include "stm32h7xx.h"
#include <limits.h>
#include <string.h>

typedef enum { SLOT_FREE = 0, SLOT_WRITING, SLOT_READY, SLOT_READING } slot_state_t;
static SEQ_STATE_SDRAM seq_terminal_block_t g_terminal[SEQ_ENGINE_BLOCK_SLOTS];
static SEQ_STATE_D2 seq_engine_core_t g_core;
static volatile uint8_t g_slot_state[SEQ_ENGINE_BLOCK_SLOTS];
static volatile uint64_t g_service_now, g_publish_until;
static volatile uint64_t g_next_deadline = UINT64_MAX;
static volatile uint8_t g_pending;
static volatile uint8_t g_urgent_pending;
static int8_t g_audio_slot = -1;
static uint16_t g_audio_cursor;
static uint8_t g_audio_offset,g_audio_class;
static volatile uint16_t g_disarmed_tracks;
static uint32_t g_disarm_generation;
static volatile uint8_t g_force_stopped;
static volatile uint8_t g_force_stop_preserve_live;
static volatile uint32_t g_missed_horizons;
static volatile uint64_t g_force_stop_sample;
static volatile uint32_t g_force_stop_epoch;
static seq_ingress_event_t g_ingress[SEQ_ENGINE_INGRESS_CAPACITY];
static volatile uint8_t g_ingress_head,g_ingress_tail,g_ingress_count;
static volatile uint8_t g_ingress_panic;
static uint64_t g_ingress_rate_window;
static uint8_t g_ingress_rate_count;
void seq_engine_control_disarm_track(uint8_t track)
{
    if (track < SEQ_LANE_CAPACITY) {
        g_disarmed_tracks |= (uint16_t)(1U << track); __DMB();
    }
}

static uint16_t active_track_mask(const seq_terminal_block_t *block)
{
    return block ? (uint16_t)(block->emitter_tracks
        & (uint16_t)~g_disarmed_tracks) : 0U;
}

void seq_engine_irq_init(void)
{
    memset(g_terminal, 0, sizeof(g_terminal));
    memset((void *)g_slot_state, 0, sizeof(g_slot_state));
    g_audio_slot = -1; g_audio_cursor = SEQ_ENGINE_TERMINAL_INDEX_NONE;
    g_audio_offset=0U;g_audio_class=0U;g_pending = 0U; g_urgent_pending=0U;
    g_disarmed_tracks = 0U; g_disarm_generation = 0U;
    g_force_stopped = 0U; g_force_stop_preserve_live=0U;
    g_force_stop_epoch=0U; g_next_deadline = UINT64_MAX;
    g_missed_horizons=0U;
    g_ingress_head=0U;g_ingress_tail=0U;g_ingress_count=0U;g_ingress_panic=0U;
    g_ingress_rate_window=UINT64_MAX;g_ingress_rate_count=0U;
    seq_engine_core_init(&g_core);
    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_SetPriority(TIM4_IRQn, 2U);
    NVIC_EnableIRQ(TIM4_IRQn);
}

void seq_engine_audio_boundary(uint64_t block_start_sample, uint8_t recovering)
{
    uint8_t acquired=0U;
    if (g_audio_slot >= 0) {
        g_slot_state[(uint8_t)g_audio_slot] = SLOT_FREE; g_audio_slot = -1;
    }
    if (recovering != 0U)
        for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
            if (g_slot_state[i] == SLOT_READY) g_slot_state[i] = SLOT_FREE;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i) {
        if ((g_slot_state[i] == SLOT_READY)
                && (g_terminal[i].start_sample == block_start_sample)) {
            g_slot_state[i] = SLOT_READING; g_audio_slot = (int8_t)i;
            g_audio_cursor=SEQ_ENGINE_TERMINAL_INDEX_NONE;
            g_audio_offset=0U;g_audio_class=0U;acquired=1U;break;
        }
        if ((g_slot_state[i] == SLOT_READY)
                && (g_terminal[i].start_sample < block_start_sample)) {
            g_slot_state[i] = SLOT_FREE;
        }
    }
    if(recovering==0U&&acquired==0U)++g_missed_horizons;
    g_service_now = block_start_sample;
    g_publish_until = block_start_sample + SEQ_ENGINE_H743_PERIOD_SAMPLES;
    __DMB();
    g_pending = 1U; NVIC_SetPendingIRQ(TIM4_IRQn);
}

static uint8_t event_is_audible(const seq_terminal_block_t *block,uint8_t kind,
                                const seq_terminal_event_t *event)
{
    if(event==0)return 0U;
    const uint8_t track=(kind==SEQ_ENGINE_EVENT_PARAM)
        ?event->param.track:event->note.track;
    if(track>=SEQ_LANE_CAPACITY)return 0U;
    if (kind == SEQ_ENGINE_EVENT_PARAM)
        return (uint8_t)((block->lock_tracks
            & (uint16_t)(1U << track)) != 0U);
    return (uint8_t)((active_track_mask(block)
        & (uint16_t)(1U << track)) != 0U);
}

static uint8_t event_is_live_note(uint8_t kind,
                                  const seq_terminal_event_t *event)
{
    if((event==0)||((kind!=SEQ_ENGINE_EVENT_NOTE_ON)
            &&(kind!=SEQ_ENGINE_EVENT_NOTE_OFF)))return 0U;
    const uint32_t source=event->note.occurrence_id
        &~NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    return(uint8_t)((source==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)
        ||(source==NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI));
}

static uint8_t audio_cursor_seek(seq_terminal_block_t *block)
{for(;;){if(g_audio_cursor!=SEQ_ENGINE_TERMINAL_INDEX_NONE)return 1U;
  while(g_audio_offset<block->frames){while(g_audio_class<SEQ_ENGINE_TERMINAL_CLASS_COUNT){
    const uint16_t head=block->head[g_audio_offset][g_audio_class];
    if(head!=SEQ_ENGINE_TERMINAL_INDEX_NONE){g_audio_cursor=head;return 1U;}
    ++g_audio_class;}++g_audio_offset;g_audio_class=0U;}return 0U;}}

static void audio_cursor_advance(const seq_terminal_block_t *block)
{if(g_audio_cursor!=SEQ_ENGINE_TERMINAL_INDEX_NONE)
    g_audio_cursor=block->next[g_audio_cursor];
 if(g_audio_cursor==SEQ_ENGINE_TERMINAL_INDEX_NONE)++g_audio_class;}

uint16_t seq_engine_audio_frames_until_due(uint64_t sample, uint16_t maximum)
{
    if ((g_audio_slot < 0) || (maximum == 0U)) return maximum;
    seq_terminal_block_t *const block = &g_terminal[(uint8_t)g_audio_slot];
    while(audio_cursor_seek(block)!=0U){
        const seq_terminal_event_t *const event=&block->events[g_audio_cursor];
        if(event_is_audible(block,g_audio_class,event)==0U){audio_cursor_advance(block);continue;}
        const uint64_t due = block->start_sample + g_audio_offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)
                &&((g_force_stop_preserve_live==0U)
                    ||(event_is_live_note(g_audio_class,event)==0U))) {
            audio_cursor_advance(block);continue;
        }
        if (due <= sample) return 0U;
        const uint64_t distance = due - sample;
        return (distance < maximum) ? (uint16_t)distance : maximum;
    }
    return maximum;
}

uint8_t seq_engine_audio_pop_due(uint64_t sample,uint8_t *out_kind,
    seq_terminal_event_t *out_event)
{
    if ((out_event == 0)||(out_kind==0)||(g_audio_slot < 0)) return 0U;
    seq_terminal_block_t *const block = &g_terminal[(uint8_t)g_audio_slot];
    while(audio_cursor_seek(block)!=0U){
        const seq_terminal_event_t event=block->events[g_audio_cursor];
        if(event_is_audible(block,g_audio_class,&event)==0U){audio_cursor_advance(block);continue;}
        const uint64_t due = block->start_sample + g_audio_offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)
                &&((g_force_stop_preserve_live==0U)
                    ||(event_is_live_note(g_audio_class,&event)==0U))) {
            audio_cursor_advance(block);continue;
        }
        if (due > sample) return 0U;
        *out_kind=g_audio_class;*out_event=event;audio_cursor_advance(block);return 1U;
    }
    return 0U;
}

void seq_engine_audio_retire_occurrence(uint32_t occurrence_id)
{ (void)occurrence_id; }

uint16_t seq_engine_audio_track_mask(void)
{
    return (g_audio_slot >= 0)
        ? active_track_mask(&g_terminal[(uint8_t)g_audio_slot]) : 0U;
}

void seq_engine_audio_force_stop(uint64_t effective_sample,
    uint8_t preserve_live_notes)
{ g_force_stop_sample = effective_sample; g_force_stop_epoch=g_core.transport_epoch;
  g_force_stop_preserve_live=(preserve_live_notes!=0U)?1U:0U;
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

uint8_t seq_ingress_submit(const seq_ingress_event_t *event)
{
    if((event==0)||(event->track>=SEQ_LANE_CAPACITY)||(event->note>=128U)
            ||(event->velocity>=128U)||(event->kind>NOTE_EVENT_KIND_ON)
            ||(event->provenance>=NOTE_EVENT_SOURCE_COUNT)
            ||(event->occurrence_id==0U))return 0U;
    const seq_pattern_t *const pattern=seq_engine_pattern_capture();
    if(event->track==BRICK_ENTITY_GROUP_MASTER_ID
            ||(pattern!=0&&pattern->track_exec[event->track].logical_capacity==0U))return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    const uint64_t rate_window=event->capture_sample/SEQ_INGRESS_WINDOW_SAMPLES;
    if(g_ingress_rate_window==UINT64_MAX){g_ingress_rate_window=rate_window;
        g_ingress_rate_count=0U;}
    else if(rate_window>g_ingress_rate_window){g_ingress_rate_window=rate_window;
        g_ingress_rate_count=0U;}
    else if(rate_window<g_ingress_rate_window){__set_PRIMASK(primask);return 0U;}
    if((g_ingress_rate_count>=SEQ_INGRESS_EVENTS_PER_WINDOW_MAX)
            ||(g_ingress_count>=SEQ_ENGINE_INGRESS_CAPACITY)){
        __set_PRIMASK(primask);return 0U;}
    g_ingress[g_ingress_head]=*event;
    g_ingress_head=(uint8_t)((g_ingress_head+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
    ++g_ingress_count;
    ++g_ingress_rate_count;
    g_urgent_pending=1U;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);return 1U;
}

void seq_ingress_panic(void)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    g_ingress_count=0U;g_ingress_head=0U;g_ingress_tail=0U;g_ingress_panic=1U;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);
}

void seq_ingress_discard(void)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    g_ingress_count=0U;g_ingress_head=0U;g_ingress_tail=0U;
    __set_PRIMASK(primask);
}

void seq_service(uint64_t now_sample, uint64_t publish_until_sample)
{
    uint8_t slot = SEQ_ENGINE_BLOCK_SLOTS;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
        if (g_slot_state[i] == SLOT_FREE) { slot = i; break; }
    if ((slot == SEQ_ENGINE_BLOCK_SLOTS) || (publish_until_sample <= now_sample)
            || ((publish_until_sample - now_sample) > UINT16_MAX)) {
        return;
    }
    g_slot_state[slot] = SLOT_WRITING;
    seq_terminal_block_t *const block = &g_terminal[slot];
    const seq_pattern_t *const pattern = seq_engine_pattern_capture();
    if ((pattern != 0) && (pattern->generation != g_disarm_generation)) {
        g_disarmed_tracks = 0U; g_disarm_generation = pattern->generation;
    }
    if((g_force_stopped!=0U)&&(pattern!=0)
            &&((pattern->running==0U)
                ||(pattern->transport_epoch!=g_force_stop_epoch)))
        g_force_stopped=0U;
    const uint64_t start = publish_until_sample;
    const uint16_t frames = SEQ_ENGINE_H743_PERIOD_SAMPLES;
    if(g_ingress_panic!=0U){g_ingress_panic=0U;
        seq_engine_core_init(&g_core);}
    if ((g_force_stopped != 0U) && (start >= g_force_stop_sample)) {
        seq_engine_core_process_block(&g_core,start,frames,0,block);
        block->emitter_tracks=g_core.emitter_tracks;
    } else {
        g_force_stopped = 0U;
        seq_engine_core_process_block(&g_core,start,frames,pattern,block);
    }
    while(g_ingress_count!=0U){
            const seq_ingress_event_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            uint64_t captured=in.capture_sample;
            const uint64_t due=(captured<start)?start:captured;
            const note_event_t event={.sample_abs=due,
                .duration_samples=1U,
                .source_id=in.occurrence_id,.occurrence_id=in.occurrence_id,
                .source_generation=pattern?pattern->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .note=in.note,
                .velocity=in.velocity,.kind=in.kind,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            (void)seq_engine_core_submit_live(&g_core,&event,pattern,start,start+frames,block);
    }
    block->block_id = (uint32_t)(start / frames);
    g_next_deadline = start + frames; __DMB(); g_slot_state[slot] = SLOT_READY;
}

static void seq_service_urgent(uint64_t now_sample,uint64_t publish_until_sample)
{
    (void)now_sample;
    for(uint8_t slot=0U;slot<SEQ_ENGINE_BLOCK_SLOTS;++slot){
        const uint32_t primask=__get_PRIMASK();__disable_irq();
        if((g_slot_state[slot]!=SLOT_READY)
                ||(g_terminal[slot].start_sample!=publish_until_sample)){
            __set_PRIMASK(primask);continue;}
        g_slot_state[slot]=SLOT_WRITING;__DMB();__set_PRIMASK(primask);
        seq_terminal_block_t *const block=&g_terminal[slot];
        const uint64_t end=block->start_sample+block->frames;
        if(g_ingress_panic!=0U){g_ingress_panic=0U;
            seq_engine_core_init(&g_core);}
        while(g_ingress_count!=0U){
            const seq_ingress_event_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            const uint64_t due=(in.capture_sample<block->start_sample)
                ?block->start_sample:in.capture_sample;
            const note_event_t event={.sample_abs=due,
                .duration_samples=1U,
                .source_id=in.occurrence_id,.occurrence_id=in.occurrence_id,
                .source_generation=block->generation?block->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .note=in.note,
                .velocity=in.velocity,.kind=in.kind,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            const seq_pattern_t *const pattern=seq_engine_pattern_capture();
            (void)seq_engine_core_submit_live(&g_core,&event,pattern,
                block->start_sample,end,block);
        }
        __DMB();g_slot_state[slot]=SLOT_READY;return;
    }
}

void TIM4_IRQHandler(void)
{
if ((g_pending == 0U)&&(g_urgent_pending==0U)) {
return;
    }
    uint64_t now = g_service_now;const uint64_t until = g_publish_until;
    const uint8_t urgent=g_urgent_pending,periodic=g_pending;
    if(urgent!=0U)(void)brick_media_clock_now_sample(&now);
    g_pending=0U;g_urgent_pending=0U;
    if(periodic!=0U)seq_service(now,until);else seq_service_urgent(now,until);
    if(g_urgent_pending!=0U)NVIC_SetPendingIRQ(TIM4_IRQn);
}
