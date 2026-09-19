#include "Seq/seq_engine.h"
#include "Seq/seq_runtime.h"
#include "NoteFx/note_fx_engine.h"
#include "Param/param_ids.h"
#include "Platform/memory_layout.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static SEQ_STATE_D2 note_event_t g_seq_fx_a[NOTE_FX_BATCH_CAPACITY];
static SEQ_HOT_D1 note_event_t g_seq_fx_b[NOTE_FX_BATCH_CAPACITY];
static seq_engine_core_t *g_seq_fx_core;
static seq_event_block_t *g_seq_fx_block;
static uint64_t g_seq_fx_start;
static uint64_t g_seq_fx_end;
typedef enum {SEQ_DROP_SCHEDULER_CAPACITY=0,SEQ_DROP_GROOVE_RESUME_CAPACITY,
 SEQ_DROP_LEDGER_ADMISSION,SEQ_DROP_TERMINAL_OUTPUT_CAPACITY,
 SEQ_DROP_SOURCE_CAPACITY,SEQ_DROP_FX_PREPROCESS,SEQ_DROP_SOURCE_TRANSFORM,
 SEQ_DROP_SCHEDULED_OUTPUT_CAPACITY,SEQ_DROP_FX_POSTPROCESS,
 SEQ_DROP_REASON_COUNT} seq_drop_reason_t;
static uint32_t g_seq_drop_reason[SEQ_DROP_REASON_COUNT];
static void seq_drop(seq_engine_core_t*core,seq_drop_reason_t reason)
{++core->dropped_events;++g_seq_drop_reason[reason];}
void seq_engine_drop_diag_reset(void){memset(g_seq_drop_reason,0,sizeof(g_seq_drop_reason));}
void seq_engine_drop_diag_capture(uint32_t out[SEQ_DROP_REASON_COUNT])
{if(out!=0)memcpy(out,g_seq_drop_reason,sizeof(g_seq_drop_reason));}
typedef struct {uint32_t duration_samples,source_id,occurrence_id;
 uint8_t note,velocity,kind,flags;} seq_groove_resume_event_t;
typedef struct {uint64_t sample_abs;seq_groove_resume_event_t event[SEQ_PRODUCT_HARMONY_FANOUT_MAX];
 uint16_t ticket,generation;uint8_t track,temporal_index,count,active;} seq_groove_resume_t;
static SEQ_STATE_D2 seq_groove_resume_t
    g_groove_resume[SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY];
_Static_assert(sizeof(seq_groove_resume_t)==80U,"Groove resume batch budget");

static void groove_resume_event_store(seq_groove_resume_event_t *out,
    const note_event_t *event)
{*out=(seq_groove_resume_event_t){.duration_samples=event->duration_samples,
 .source_id=event->source_id,.occurrence_id=event->occurrence_id,
 .note=event->note,.velocity=event->velocity,.kind=event->kind,
 .flags=(uint8_t)(((event->flags&NOTE_EVENT_FLAG_GENERATED)!=0U)
    |(note_event_branch(event)<<1U))};}

static void scheduler_release(seq_engine_core_t *core,uint16_t index);
static uint16_t scheduler_add(seq_engine_core_t *core,uint64_t due,uint8_t kind,
    uint8_t track,uint8_t note,uint8_t flags,uint32_t occurrence,uint32_t payload)
{
    if(core->scheduler_free_head==UINT16_MAX){
        ++core->scheduler_overflow_count;seq_drop(core,SEQ_DROP_SCHEDULER_CAPACITY);return UINT16_MAX;}
    const uint16_t index=core->scheduler_free_head;
    seq_ticket_t *const ticket=&core->scheduler[index];
    core->scheduler_free_head=ticket->next_free;
    uint16_t generation=(uint16_t)(ticket->generation+1U);
    if(generation==0U)generation=1U;
    *ticket=(seq_ticket_t){.due_sample=due,.occurrence_id=occurrence,
        .payload=payload,.generation=generation,.next_free=UINT16_MAX,
        .kind=kind,.track=track,.note=note,.flags=(uint8_t)(flags|0x80U)};
    ++core->scheduler_count;return index;
}

static void scheduler_release(seq_engine_core_t *core,uint16_t index)
{
    if(index>=SEQ_ENGINE_SCHEDULER_CAPACITY)return;
    seq_ticket_t *const ticket=&core->scheduler[index];
    if((ticket->flags&0x80U)==0U)return;
    ticket->flags=0U;ticket->next_free=core->scheduler_free_head;
    core->scheduler_free_head=index;
    if(core->scheduler_count!=0U)--core->scheduler_count;
}

static void scheduler_clear(seq_engine_core_t *core)
{
    core->scheduler_count=0U;core->scheduler_free_head=0U;
    for(uint16_t i=0U;i<SEQ_ENGINE_SCHEDULER_CAPACITY;++i){
        core->scheduler[i].flags=0U;
        core->scheduler[i].next_free=(i+1U<SEQ_ENGINE_SCHEDULER_CAPACITY)
            ?(uint16_t)(i+1U):UINT16_MAX;}
}

static int16_t ledger_find(const seq_engine_core_t *core,uint32_t occurrence)
{for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i)
    if(core->ledger[i].active&&core->ledger[i].occurrence_id==occurrence)return(int16_t)i;
 return -1;}

static void ledger_release(seq_engine_core_t *core,uint8_t index)
{seq_ledger_entry_t *const entry=&core->ledger[index];if(!entry->active)return;
 entry->active=0U;if(core->ledger_count)--core->ledger_count;
 if(core->ledger_track_count[entry->track])--core->ledger_track_count[entry->track];}

static void cancel_note_off(seq_engine_core_t *core,uint32_t occurrence)
{for(uint16_t i=0U;i<SEQ_ENGINE_SCHEDULER_CAPACITY;++i)
    if((core->scheduler[i].flags&0x80U)&&core->scheduler[i].kind==SEQ_TICKET_NOTE_OFF
            &&core->scheduler[i].occurrence_id==occurrence)scheduler_release(core,i);}

static uint8_t groove_resume_store(seq_engine_core_t *core,const note_event_t *event)
{uint8_t count=0U;int16_t free_index=-1,victim=-1;
 for(uint8_t i=0U;i<SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY;++i){seq_groove_resume_t*x=&g_groove_resume[i];
  if(!x->active){if(free_index<0)free_index=(int16_t)i;continue;}
  if(x->track!=event->track||x->temporal_index!=event->temporal_index)continue;
  if(x->sample_abs==event->sample_abs){
   for(uint8_t n=0U;n<x->count;++n)if(x->event[n].occurrence_id==event->occurrence_id){
    groove_resume_event_store(&x->event[n],event);return 1U;}
   if(x->count<SEQ_PRODUCT_HARMONY_FANOUT_MAX){
    groove_resume_event_store(&x->event[x->count++],event);return 1U;}}
  ++count;if((victim<0)||x->sample_abs
      <g_groove_resume[(uint8_t)victim].sample_abs)victim=(int16_t)i;}
 if(count>=2U){scheduler_release(core,g_groove_resume[(uint8_t)victim].ticket);free_index=victim;}
 if(free_index<0)return 0U;
 seq_groove_resume_t*x=&g_groove_resume[(uint8_t)free_index];
 uint16_t generation=(uint16_t)(x->generation+1U);if(!generation)generation=1U;
 *x=(seq_groove_resume_t){.sample_abs=event->sample_abs,.generation=generation,
    .track=event->track,.temporal_index=event->temporal_index,.count=1U,.active=1U};
 groove_resume_event_store(&x->event[0],event);
 x->ticket=scheduler_add(core,event->sample_abs,SEQ_TICKET_GROOVE_RESUME,event->track,
     event->note,event->velocity,event->occurrence_id,(uint32_t)(uint8_t)free_index
     |((uint32_t)generation<<8U));
 if(x->ticket==UINT16_MAX){x->active=0U;return 0U;}return 1U;}

static int16_t ledger_admit(seq_engine_core_t *core,const note_event_t *event)
{
    const uint8_t quota=core->logical_capacity[event->track];
    if(quota==0U)return -1;
    uint8_t used_slots=0U;int16_t free_index=-1,victim=-1,global_victim=-1;
    for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i){
        const seq_ledger_entry_t *const entry=&core->ledger[i];
        if(!entry->active){if(free_index<0)free_index=(int16_t)i;continue;}
        if((global_victim<0)||((core->ledger[(uint8_t)global_victim].original!=0U)&&(entry->original==0U))
                ||((entry->original==core->ledger[(uint8_t)global_victim].original)
                &&(entry->admitted_sample<core->ledger[(uint8_t)global_victim].admitted_sample)))global_victim=(int16_t)i;
        if(entry->track!=event->track)continue;
        used_slots|=(uint8_t)(1U<<entry->logical_slot);
        if((victim<0)||((core->ledger[(uint8_t)victim].original!=0U)&&(entry->original==0U))
                ||((entry->original==core->ledger[(uint8_t)victim].original)
                &&(entry->admitted_sample<core->ledger[(uint8_t)victim].admitted_sample)))victim=(int16_t)i;}
    uint8_t logical_slot=0U;
    while((logical_slot<quota)&&((used_slots&(uint8_t)(1U<<logical_slot))!=0U))++logical_slot;
    if(logical_slot>=quota){if(victim<0)return -1;}
    else if(free_index<0)victim=global_victim;
    if((logical_slot>=quota)||(free_index<0)){
        if(victim<0)return -1;
        const seq_ledger_entry_t old=core->ledger[(uint8_t)victim];
        cancel_note_off(core,old.occurrence_id);
        if(g_seq_fx_block->event_count<SEQ_ENGINE_EVENT_CAPACITY)
            g_seq_fx_block->events[g_seq_fx_block->event_count++]=(seq_event_t){
                .offset=(uint16_t)((event->sample_abs<=g_seq_fx_start)?0U:
                    event->sample_abs-g_seq_fx_start),.kind=SEQ_ENGINE_EVENT_NOTE_OFF,
                .track=old.track,.occurrence_id=old.occurrence_id,.note=old.note};
        if(old.track==event->track)logical_slot=old.logical_slot;
        ledger_release(core,(uint8_t)victim);free_index=victim;}
    core->ledger[(uint8_t)free_index]=(seq_ledger_entry_t){
        .admitted_sample=event->sample_abs,.occurrence_id=event->occurrence_id,
        .track=event->track,.logical_slot=logical_slot,.note=event->note,
        .original=(uint8_t)(((event->flags&NOTE_EVENT_FLAG_GENERATED)==0U)?1U:0U),.active=1U};
    ++core->ledger_count;++core->ledger_track_count[event->track];return free_index;
}

static void fx_terminal(const note_event_t *e)
{
    uint64_t due=e->sample_abs;
    if(due<g_seq_fx_start)due=g_seq_fx_start;
    if(due>=g_seq_fx_end){note_event_t resume=*e;resume.sample_abs=due;
        if(!groove_resume_store(g_seq_fx_core,&resume))
            seq_drop(g_seq_fx_core,SEQ_DROP_GROOVE_RESUME_CAPACITY);
        return;}
    if(e->kind==NOTE_EVENT_KIND_OFF){const int16_t found=ledger_find(g_seq_fx_core,e->occurrence_id);
        if(found<0)return;
        ledger_release(g_seq_fx_core,(uint8_t)found);}
    else if(ledger_admit(g_seq_fx_core,e)<0){
        seq_drop(g_seq_fx_core,SEQ_DROP_LEDGER_ADMISSION);return;}
    if(g_seq_fx_block->event_count<SEQ_ENGINE_EVENT_CAPACITY)
        g_seq_fx_block->events[g_seq_fx_block->event_count++]=(seq_event_t){
            .offset=(uint16_t)(due-g_seq_fx_start),
            .kind=(e->kind==NOTE_EVENT_KIND_OFF)
                ?SEQ_ENGINE_EVENT_NOTE_OFF:SEQ_ENGINE_EVENT_NOTE_ON,
            .track=e->track,.occurrence_id=e->occurrence_id,
            .note=e->note,.velocity=e->velocity,
            .reserved=(uint16_t)(((e->flags&NOTE_EVENT_FLAG_GENERATED)!=0U)?1U:0U)};
    else {seq_drop(g_seq_fx_core,SEQ_DROP_TERMINAL_OUTPUT_CAPACITY);return;}
    const uint32_t source_namespace=e->source_id&~NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if(source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY
            ||source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI)
        (void)seq_runtime_live_rec_submit_effective(
            (source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)
                ?SEQ_LIVE_REC_SRC_INTERNAL:SEQ_LIVE_REC_SRC_EXTERNAL,
            (uint8_t)(e->kind==NOTE_EVENT_KIND_ON),0U,e->note,e->velocity,due,
            e->occurrence_id,e->occurrence_id);
    if((e->kind==NOTE_EVENT_KIND_ON)
            &&(e->duration_samples!=NOTE_EVENT_DURATION_OPEN))
        scheduler_add(g_seq_fx_core,due+(e->duration_samples?e->duration_samples:1U),
            SEQ_TICKET_NOTE_OFF,e->track,e->note,0U,e->occurrence_id,0U);
}

static note_event_result_t walker_resume(const note_event_t *source,uint8_t stage)
{
    g_seq_fx_a[0]=*source;uint8_t count=1U;
    note_event_t *in=g_seq_fx_a,*out=g_seq_fx_b;
    for(uint8_t slot=stage;slot<NOTE_FX_SLOT_COUNT;++slot){
        uint8_t out_count=0U;
        const note_event_result_t r=note_fx_engine_transform(slot,in,count,
            out,NOTE_FX_BATCH_CAPACITY,&out_count);
        if(r!=NOTE_EVENT_RESULT_ACCEPTED)return r;
        count=out_count;note_event_t*swap=in;in=out;out=swap;
        if(count==0U)return NOTE_EVENT_RESULT_ACCEPTED;}
    for(uint8_t i=0U;i<count;++i)fx_terminal(&in[i]);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t fx_generated(const note_event_t *event,void *ctx)
{(void)ctx;return walker_resume(event,event->stage);}

uint8_t seq_engine_core_submit_live(seq_engine_core_t *core,
    const note_event_t *event,uint64_t window_start,uint64_t window_end,
    seq_event_block_t *out_block)
{
    if((core==0)||(event==0)||(out_block==0)||(window_end<=window_start))
        return 0U;
    g_seq_fx_core=core;g_seq_fx_block=out_block;
    g_seq_fx_start=window_start;g_seq_fx_end=window_end;
    return (walker_resume(event,0U)==NOTE_EVENT_RESULT_ACCEPTED)?1U:0U;
}

static uint8_t event_after(const seq_event_t *left,const seq_event_t *right)
{ return (uint8_t)((left->offset>right->offset)
    ||((left->offset==right->offset)&&((left->kind>right->kind)
    ||((left->kind==right->kind)&&(left->reserved>right->reserved))))); }

ITCM_TEXT void seq_engine_event_order(seq_event_block_t *block)
{
    if((block==0)||(block->event_count<2U))return;
    for(uint16_t i=1U;i<block->event_count;++i){const seq_event_t item=block->events[i];
        uint16_t j=i;while(j&&event_after(&block->events[j-1U],&item)){
            block->events[j]=block->events[j-1U];--j;}block->events[j]=item;}
}

static void sources_clear(seq_engine_core_t *core)
{core->source_count=0U;core->ledger_count=0U;
 memset(core->sources,0,sizeof(core->sources));
 memset(core->ledger,0,sizeof(core->ledger));
 memset(core->ledger_track_count,0,sizeof(core->ledger_track_count));}

static const seq_play_item_t *step_item(const seq_pattern_t *p,
    uint8_t track, uint8_t step, uint8_t voice)
{
    if (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
        return &p->top_play[track][step].items[voice];
    if ((track < SEQ_LANE_CAPACITY) && (voice == 0U))
        return &p->child_play[track - BRICK_ENTITY_TOP_LEVEL_COUNT][step];
    return 0;
}

static int16_t play_value(const seq_pattern_t *p, uint8_t track,
    uint8_t step, uint8_t voice, seq_step_play_field_t field)
{
    const seq_play_item_t *item = step_item(p, track, step, voice);
    const seq_play_item_t *base = &p->play_base[track].items[voice];
    const uint8_t mask = (uint8_t)(1U << (uint8_t)field);
    const seq_play_item_t *source = ((item != 0)
        && ((item->present_mask & mask) != 0U)) ? item : base;
    switch (field) {
        case SEQ_STEP_PLAY_FIELD_NOTE: return source->note;
        case SEQ_STEP_PLAY_FIELD_VELOCITY: return source->velocity;
        case SEQ_STEP_PLAY_FIELD_LENGTH: return source->length;
        case SEQ_STEP_PLAY_FIELD_MICROTIMING: return source->microtiming;
        default: return 0;
    }
}

static uint16_t roll_divisor(uint8_t roll)
{
    static const uint16_t values[SEQ_STEP_ROLL_COUNT] =
        {0U,20U,24U,32U,40U,48U,64U,80U};
    return (roll < SEQ_STEP_ROLL_COUNT) ? values[roll] : 0U;
}

static uint64_t first_on(const seq_pattern_t *p, uint8_t track,
    uint8_t swing_phase, uint64_t nominal, uint64_t span_q16, int16_t mictim)
{
    uint64_t swing = 0U;
    if (((swing_phase & 1U) != 0U) && (p->track_swing[track] != 0U))
        swing = ((span_q16 * p->track_swing[track] + 100ULL) / 200ULL
            + 0x8000ULL) >> 16;
    int64_t micro = ((int64_t)mictim * p->samples_per_step_q16)
        / (96LL * 65536LL);
    const int64_t scaled = micro * (100 - p->track_quant[track]);
    micro = (scaled + ((scaled >= 0LL) ? 50LL : -50LL)) / 100LL;
    const uint64_t swung = nominal + swing;
    if (micro >= 0) return swung + (uint64_t)micro;
    return (swung > (uint64_t)(-micro)) ? swung - (uint64_t)(-micro) : 0U;
}

static ITCM_TEXT void source_add(seq_engine_core_t *core, uint64_t first_on,
    uint64_t span_q16, uint64_t interval_q16, uint32_t gate_samples,
    uint8_t track, uint8_t logical_slot, uint8_t note, uint8_t velocity,
    uint8_t playback_stage)
{
    const uint8_t quota=(track<BRICK_ENTITY_TOP_LEVEL_COUNT)?8U:1U;
    uint8_t owned=0U;uint16_t target=UINT16_MAX,oldest=UINT16_MAX;
    for(uint16_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i){
        const seq_source_cursor_t *const source=&core->sources[i];
        if(source->active==0U){if(target==UINT16_MAX)target=i;continue;}
        if(source->track!=track)continue;
        ++owned;
        if((oldest==UINT16_MAX)||(source->first_on_sample<core->sources[oldest].first_on_sample)
                ||((source->first_on_sample==core->sources[oldest].first_on_sample)&&(i<oldest)))oldest=i;}
    if(owned>=quota)target=oldest;
    if(target==UINT16_MAX){seq_drop(core,SEQ_DROP_SOURCE_CAPACITY);return;}
    seq_source_cursor_t *const source=&core->sources[target];
    if(source->active!=0U)scheduler_release(core,source->ticket);else ++core->source_count;
    const uint32_t serial=++core->occurrence_serial;
    const uint16_t ticket=scheduler_add(core,first_on,SEQ_TICKET_SOURCE_WAKE,
        track,note,velocity,serial,target);
    if(ticket==UINT16_MAX){source->active=0U;if(core->source_count)--core->source_count;return;}
    *source=(seq_source_cursor_t){.first_on_sample=first_on,.span_q16=span_q16,
        .interval_q16=interval_q16,.gate_samples=gate_samples,.serial=serial,
        .ticket=ticket,.track=track,.note=note,.velocity=velocity,
        .logical_slot=logical_slot,.playback_stage=playback_stage,.active=1U};
}

static uint8_t next_step(const seq_pattern_t *p, uint8_t track,
    uint8_t current)
{
    const uint8_t length = p->track_length[track] ? p->track_length[track] : 1U;
    return ((uint8_t)(current + 1U) < length) ? (uint8_t)(current + 1U) : 0U;
}

static void configure_fx_step(const seq_pattern_t *p,uint8_t track,
    uint8_t step)
{
    note_fx_track_state_t state=p->note_fx[track];
    const uint16_t first=p->lock_first[track][step];
    const uint8_t count=p->steps[track][step].lock_count;
    for(uint8_t n=0U;n<count;++n){
        const seq_lock_pattern_t *const lock=&p->lock_pool[track][first+n];
        if((lock->param_flags&SEQ_ENGINE_PARAM_FLAG_NOTE_FX)==0U)continue;
        const uint8_t slot=(uint8_t)(lock->param_flags
            &SEQ_ENGINE_FX_PLAN_SLOT_MASK);
        const uint32_t word=(uint32_t)lock->value16
            |((uint32_t)lock->base_value16<<16U);
        state.value[slot][NOTE_FX_PARAM_COUNT-1U]=note_fx_plan_model(word);
        for(uint8_t param=0U;param<NOTE_FX_PARAM_COUNT-1U;++param)
            state.value[slot][param]=note_fx_plan_param(word,param);
    }
    for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
        if(note_fx_engine_configure(track,slot,
            state.value[slot][NOTE_FX_PARAM_COUNT-1U],state.value[slot][0],
            state.value[slot][1],state.value[slot][2],0U)!=NOTE_EVENT_RESULT_ACCEPTED)
            return;
}

static void schedule_step(seq_engine_core_t *core, const seq_pattern_t *p,
    uint8_t track, uint8_t step, uint8_t swing_phase, uint32_t serial,
    uint64_t nominal, uint8_t negative_only)
{
    if ((p->track_can_emit[track] == 0U) || (p->track_muted[track] != 0U)
            || ((p->steps[track][step].trig_roll & 1U) == 0U)) return;
    const uint8_t voices = (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
        ? SEQ_PLAY_MAX_CAPACITY : 1U;
    const uint64_t span_q16 =
        (uint64_t)p->samples_per_step_q16 * p->track_div[track];
    const uint16_t divisor = roll_divisor(
        (uint8_t)(p->steps[track][step].trig_roll >> 1U));
    uint64_t interval_q16 = divisor ? (span_q16 * 16ULL) / divisor : span_q16;
    if (interval_q16 == 0U) interval_q16 = span_q16;
    for (uint8_t voice = 0U; voice < voices; ++voice) {
        if (core->voice_scheduled_serial[track][voice] == serial) continue;
        const int16_t note = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_NOTE);
        const int16_t vel = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_VELOCITY);
        int16_t length = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_LENGTH);
        const int16_t mictim = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_MICROTIMING);
        if ((note < 0) || (note >= 128) || (vel <= 0)) continue;
        if (length < 1) length = 1;
        if (length > 64) length = 64;
        const uint64_t first = first_on(p,track,swing_phase,nominal,span_q16,mictim);
        if ((negative_only != 0U) && (first >= nominal)) continue;
        core->voice_scheduled_serial[track][voice] = serial;
        const uint64_t gate = ((uint64_t)(uint16_t)length
            * p->samples_per_step_q16 + 0x8000ULL) >> 16;
        source_add(core,first,span_q16,interval_q16,
            (uint32_t)(gate ? gate : 1U),track,voice,(uint8_t)note,(uint8_t)vel,
            (uint8_t)((step_item(p,track,step,voice)!=0
                &&(step_item(p,track,step,voice)->present_mask&SEQ_STEP_PLAY_TERMINAL))
                ?NOTE_EVENT_STAGE_TERMINAL:NOTE_EVENT_STAGE_SOURCE));
    }
}

static void schedule_boundary(seq_engine_core_t *core,
    const seq_pattern_t *p, uint64_t sample, uint16_t hit_mask,
    uint64_t block_start, seq_param_block_t *params)
{
    for (uint8_t track=0U; track<SEQ_LANE_CAPACITY; ++track) {
        if ((hit_mask & (uint16_t)(1U << track)) == 0U) continue;
        core->emitter_tracks |= (uint16_t)(1U << track);
        const uint8_t step=core->play_step[track];
        if(p->track_fx_enabled[track]!=0U)configure_fx_step(p,track,step);
        if ((p->track_lock_enabled[track] != 0U)
                && ((core->plock_fault_tracks & (uint16_t)(1U << track)) == 0U))
        {
            const uint16_t first=p->lock_first[track][step];
            const uint8_t count=p->steps[track][step].lock_count;
            const uint8_t old_count=core->active_lock_count[track];
            if ((uint32_t)params->event_count+old_count+count
                    > SEQ_ENGINE_PARAM_EVENT_CAPACITY)
                core->plock_fault_tracks|=(uint16_t)(1U<<track);
            else {
                uint8_t old_index=0U,new_index=0U,active_write=0U;
                while ((old_index<old_count)||(new_index<count)) {
                    while ((new_index<count)&&((p->lock_pool[track][first+new_index].param_flags
                            &SEQ_ENGINE_PARAM_FLAG_NOTE_FX)!=0U)) ++new_index;
                    const uint16_t old_key=(old_index<old_count)
                        ?(uint16_t)(core->active_locks[track][old_index].param_flags
                            &SEQ_ENGINE_PARAM_ID_MASK):UINT16_MAX;
                    const uint16_t new_key=(new_index<count)
                        ?(uint16_t)(p->lock_pool[track][first+new_index].param_flags
                            &SEQ_ENGINE_PARAM_ID_MASK):UINT16_MAX;
                    if ((old_key==UINT16_MAX)&&(new_key==UINT16_MAX)) break;
                    if (old_key<new_key) {
                        const seq_active_lock_t active=
                            core->active_locks[track][old_index++];
                        params->events[params->event_count++]=(seq_param_event_t){
                            .offset=(uint16_t)(sample-block_start),.param_id=old_key,
                            .value16=active.base_value16,.track=track,
                            .semantic=((active.param_flags&SEQ_ENGINE_PARAM_FLAG_CLEARABLE)!=0U)
                                ?SEQ_ENGINE_PARAM_CLEAR_TEMP:SEQ_ENGINE_PARAM_RESTORE_BASE};
                        continue;
                    }
                    const seq_lock_pattern_t *lock=&p->lock_pool[track][first+new_index++];
                    params->events[params->event_count++]=(seq_param_event_t){
                        .offset=(uint16_t)(sample-block_start),
                        .param_id=(uint16_t)(lock->param_flags&SEQ_ENGINE_PARAM_ID_MASK),
                        .value16=lock->value16,.track=track,.semantic=SEQ_ENGINE_PARAM_TEMP};
                    core->active_locks[track][active_write++]=(seq_active_lock_t){
                        .param_flags=lock->param_flags,.base_value16=lock->base_value16};
                    if (old_key==new_key) ++old_index;
                }
                core->active_lock_count[track]=active_write;
            }
        }
        const uint32_t serial=core->step_serial[track];
        schedule_step(core,p,track,step,core->track_swing_phase[track],serial,
            sample,0U);
        const uint64_t next_sample=sample+(((uint64_t)p->samples_per_step_q16
            * p->track_div[track]+0x8000ULL)>>16);
        schedule_step(core,p,track,next_step(p,track,step),
            (uint8_t)(core->track_swing_phase[track]^1U),serial+1U,
            next_sample,1U);
    }
}

static uint16_t advance(seq_engine_core_t *core,const seq_pattern_t *p)
{
    uint16_t hits = 0U;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
        uint8_t div=p->track_div[track];
        if((div!=1U)&&(div!=2U)&&(div!=4U)&&(div!=8U))div=1U;
        if(core->track_div_phase[track]<(uint8_t)(div-1U)){
            ++core->track_div_phase[track];continue;}
        core->track_div_phase[track]=0U;
        core->track_swing_phase[track]^=1U;
        core->play_step[track]=next_step(p,track,core->play_step[track]);
        ++core->step_serial[track];
        hits |= (uint16_t)(1U << track);
    }
    return hits;
}

static ITCM_TEXT void collect(seq_engine_core_t *core,uint64_t start,uint16_t frames,
    const seq_pattern_t *p,seq_event_block_t *out)
{
    const uint64_t end=start+frames;
    g_seq_fx_core=core;g_seq_fx_block=out;g_seq_fx_start=start;g_seq_fx_end=end;
    uint32_t pattern[NOTE_FX_TRACK_COUNT];
    for(uint8_t t=0U;t<NOTE_FX_TRACK_COUNT;++t)
        pattern[t]=(uint32_t)core->play_step[t]<<16U;
    const uint64_t step_samples=p->samples_per_step_q16?p->samples_per_step_q16:1U;
    const uint64_t delta_q16=((start<<16U)>=core->step_sample_q16)
        ?(((start<<16U)-core->step_sample_q16)<<16U)/step_samples:0U;
    const uint64_t transport=((uint64_t)core->step_serial[0]<<16U)+delta_q16;
    if(note_fx_engine_process(start,0U,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        seq_drop(core,SEQ_DROP_FX_PREPROCESS);
    for(;;){
        uint16_t selected=UINT16_MAX;uint64_t selected_due=end;
        for(uint16_t i=0U;i<SEQ_ENGINE_SCHEDULER_CAPACITY;++i){
            const seq_ticket_t *const ticket=&core->scheduler[i];
            if((ticket->flags&0x80U)==0U||ticket->due_sample>=end||ticket->due_sample>selected_due
                    ||(ticket->due_sample==selected_due&&selected!=UINT16_MAX
                    &&ticket->kind>=core->scheduler[selected].kind))continue;
            selected=i;selected_due=ticket->due_sample;}
        if(selected==UINT16_MAX)break;
        const seq_ticket_t ticket=core->scheduler[selected];
        scheduler_release(core,selected);
        if(ticket.kind==SEQ_TICKET_SOURCE_WAKE){
            if(ticket.payload>=SEQ_ENGINE_LEDGER_CAPACITY)continue;
            seq_source_cursor_t *const source=&core->sources[ticket.payload];
            if((source->active==0U)||(source->serial!=ticket.occurrence_id))continue;
            uint64_t on=ticket.due_sample;if(on<start)on=start;
            const uint32_t occurrence=++core->occurrence_serial;
            const note_event_t event={.sample_abs=on,
                .duration_samples=source->gate_samples,
                .source_token=occurrence,.occurrence_id=occurrence,
                .generation=p->generation?p->generation:1U,.group_id=occurrence,
                .track=source->track,.destination_id=NOTE_EVENT_DESTINATION_DEFAULT,
                .note=source->note,.velocity=source->velocity,
                .kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_STEP,
                .stage=source->playback_stage,.temporal_index=source->logical_slot,
                .flags=(uint8_t)((source->playback_stage==NOTE_EVENT_STAGE_TERMINAL)
                    ?NOTE_EVENT_FLAG_TERMINAL:0U)};
            if(source->playback_stage==NOTE_EVENT_STAGE_TERMINAL)fx_terminal(&event);
            else if(walker_resume(&event,0U)!=NOTE_EVENT_RESULT_ACCEPTED)
                seq_drop(core,SEQ_DROP_SOURCE_TRANSFORM);
            source->next_offset_q16+=source->interval_q16;
            if(source->next_offset_q16<source->span_q16){
                const uint64_t next_due=source->first_on_sample
                    +((source->next_offset_q16+0x8000ULL)>>16);
                source->ticket=scheduler_add(core,next_due,SEQ_TICKET_SOURCE_WAKE,
                    source->track,source->note,source->velocity,source->serial,ticket.payload);
                if(source->ticket!=UINT16_MAX)continue;}
            source->active=0U;if(core->source_count)--core->source_count;
            continue;}
        if(ticket.kind==SEQ_TICKET_GROOVE_RESUME){const uint8_t index=(uint8_t)ticket.payload;
            const uint16_t generation=(uint16_t)(ticket.payload>>8U);
            if(index<SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY&&g_groove_resume[index].active
                    &&g_groove_resume[index].generation==generation){
                const seq_groove_resume_t resume=g_groove_resume[index];
                g_groove_resume[index].active=0U;
                for(uint8_t n=0U;n<resume.count;++n){const seq_groove_resume_event_t*x=&resume.event[n];
                    const note_event_t event={.sample_abs=resume.sample_abs,
                        .duration_samples=x->duration_samples,.source_id=x->source_id,
                        .occurrence_id=x->occurrence_id,.track=resume.track,.note=x->note,
                        .velocity=x->velocity,.kind=x->kind,
                        .flags=(uint8_t)(x->flags&NOTE_EVENT_FLAG_GENERATED)};fx_terminal(&event);}}continue;}
        if(ticket.kind==SEQ_TICKET_NOTE_OFF){
            const int16_t found=ledger_find(core,ticket.occurrence_id);
            if(found<0)continue;
            ledger_release(core,(uint8_t)found);}
        if(out->event_count>=SEQ_ENGINE_EVENT_CAPACITY){
            seq_drop(core,SEQ_DROP_SCHEDULED_OUTPUT_CAPACITY);continue;}
        out->events[out->event_count++]=(seq_event_t){
            .offset=(uint16_t)((ticket.due_sample<start)?0U:ticket.due_sample-start),
            .kind=(ticket.kind==SEQ_TICKET_NOTE_OFF)?SEQ_ENGINE_EVENT_NOTE_OFF:
                SEQ_ENGINE_EVENT_NOTE_ON,.track=ticket.track,
            .occurrence_id=ticket.occurrence_id,.note=ticket.note,
            .velocity=(ticket.kind==SEQ_TICKET_NOTE_OFF)?0U:(uint8_t)(ticket.flags&0x7FU),
            .reserved=(uint16_t)(((ticket.payload&0x100U)!=0U)?1U:0U)};}
    if(note_fx_engine_process(start,frames,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        seq_drop(core,SEQ_DROP_FX_POSTPROCESS);
}

void seq_engine_core_init(seq_engine_core_t *core)
{
    if(core==0)return;
    memset(core,0,sizeof(*core));
    memset(g_groove_resume,0,sizeof(g_groove_resume));
    sources_clear(core);
    scheduler_clear(core);
    note_fx_engine_init();
    for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)for(uint8_t v=0U;
        v<SEQ_PLAY_MAX_CAPACITY;++v)core->voice_scheduled_serial[t][v]=UINT32_MAX;
}

void seq_engine_core_process_block(seq_engine_core_t *core,uint64_t start,uint16_t frames,
    const seq_pattern_t *p,seq_event_block_t *out,
    seq_param_block_t *params)
{
    if((core==0)||(out==0)||(params==0))return;
    out->start_sample=start;out->frames=frames;out->generation=p?p->generation:0U;
    out->emitter_tracks=0U;
    out->lock_tracks=0U;
    out->event_count=0U;if((p==0)||(frames==0U))return;
    params->start_sample=start;params->frames=frames;
    params->generation=p->generation;params->event_count=0U;
    if((core->initialized==0U)||(core->transport_epoch!=p->transport_epoch)){
        seq_engine_core_init(core);core->initialized=1U;core->transport_epoch=p->transport_epoch;
        core->running=p->running;core->step_sample_q16=p->seed_step_sample_q16;
        core->samples_per_step_q16=p->samples_per_step_q16;
        memcpy(core->play_step,p->seed_play_step,sizeof(core->play_step));
        memcpy(core->track_div_phase,p->seed_div_phase,sizeof(core->track_div_phase));
        memcpy(core->track_swing_phase,p->seed_swing_phase,sizeof(core->track_swing_phase));
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t){
            const uint8_t s=core->play_step[t];const uint8_t count=p->steps[t][s].lock_count;
            const uint16_t first=p->lock_first[t][s];
            if((p->track_lock_enabled[t]!=0U)&&(count<=SEQ_STEP_MAX_LOCKS)){
                uint8_t write=0U;
                for(uint8_t n=0U;n<count;++n){
                    if((p->lock_pool[t][first+n].param_flags
                            &SEQ_ENGINE_PARAM_FLAG_NOTE_FX)!=0U)continue;
                    core->active_locks[t][write++]=(seq_active_lock_t){
                        .param_flags=p->lock_pool[t][first+n].param_flags,
                        .base_value16=p->lock_pool[t][first+n].base_value16};}
                core->active_lock_count[t]=write;}
            if(p->track_fx_enabled[t]!=0U)configure_fx_step(p,t,s);}
        }
    core->samples_per_step_q16=p->samples_per_step_q16;
    for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t){uint8_t capacity=p->track_exec[t].logical_capacity;
        core->logical_capacity[t]=(capacity<=8U)?capacity:8U;}
    if(core->pattern_generation!=p->generation){
        core->pattern_generation=p->generation;
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)
            if(p->track_fx_enabled[t]!=0U)
                configure_fx_step(p,t,core->play_step[t]);}
    if((p->running==0U)||(core->samples_per_step_q16==0U)){
        core->running=0U;scheduler_clear(core);sources_clear(core);
        memset(g_groove_resume,0,sizeof(g_groove_resume));return;}core->running=1U;
    const uint32_t dropped_before=core->dropped_events;
    const uint64_t begin_q16=start<<16,end_q16=(start+frames)<<16;
    uint64_t next=core->step_sample_q16+core->samples_per_step_q16;
    while(next<end_q16){const uint16_t hits=advance(core,p);core->step_sample_q16=next;
        if(next>=begin_q16)schedule_boundary(core,p,(next+0x8000ULL)>>16,hits,
            start,params);
        next=core->step_sample_q16+core->samples_per_step_q16;}
    collect(core,start,frames,p,out);
    if(core->dropped_events!=dropped_before){core->event_faulted=1U;return;}
    if(core->event_faulted!=0U)return;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track)
    {
        const uint16_t bit=(uint16_t)(1U<<track);
        if((p->track_lock_enabled[track]!=0U)
                &&((core->plock_fault_tracks&bit)==0U)
                &&((core->emitter_tracks&bit)!=0U))
            out->lock_tracks|=bit;
        if((p->track_note_enabled[track]!=0U)
                &&(p->track_muted[track]==0U)
                &&((core->emitter_tracks&bit)!=0U))
            out->emitter_tracks|=bit;
    }
}
