#include "Seq/seq_rt_pass1.h"
#include "NoteFx/note_fx_engine.h"
#include "Param/param_ids.h"
#include "Platform/memory_layout.h"
#include <limits.h>
#include <string.h>

static AUDIO_STATE_D3 note_event_t g_rt_fx_a[NOTE_FX_BATCH_CAPACITY];
static AUDIO_STATE_D3 note_event_t g_rt_fx_b[NOTE_FX_BATCH_CAPACITY];
static seq_rt_core_t *g_rt_fx_core;
static seq_rt_event_block_t *g_rt_fx_block;
static uint64_t g_rt_fx_start;
static uint64_t g_rt_fx_end;

static void pending_event_add(seq_rt_core_t *core,uint64_t due,uint8_t kind,
    uint8_t track,uint8_t note,uint8_t velocity,uint32_t occurrence)
{
    if(core->pending_count>=SEQ_RT_PENDING_CAPACITY){++core->dropped_events;return;}
    core->pending[core->pending_count++]=(seq_rt_pending_event_t){
        .due_sample=due,.occurrence_id=occurrence,.kind=kind,.track=track,
        .note=note,.velocity=velocity};
}

static void fx_terminal(const note_event_t *e)
{
    uint64_t due=e->sample_abs;
    if(due<g_rt_fx_start)due=g_rt_fx_start;
    if(due<g_rt_fx_end&&g_rt_fx_block->event_count<SEQ_RT_EVENT_CAPACITY)
        g_rt_fx_block->events[g_rt_fx_block->event_count++]=(seq_rt_event_t){
            .offset=(uint16_t)(due-g_rt_fx_start),.kind=SEQ_RT_EVENT_NOTE_ON,
            .track=e->track,.occurrence_id=e->occurrence_id,
            .note=e->note,.velocity=e->velocity};
    else pending_event_add(g_rt_fx_core,due,SEQ_RT_EVENT_NOTE_ON,e->track,
        e->note,e->velocity,e->occurrence_id);
    if(e->duration_samples!=NOTE_EVENT_DURATION_OPEN)
        pending_event_add(g_rt_fx_core,due+(e->duration_samples?e->duration_samples:1U),
            SEQ_RT_EVENT_NOTE_OFF,e->track,e->note,0U,e->occurrence_id);
}

static note_event_result_t fx_run_from(const note_event_t *source,uint8_t stage)
{
    g_rt_fx_a[0]=*source;uint8_t count=1U;
    note_event_t *in=g_rt_fx_a,*out=g_rt_fx_b;
    for(uint8_t slot=stage;slot<NOTE_FX_SLOT_COUNT;++slot){
        uint8_t out_count=0U;
        const note_event_result_t r=note_fx_engine_rt_transform(slot,in,count,
            out,NOTE_FX_BATCH_CAPACITY,&out_count);
        if(r!=NOTE_EVENT_RESULT_ACCEPTED)return r;
        count=out_count;note_event_t*swap=in;in=out;out=swap;
        if(count==0U)return NOTE_EVENT_RESULT_ACCEPTED;}
    for(uint8_t i=0U;i<count;++i)fx_terminal(&in[i]);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t fx_generated(const note_event_t *event,void *ctx)
{(void)ctx;return fx_run_from(event,event->stage);}

static uint8_t priority(uint8_t kind)
{
    if (kind == SEQ_RT_EVENT_PANIC) return 0U;
    if (kind == SEQ_RT_EVENT_NOTE_OFF) return 1U;
    if (kind == SEQ_RT_EVENT_PARAM) return 2U;
    return 3U;
}

static const seq_play_item_t *step_item(const seq_rt_projection_t *p,
    uint8_t track, uint8_t step, uint8_t voice)
{
    if (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
        return &p->top_play[track][step].items[voice];
    if ((track < SEQ_LANE_CAPACITY) && (voice == 0U))
        return &p->child_play[track - BRICK_ENTITY_TOP_LEVEL_COUNT][step];
    return 0;
}

static int16_t play_value(const seq_rt_projection_t *p, uint8_t track,
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

static uint64_t first_on(const seq_rt_projection_t *p, uint8_t track,
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

static void source_add(seq_rt_core_t *core, uint64_t first_on,
    uint64_t span_q16, uint64_t interval_q16, uint32_t gate_samples,
    uint8_t track, uint8_t note, uint8_t velocity)
{
    for (uint16_t i = 0U; i < SEQ_RT_SOURCE_CAPACITY; ++i)
    {
        seq_rt_note_source_t *const source = &core->sources[i];
        if (source->active != 0U) continue;
        *source = (seq_rt_note_source_t){
            .first_on_sample=first_on,.span_q16=span_q16,
            .interval_q16=interval_q16,.gate_samples=gate_samples,
            .track=track,.note=note,.velocity=velocity,.active=1U};
        ++core->source_count;
        return;
    }
    ++core->dropped_events;
}

static uint8_t next_step(const seq_rt_projection_t *p, uint8_t track,
    uint8_t current)
{
    const uint8_t length = p->track_length[track] ? p->track_length[track] : 1U;
    return ((uint8_t)(current + 1U) < length) ? (uint8_t)(current + 1U) : 0U;
}

static void configure_fx_step(const seq_rt_projection_t *p,uint8_t track,
    uint8_t step)
{
    note_fx_track_state_t state=p->note_fx[track];
    const uint16_t first=p->lock_first[track][step];
    const uint8_t count=p->steps[track][step].lock_count;
    for(uint8_t n=0U;n<count;++n){
        const seq_rt_lock_projection_t *lock=&p->lock_pool[first+n];
        if((lock->param_flags&SEQ_RT_PARAM_FLAG_NOTE_FX)==0U)continue;
        uint8_t slot,param;
        if(note_fx_state_param_map((param_id_t)(lock->param_flags&SEQ_RT_PARAM_ID_MASK),
                &slot,&param)!=0U)
            state.value[slot][param]=(uint8_t)lock->value16;
    }
    (void)note_fx_state_normalize_track(&state);
    for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
        if(note_fx_engine_rt_configure(track,slot,
            state.value[slot][NOTE_FX_PARAM_COUNT-1U],state.value[slot][0],
            state.value[slot][1],state.value[slot][2])!=NOTE_EVENT_RESULT_ACCEPTED)
            return;
}

static void schedule_step(seq_rt_core_t *core, const seq_rt_projection_t *p,
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

static void schedule_boundary(seq_rt_core_t *core,
    const seq_rt_projection_t *p, uint64_t sample, uint16_t hit_mask,
    uint64_t block_start, seq_rt_param_block_t *params)
{
    for (uint8_t track=0U; track<SEQ_LANE_CAPACITY; ++track) {
        if ((hit_mask & (uint16_t)(1U << track)) == 0U) continue;
        core->comparable_tracks |= (uint16_t)(1U << track);
        const uint8_t step=core->play_step[track];
        if(p->track_rt_fx_direct[track]!=0U)configure_fx_step(p,track,step);
        if ((p->track_rt_plock_direct[track] != 0U)
                && ((core->plock_fault_tracks & (uint16_t)(1U << track)) == 0U))
        {
            const uint16_t first=p->lock_first[track][step];
            const uint8_t count=p->steps[track][step].lock_count;
            uint8_t restores=0U;
            for(uint8_t a=0U;a<core->active_lock_count[track];++a){
                if((core->active_locks[track][a].param_flags
                        &SEQ_RT_PARAM_FLAG_NOTE_FX)!=0U)continue;
                uint8_t found=0U;
                for(uint8_t n=0U;n<count;++n)
                    if((p->lock_pool[first+n].param_flags
                            &SEQ_RT_PARAM_FLAG_NOTE_FX)==0U
                        &&(p->lock_pool[first+n].param_flags&SEQ_RT_PARAM_ID_MASK)
                        ==(core->active_locks[track][a].param_flags&SEQ_RT_PARAM_ID_MASK))
                        {found=1U;break;}
                if(found==0U)++restores;
            }
            if ((uint32_t)params->event_count+restores+count
                    > SEQ_RT_PARAM_EVENT_CAPACITY)
                core->plock_fault_tracks|=(uint16_t)(1U<<track);
            else {
                for(uint8_t a=0U;a<core->active_lock_count[track];++a){
                    const seq_rt_active_lock_t active=core->active_locks[track][a];
                    if((active.param_flags&SEQ_RT_PARAM_FLAG_NOTE_FX)!=0U)continue;
                    uint8_t found=0U;
                    for(uint8_t n=0U;n<count;++n)
                        if((p->lock_pool[first+n].param_flags
                                &SEQ_RT_PARAM_FLAG_NOTE_FX)==0U
                            &&(p->lock_pool[first+n].param_flags&SEQ_RT_PARAM_ID_MASK)
                            ==(active.param_flags&SEQ_RT_PARAM_ID_MASK)){found=1U;break;}
                    if(found==0U)params->events[params->event_count++]=(seq_rt_param_event_t){
                        .offset=(uint16_t)(sample-block_start),
                        .param_id=(uint16_t)(active.param_flags&SEQ_RT_PARAM_ID_MASK),
                        .value16=active.base_value16,.track=track,
                        .semantic=((active.param_flags&SEQ_RT_PARAM_FLAG_CLEARABLE)!=0U)
                            ?SEQ_RT_PARAM_CLEAR_TEMP:SEQ_RT_PARAM_RESTORE_BASE};
                }
                uint8_t active_write=0U;
                for(uint8_t n=0U;n<count;++n){
                    const seq_rt_lock_projection_t *lock=&p->lock_pool[first+n];
                    if((lock->param_flags&SEQ_RT_PARAM_FLAG_NOTE_FX)!=0U)continue;
                    params->events[params->event_count++]=(seq_rt_param_event_t){
                        .offset=(uint16_t)(sample-block_start),
                        .param_id=(uint16_t)(lock->param_flags&SEQ_RT_PARAM_ID_MASK),
                        .value16=lock->value16,.track=track,.semantic=SEQ_RT_PARAM_TEMP};
                    core->active_locks[track][active_write++]=(seq_rt_active_lock_t){
                        .param_flags=lock->param_flags,.base_value16=lock->base_value16};
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

static uint16_t advance(seq_rt_core_t *core,const seq_rt_projection_t *p)
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

static void collect(seq_rt_core_t *core,uint64_t start,uint16_t frames,
    const seq_rt_projection_t *p,seq_rt_event_block_t *out)
{
    const uint64_t end=start+frames;
    g_rt_fx_core=core;g_rt_fx_block=out;g_rt_fx_start=start;g_rt_fx_end=end;
    uint32_t pattern[NOTE_FX_TRACK_COUNT];
    for(uint8_t t=0U;t<NOTE_FX_TRACK_COUNT;++t)
        pattern[t]=(uint32_t)core->play_step[t]<<16U;
    const uint64_t step_samples=p->samples_per_step_q16?p->samples_per_step_q16:1U;
    const uint64_t delta_q16=((start<<16U)>=core->step_sample_q16)
        ?(((start<<16U)-core->step_sample_q16)<<16U)/step_samples:0U;
    const uint64_t transport=((uint64_t)core->step_serial[0]<<16U)+delta_q16;
    if(note_fx_engine_rt_process(start,0U,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        ++core->dropped_events;
    for(uint16_t source_index=0U;source_index<SEQ_RT_SOURCE_CAPACITY;
        ++source_index){seq_rt_note_source_t *const source=&core->sources[source_index];
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
            source->active=0U;if(core->source_count!=0U)--core->source_count;}}
    if(note_fx_engine_rt_process(start,frames,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        ++core->dropped_events;
    uint16_t i=0U;
    while(i<core->pending_count){const seq_rt_pending_event_t e=core->pending[i];
        if(e.due_sample>=end){++i;continue;}
        core->pending[i]=core->pending[--core->pending_count];
        if(out->event_count>=SEQ_RT_EVENT_CAPACITY)
        {
            ++core->dropped_events;
            continue;
        }
        const uint64_t due=(e.due_sample<start)?start:e.due_sample;
        out->events[out->event_count++]=(seq_rt_event_t){
            .offset=(uint16_t)(due-start),.kind=e.kind,.track=e.track,
            .occurrence_id=e.occurrence_id,.note=e.note,.velocity=e.velocity};}
    for(uint16_t a=1U;a<out->event_count;++a){const seq_rt_event_t key=out->events[a];
        uint16_t b=a;while((b!=0U)&&((out->events[b-1U].offset>key.offset)
            ||((out->events[b-1U].offset==key.offset)
            &&(priority(out->events[b-1U].kind)>priority(key.kind))))){
            out->events[b]=out->events[b-1U];--b;}out->events[b]=key;}
}

void seq_rt_core_init(seq_rt_core_t *core)
{
    if(core==0)return;
    memset(core,0,sizeof(*core));
    note_fx_engine_rt_init();
    for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)for(uint8_t v=0U;
        v<SEQ_PLAY_MAX_CAPACITY;++v)core->voice_scheduled_serial[t][v]=UINT32_MAX;
}

void seq_rt_core_process_block(seq_rt_core_t *core,uint64_t start,uint16_t frames,
    const seq_rt_projection_t *p,seq_rt_event_block_t *out,
    seq_rt_param_block_t *params)
{
    if((core==0)||(out==0)||(params==0))return;
    out->start_sample=start;out->frames=frames;out->generation=p?p->generation:0U;
    out->comparable_tracks=core->comparable_tracks;
    out->rt_note_tracks=0U;
    out->rt_plock_tracks=0U;
    out->event_count=0U;if((p==0)||(frames==0U))return;
    params->start_sample=start;params->frames=frames;
    params->generation=p->generation;params->event_count=0U;
    if((core->initialized==0U)||(core->transport_epoch!=p->transport_epoch)){
        seq_rt_core_init(core);core->initialized=1U;core->transport_epoch=p->transport_epoch;
        core->running=p->running;core->step_sample_q16=p->seed_step_sample_q16;
        core->samples_per_step_q16=p->samples_per_step_q16;
        memcpy(core->play_step,p->seed_play_step,sizeof(core->play_step));
        memcpy(core->track_div_phase,p->seed_div_phase,sizeof(core->track_div_phase));
        memcpy(core->track_swing_phase,p->seed_swing_phase,sizeof(core->track_swing_phase));
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t){
            const uint8_t s=core->play_step[t];const uint8_t count=p->steps[t][s].lock_count;
            const uint16_t first=p->lock_first[t][s];
            if((p->track_rt_plock_direct[t]!=0U)&&(count<=SEQ_STEP_MAX_LOCKS)){
                uint8_t write=0U;
                for(uint8_t n=0U;n<count;++n){
                    if((p->lock_pool[first+n].param_flags
                            &SEQ_RT_PARAM_FLAG_NOTE_FX)!=0U)continue;
                    core->active_locks[t][write++]=(seq_rt_active_lock_t){
                        .param_flags=p->lock_pool[first+n].param_flags,
                        .base_value16=p->lock_pool[first+n].base_value16};}
                core->active_lock_count[t]=write;}
            if(p->track_rt_fx_direct[t]!=0U)configure_fx_step(p,t,s);}
        }
    core->samples_per_step_q16=p->samples_per_step_q16;
    if(core->projection_generation!=p->generation){
        core->projection_generation=p->generation;
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)
            if(p->track_rt_fx_direct[t]!=0U)
                configure_fx_step(p,t,core->play_step[t]);}
    if((p->running==0U)||(core->samples_per_step_q16==0U)){
        core->running=0U;core->pending_count=0U;core->source_count=0U;
        memset(core->sources,0,sizeof(core->sources));return;}core->running=1U;
    const uint32_t dropped_before=core->dropped_events;
    const uint64_t begin_q16=start<<16,end_q16=(start+frames)<<16;
    uint64_t next=core->step_sample_q16+core->samples_per_step_q16;
    while(next<end_q16){const uint16_t hits=advance(core,p);core->step_sample_q16=next;
        if(next>=begin_q16)schedule_boundary(core,p,(next+0x8000ULL)>>16,hits,
            start,params);
        next=core->step_sample_q16+core->samples_per_step_q16;}
    out->comparable_tracks=core->comparable_tracks;
    collect(core,start,frames,p,out);
    if(core->dropped_events!=dropped_before){core->event_faulted=1U;return;}
    if(core->event_faulted!=0U)return;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track)
    {
        const uint16_t bit=(uint16_t)(1U<<track);
        if((p->track_rt_plock_direct[track]!=0U)
                &&((core->plock_fault_tracks&bit)==0U)
                &&((core->comparable_tracks&bit)!=0U))
            out->rt_plock_tracks|=bit;
        if((p->track_rt_note_direct[track]!=0U)
                &&(p->track_muted[track]==0U)
                &&((out->rt_plock_tracks&bit)!=0U))
            out->rt_note_tracks|=bit;
    }
}
