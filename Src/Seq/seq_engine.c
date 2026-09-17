#include "Seq/seq_engine.h"
#include "NoteFx/note_fx_engine.h"
#include "Param/param_ids.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static AUDIO_STATE_D3 note_event_t g_seq_fx_a[NOTE_FX_BATCH_CAPACITY];
static AUDIO_STATE_D3 note_event_t g_seq_fx_b[NOTE_FX_BATCH_CAPACITY];
static seq_engine_core_t *g_seq_fx_core;
static seq_event_block_t *g_seq_fx_block;
static uint64_t g_seq_fx_start;
static uint64_t g_seq_fx_end;

static seq_future_handle_t future_add(seq_engine_core_t *core,uint64_t due,uint8_t kind,
    uint8_t track,uint8_t note,uint8_t velocity,uint32_t occurrence)
{
    seq_future_handle_t invalid={UINT16_MAX,0U};
    if(core->future_free_head==UINT16_MAX){++core->dropped_events;return invalid;}
    const uint16_t index=core->future_free_head;
    seq_future_t *const slot=&core->futures[index];
    core->future_free_head=slot->next_free;
    uint32_t generation=slot->generation+1U;
    if(generation==0U) generation=1U;
    *slot=(seq_future_t){
        .due_sample=due,.occurrence_id=occurrence,.kind=kind,.owner=track,
        .note=note,.velocity=velocity,.generation=generation,.active=1U};
    ++core->future_count;
    return (seq_future_handle_t){index,generation};
}

static seq_future_t *future_resolve(seq_engine_core_t *core,seq_future_handle_t h)
{
    if((h.index>=SEQ_ENGINE_FUTURE_CAPACITY)||(core->futures[h.index].active==0U)
            ||(core->futures[h.index].generation!=h.generation)){
        ++g_seq_diag.stale_generation_count;return 0;}
    return &core->futures[h.index];
}

static void future_release(seq_engine_core_t *core,seq_future_handle_t h)
{
    seq_future_t *const slot=future_resolve(core,h);if(slot==0)return;
    slot->active=0U;slot->next_free=core->future_free_head;
    core->future_free_head=h.index;if(core->future_count!=0U)--core->future_count;
}

static void future_clear(seq_engine_core_t *core)
{
    core->future_count=0U;core->future_free_head=0U;
    for(uint16_t i=0U;i<SEQ_ENGINE_FUTURE_CAPACITY;++i){
        core->futures[i].active=0U;
        core->futures[i].next_free=(i+1U<SEQ_ENGINE_FUTURE_CAPACITY)?(uint16_t)(i+1U):UINT16_MAX;}
}

static void fx_terminal(const note_event_t *e)
{
    uint64_t due=e->sample_abs;
    if(due<g_seq_fx_start)due=g_seq_fx_start;
    if(due<g_seq_fx_end&&g_seq_fx_block->event_count<SEQ_ENGINE_EVENT_CAPACITY)
        g_seq_fx_block->events[g_seq_fx_block->event_count++]=(seq_event_t){
            .offset=(uint16_t)(due-g_seq_fx_start),
            .kind=(e->kind==NOTE_EVENT_KIND_OFF)
                ?SEQ_ENGINE_EVENT_NOTE_OFF:SEQ_ENGINE_EVENT_NOTE_ON,
            .track=e->track,.occurrence_id=e->occurrence_id,
            .note=e->note,.velocity=e->velocity};
    else future_add(g_seq_fx_core,due,(e->kind==NOTE_EVENT_KIND_OFF)
            ?SEQ_ENGINE_EVENT_NOTE_OFF:SEQ_ENGINE_EVENT_NOTE_ON,e->track,
        e->note,e->velocity,e->occurrence_id);
    if((e->kind==NOTE_EVENT_KIND_ON)
            &&(e->duration_samples!=NOTE_EVENT_DURATION_OPEN))
        future_add(g_seq_fx_core,due+(e->duration_samples?e->duration_samples:1U),
            SEQ_ENGINE_EVENT_NOTE_OFF,e->track,e->note,0U,e->occurrence_id);
}

static note_event_result_t fx_run_from(const note_event_t *source,uint8_t stage)
{
    const uint32_t started=DWT->CYCCNT;
    g_seq_fx_a[0]=*source;uint8_t count=1U;
    note_event_t *in=g_seq_fx_a,*out=g_seq_fx_b;
    for(uint8_t slot=stage;slot<NOTE_FX_SLOT_COUNT;++slot){
        uint8_t out_count=0U;
        const note_event_result_t r=note_fx_engine_seq_transform(slot,in,count,
            out,NOTE_FX_BATCH_CAPACITY,&out_count);
        if(r!=NOTE_EVENT_RESULT_ACCEPTED)return r;
        count=out_count;note_event_t*swap=in;in=out;out=swap;
        if(count==0U)return NOTE_EVENT_RESULT_ACCEPTED;}
    for(uint8_t i=0U;i<count;++i)fx_terminal(&in[i]);
    const uint32_t cycles=DWT->CYCCNT-started;
    if(cycles>g_seq_diag.fx_admission_max_cycles)
        g_seq_diag.fx_admission_max_cycles=cycles;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t fx_generated(const note_event_t *event,void *ctx)
{(void)ctx;return fx_run_from(event,event->stage);}

uint8_t seq_engine_core_submit_live(seq_engine_core_t *core,
    const note_event_t *event,uint64_t window_start,uint64_t window_end,
    seq_event_block_t *out_block)
{
    if((core==0)||(event==0)||(out_block==0)||(window_end<=window_start))
        return 0U;
    g_seq_fx_core=core;g_seq_fx_block=out_block;
    g_seq_fx_start=window_start;g_seq_fx_end=window_end;
    return (fx_run_from(event,0U)==NOTE_EVENT_RESULT_ACCEPTED)?1U:0U;
}

static uint8_t priority(uint8_t kind)
{
    if (kind == SEQ_ENGINE_EVENT_PANIC) return 0U;
    if (kind == SEQ_ENGINE_EVENT_NOTE_OFF) return 1U;
    if (kind == SEQ_ENGINE_EVENT_PARAM) return 2U;
    return 3U;
}

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

static void source_add(seq_engine_core_t *core, uint64_t first_on,
    uint64_t span_q16, uint64_t interval_q16, uint32_t gate_samples,
    uint8_t track, uint8_t note, uint8_t velocity)
{
    const uint8_t quota=(track<BRICK_ENTITY_TOP_LEVEL_COUNT)?8U:1U;
    uint8_t owned=0U;
    uint16_t target=SEQ_ENGINE_LIFETIME_CAPACITY;
    uint16_t oldest=SEQ_ENGINE_LIFETIME_CAPACITY;
    for (uint16_t i = 0U; i < SEQ_ENGINE_LIFETIME_CAPACITY; ++i)
    {
        seq_lifetime_t *const source = &core->lifetimes[i];
        if (source->active == 0U) { if(target==SEQ_ENGINE_LIFETIME_CAPACITY)target=i;continue; }
        if (source->track != track) continue;
        ++owned;
        if ((oldest==SEQ_ENGINE_LIFETIME_CAPACITY)
                ||(source->first_on_sample<core->lifetimes[oldest].first_on_sample))
            oldest=i;
    }
    if(owned>=quota)target=oldest;
    if(target<SEQ_ENGINE_LIFETIME_CAPACITY)
    {
        seq_lifetime_t *const source=&core->lifetimes[target];
        const uint16_t generation=(uint16_t)(source->generation+1U);
        *source = (seq_lifetime_t){
            .first_on_sample=first_on,.span_q16=span_q16,
            .interval_q16=interval_q16,.gate_samples=gate_samples,
            .track=track,.note=note,.velocity=velocity,.active=1U,
            .generation=generation?generation:1U};
        if(owned<quota)++core->lifetime_count;
        return;
    }
    ++core->dropped_events;
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
        const seq_lock_pattern_t *lock=&p->lock_pool[track][first+n];
        if((lock->param_flags&SEQ_ENGINE_PARAM_FLAG_NOTE_FX)==0U)continue;
        uint8_t slot,param;
        if(note_fx_state_param_map((param_id_t)(lock->param_flags&SEQ_ENGINE_PARAM_ID_MASK),
                &slot,&param)!=0U)
            state.value[slot][param]=(uint8_t)lock->value16;
    }
    (void)note_fx_state_normalize_track(&state);
    for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
        if(note_fx_engine_seq_configure(track,slot,
            state.value[slot][NOTE_FX_PARAM_COUNT-1U],state.value[slot][0],
            state.value[slot][1],state.value[slot][2])!=NOTE_EVENT_RESULT_ACCEPTED)
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
            (uint32_t)(gate ? gate : 1U),track,(uint8_t)note,(uint8_t)vel);
    }
}

static void schedule_boundary(seq_engine_core_t *core,
    const seq_pattern_t *p, uint64_t sample, uint16_t hit_mask,
    uint64_t block_start, seq_param_block_t *params)
{
    const uint32_t locks_started=DWT->CYCCNT;
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
    const uint32_t locks_cycles=DWT->CYCCNT-locks_started;
    if(locks_cycles>g_seq_diag.boundary_locks_max_cycles)
        g_seq_diag.boundary_locks_max_cycles=locks_cycles;
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

static void collect(seq_engine_core_t *core,uint64_t start,uint16_t frames,
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
    if(note_fx_engine_seq_process(start,0U,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        ++core->dropped_events;
    for(uint16_t source_index=0U;source_index<SEQ_ENGINE_LIFETIME_CAPACITY;
        ++source_index){seq_lifetime_t *const source=&core->lifetimes[source_index];
        if(source->active==0U)continue;
        while(source->next_offset_q16<source->span_q16){
            uint64_t on=source->first_on_sample
                +((source->next_offset_q16+0x8000ULL)>>16);
            if(on>=end)break;
            if(on<start)on=start;
            const uint32_t occurrence=++core->occurrence_serial;
            const note_event_t event={.sample_abs=on,
                .duration_samples=source->gate_samples,
                .source_token=occurrence,.occurrence_id=occurrence,
                .generation=p->generation?p->generation:1U,.group_id=occurrence,
                .track=source->track,.destination_id=NOTE_EVENT_DESTINATION_DEFAULT,
                .note=source->note,.velocity=source->velocity,
                .kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_STEP,
                .stage=NOTE_EVENT_STAGE_SOURCE};
            if(fx_run_from(&event,0U)!=NOTE_EVENT_RESULT_ACCEPTED)
                ++core->dropped_events;
            source->next_offset_q16+=source->interval_q16;
        }
        if(source->next_offset_q16>=source->span_q16){
            source->active=0U;if(core->lifetime_count!=0U)--core->lifetime_count;}}
    if(note_fx_engine_seq_process(start,frames,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        ++core->dropped_events;
    const uint32_t future_started=DWT->CYCCNT;
    for(uint16_t i=0U;i<SEQ_ENGINE_FUTURE_CAPACITY;++i){
        if(core->futures[i].active==0U)continue;
        const seq_future_handle_t handle={i,core->futures[i].generation};
        const seq_future_t e=core->futures[i];
        if(e.due_sample>=end)continue;
        future_release(core,handle);
        if(out->event_count>=SEQ_ENGINE_EVENT_CAPACITY)
        {
            ++core->dropped_events;
            continue;
        }
        const uint64_t due=(e.due_sample<start)?start:e.due_sample;
        out->events[out->event_count++]=(seq_event_t){
            .offset=(uint16_t)(due-start),.kind=e.kind,.track=e.owner,
            .occurrence_id=e.occurrence_id,.note=e.note,.velocity=e.velocity};}
    const uint32_t future_cycles=DWT->CYCCNT-future_started;
    if(future_cycles>g_seq_diag.scan_future_max_cycles)
        g_seq_diag.scan_future_max_cycles=future_cycles;
    for(uint16_t a=1U;a<out->event_count;++a){const seq_event_t key=out->events[a];
        uint16_t b=a;while((b!=0U)&&((out->events[b-1U].offset>key.offset)
            ||((out->events[b-1U].offset==key.offset)
            &&(priority(out->events[b-1U].kind)>priority(key.kind))))){
            out->events[b]=out->events[b-1U];--b;}out->events[b]=key;}
}

void seq_engine_core_init(seq_engine_core_t *core)
{
    if(core==0)return;
    /* Future tickets survive a core reset: clearing musical state must never
     * make a handle from the previous epoch valid again. */
    memset(core,0,offsetof(seq_engine_core_t,futures));
    future_clear(core);
    note_fx_engine_seq_init();
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
    if(core->pattern_generation!=p->generation){
        core->pattern_generation=p->generation;
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)
            if(p->track_fx_enabled[t]!=0U)
                configure_fx_step(p,t,core->play_step[t]);}
    if((p->running==0U)||(core->samples_per_step_q16==0U)){
        core->running=0U;future_clear(core);core->lifetime_count=0U;
        memset(core->lifetimes,0,sizeof(core->lifetimes));return;}core->running=1U;
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
