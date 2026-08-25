#include "Audio/audio_fx_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Audio/fx_audio_drift.h"
#include "Audio/fx_audio_drive.h"
#include "Audio/fx_audio_fold.h"
#include "Audio/fx_audio_lofi.h"
#include "Audio/fx_audio_point.h"
#include "Audio/fx_audio_ring.h"
#include "Audio/fx_audio_sub.h"
#include "Audio/fx_audio_vibe.h"
#include "Audio/mixer.h"
#include "Board/board_audio_format.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

#define AUDIO_FX_OWNER_COUNT BRICK_ENTITY_TOP_LEVEL_COUNT

typedef union
{
    fx_audio_lofi_state_t lofi;
    fx_audio_fold_state_t fold;
    fx_audio_drive_state_t drive;
    fx_audio_point_state_t point;
    fx_audio_sub_state_t sub;
    fx_audio_ring_state_t ring;
    fx_audio_vibe_state_t vibe;
    fx_audio_drift_state_t drift;
} audio_fx_runtime_dsp_t;

typedef struct { uint8_t model; float p1, p2, p3; } audio_fx_audio_config_t;
typedef struct audio_fx_runtime_slot audio_fx_runtime_slot_t;
typedef float (*audio_fx_mono_sample_fn)(audio_fx_runtime_slot_t *, void *, float);
typedef void (*audio_fx_stereo_sample_fn)(audio_fx_runtime_slot_t *, void *, float *, float *);
typedef void (*audio_fx_stereo_block_fn)(audio_fx_runtime_slot_t *, void *, float *, float *, uint32_t);
struct audio_fx_runtime_slot
{
    audio_fx_audio_config_t config;
    audio_fx_runtime_dsp_t dsp;
    audio_fx_mono_sample_fn prepared_mono;
};
typedef struct
{
    audio_fx_runtime_slot_t *state;
    void *history;
    audio_fx_mono_sample_fn mono_sample;
    audio_fx_stereo_sample_fn stereo_sample;
    audio_fx_stereo_block_fn stereo_block;
} audio_fx_runtime_plan_slot_t;
typedef struct audio_fx_runtime_plan audio_fx_runtime_plan_t;
typedef void (*audio_fx_plan_sample_fn)(audio_fx_runtime_plan_t *, float *, float *);
struct audio_fx_runtime_plan
{
    audio_fx_runtime_plan_slot_t slot[AUDIO_FX_SLOT_COUNT];
    audio_fx_runtime_plan_slot_t *before_filter[AUDIO_FX_SLOT_COUNT];
    audio_fx_runtime_plan_slot_t *after_filter[AUDIO_FX_SLOT_COUNT];
    uint8_t active_mask;
    uint8_t before_filter_count;
    uint8_t after_filter_count;
    uint8_t filter_pos;
    uint8_t order;
    uint8_t mode[AUDIO_FX_SLOT_COUNT];
    audio_fx_plan_sample_fn after_filter_sample;
};

_Static_assert(sizeof(audio_fx_runtime_dsp_t) == 88U,
               "Audio FX DSP union size changed");
_Static_assert(sizeof(audio_fx_runtime_slot_t) == 108U,
               "Audio FX slot size changed");

AUDIO_HOT static audio_fx_runtime_slot_t
    g_audio_fx_runtime[AUDIO_FX_OWNER_COUNT][AUDIO_FX_SLOT_COUNT];
AUDIO_HOT static audio_fx_runtime_plan_t g_audio_fx_plan[AUDIO_FX_OWNER_COUNT];
D3_IPC static volatile uint8_t g_audio_fx_filter_pos_status[AUDIO_FX_OWNER_COUNT];
_Static_assert(sizeof(g_audio_fx_runtime) == 1728U,
               "Audio FX light-state bank size changed");
_Static_assert(sizeof(g_audio_fx_plan) == 544U,
               "Audio FX structural-plan bank size changed");

AUDIO_WARM static fx_audio_vibe_history_t
    g_audio_fx_vibe_history[AUDIO_FX_OWNER_COUNT];
AUDIO_HISTORY_SDRAM static fx_audio_drift_history_t
    g_audio_fx_drift_history[AUDIO_FX_OWNER_COUNT][2];
_Static_assert(sizeof(g_audio_fx_vibe_history) == 65536U,
               "VIBE D1 pool size changed");
_Static_assert(sizeof(g_audio_fx_drift_history) == 65536U,
               "DRIFT SDRAM pool size changed");

static float clamp01(float v){return v<=0.0f?0.0f:(v>=1.0f?1.0f:v);}
static float clamp127(float v){return v<=0.0f?0.0f:(v>=127.0f?127.0f:v);}

static uint8_t audio_fx_owner(brick_entity_id_t entity, uint8_t *out)
{
    if ((out == NULL) || (entity >= AUDIO_FX_OWNER_COUNT)) return 0U;
    *out = entity;
    return 1U;
}

static uint8_t valid_model(float value)
{
    const uint8_t m=(value<0.0f)?0U:(uint8_t)(value+0.5f);
    return (m==AUDIO_FX_MODEL_LOFI||m==AUDIO_FX_MODEL_FOLD
        ||m==AUDIO_FX_MODEL_DRIVE||m==AUDIO_FX_MODEL_POINT
        ||m==AUDIO_FX_MODEL_SUB||m==AUDIO_FX_MODEL_SUB_LIGHT
        ||m==AUDIO_FX_MODEL_RING||m==AUDIO_FX_MODEL_VIBE
        ||m==AUDIO_FX_MODEL_DRIFT)?m:AUDIO_FX_MODEL_OFF;
}

uint8_t audio_fx_runtime_param_slot(param_id_t id, audio_fx_slot_t *out)
{
    audio_fx_slot_t slot;
    switch (id)
    {
        case PARAM_AUDIO_FX_P1: case PARAM_AUDIO_FX_P2:
        case PARAM_AUDIO_FX_P3: case PARAM_AUDIO_FX_MODEL:
            slot=AUDIO_FX_SLOT_A; break;
        case PARAM_AUDIO_FX_B_P1: case PARAM_AUDIO_FX_B_P2:
        case PARAM_AUDIO_FX_B_P3: case PARAM_AUDIO_FX_B_MODEL:
            slot=AUDIO_FX_SLOT_B; break;
        default: return 0U;
    }
    if (out != NULL) *out=slot;
    return 1U;
}

static uint8_t routing_param(param_id_t id)
{
    return (uint8_t)((id == PARAM_AUDIO_FX_FILTER_POS)
        || (id == PARAM_AUDIO_FX_ORDER)
        || (id == PARAM_AUDIO_FX_MODE_A)
        || (id == PARAM_AUDIO_FX_MODE_B));
}

uint8_t audio_fx_runtime_is_group_level_param(param_id_t id)
{
    return (uint8_t)((id == PARAM_GROUP_FX_A_LEVEL)
        || (id == PARAM_GROUP_FX_B_LEVEL));
}

static uint8_t param_kind(param_id_t id)
{
    switch(id)
    {
        case PARAM_AUDIO_FX_P1: case PARAM_AUDIO_FX_B_P1:return 1U;
        case PARAM_AUDIO_FX_P2: case PARAM_AUDIO_FX_B_P2:return 2U;
        case PARAM_AUDIO_FX_P3: case PARAM_AUDIO_FX_B_P3:return 3U;
        default:return 4U;
    }
}

static void reset_light_state(audio_fx_runtime_slot_t *r)
{
    memset(&r->dsp,0,sizeof(r->dsp));
    switch(r->config.model)
    {
        case AUDIO_FX_MODEL_LOFI:fx_audio_lofi_reset(&r->dsp.lofi);break;
        case AUDIO_FX_MODEL_FOLD:fx_audio_fold_reset(&r->dsp.fold);break;
        case AUDIO_FX_MODEL_DRIVE:fx_audio_drive_reset(&r->dsp.drive);break;
        case AUDIO_FX_MODEL_POINT:fx_audio_point_reset(&r->dsp.point);break;
        case AUDIO_FX_MODEL_SUB:case AUDIO_FX_MODEL_SUB_LIGHT:
            fx_audio_sub_reset(&r->dsp.sub);break;
        case AUDIO_FX_MODEL_RING:fx_audio_ring_reset(&r->dsp.ring);break;
        case AUDIO_FX_MODEL_DRIFT:
            r->dsp.drift.delay=r->dsp.drift.delay_target=4.8f;break;
        default:break;
    }
}

static void prepare(audio_fx_runtime_slot_t *r)
{
    switch(r->config.model)
    {
        case AUDIO_FX_MODEL_LOFI:
            fx_audio_lofi_set_engine(&r->dsp.lofi,(fx_audio_lofi_engine_t)
                audio_fx_lofi_model_index_from_control((uint8_t)(clamp127(r->config.p3)+0.5f)));
            fx_audio_lofi_prepare(&r->dsp.lofi,r->config.p1,r->config.p2);break;
        case AUDIO_FX_MODEL_FOLD:fx_audio_fold_prepare(&r->dsp.fold,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f);break;
        case AUDIO_FX_MODEL_DRIVE:fx_audio_drive_prepare(&r->dsp.drive,r->config.p1,r->config.p2,clamp127(r->config.p3));break;
        case AUDIO_FX_MODEL_POINT:fx_audio_point_prepare(&r->dsp.point,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f,(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
        case AUDIO_FX_MODEL_SUB:case AUDIO_FX_MODEL_SUB_LIGHT:fx_audio_sub_prepare(&r->dsp.sub,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f,(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
        case AUDIO_FX_MODEL_RING:fx_audio_ring_prepare(&r->dsp.ring,r->config.p1,r->config.p2,clamp127(r->config.p3),(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
        case AUDIO_FX_MODEL_VIBE:fx_audio_vibe_prepare(&r->dsp.vibe,.01f+11.99f*clamp01(r->config.p1),clamp01(r->config.p2),clamp127(r->config.p3)/127.0f);break;
        case AUDIO_FX_MODEL_DRIFT:
        {
            const float c=clamp01(r->config.p2)*127.0f;
            const float fb=(c<=25.0f)?(.2f*c/25.0f):(.2f+(FX_AUDIO_DRIFT_FEEDBACK_MAX-.2f)*(c-25.0f)/102.0f);
            fx_audio_drift_set_delay(&r->dsp.drift,clamp01(r->config.p1));
            fx_audio_drift_set_feedback(&r->dsp.drift,fb);break;
        }
        default:break;
    }
}

static float mono_sample_lofi(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_lofi_process_mono_sample(&r->dsp.lofi,x);}
static float mono_sample_fold(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_fold_process_mono_sample(&r->dsp.fold,x);}
static float mono_sample_drive(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_drive_process_sample(&r->dsp.drive,x);}
static float mono_sample_point(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_point_process_mono_sample(&r->dsp.point,x);}
static float mono_sample_sub(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_sub_process_mono_sample(&r->dsp.sub,x);}
static float mono_sample_sub_light(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_sub_light_process_mono_sample(&r->dsp.sub,x);}
static float mono_sample_ring(audio_fx_runtime_slot_t*r,void*h,float x){(void)h;return fx_audio_ring_process_mono_sample(&r->dsp.ring,x);}
static float mono_sample_vibe(audio_fx_runtime_slot_t*r,void*h,float x){return x+fx_audio_vibe_process_wet_mono_sample(&r->dsp.vibe,(fx_audio_vibe_history_t*)h,x);}
static float mono_sample_drift(audio_fx_runtime_slot_t*r,void*h,float x){return fx_audio_drift_process_mono_sample(&r->dsp.drift,(fx_audio_drift_history_t*)h,x,r->dsp.drift.delay_target-r->dsp.drift.delay)*.5f;}
static void stereo_sample_lofi(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;fx_audio_lofi_process_stereo_sample(&r->dsp.lofi,l,rr);}
static void stereo_sample_fold(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;*l=fx_audio_fold_process_mono_sample(&r->dsp.fold,*l);*rr=fx_audio_fold_process_mono_sample(&r->dsp.fold,*rr);}
static void stereo_sample_drive(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;*l=fx_audio_drive_process_sample(&r->dsp.drive,*l);*rr=fx_audio_drive_process_sample(&r->dsp.drive,*rr);}
static void stereo_sample_point(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;fx_audio_point_process_stereo_sample(&r->dsp.point,l,rr);}
static void stereo_sample_sub(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;fx_audio_sub_process_stereo_sample(&r->dsp.sub,l,rr);}
static void stereo_sample_sub_light(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;fx_audio_sub_light_process_stereo_sample(&r->dsp.sub,l,rr);}
static void stereo_sample_ring(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){(void)h;fx_audio_ring_process_stereo_sample(&r->dsp.ring,l,rr);}
static void stereo_sample_vibe(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){const float dl=*l,dr=*rr;fx_audio_vibe_process_wet_stereo_sample(&r->dsp.vibe,(fx_audio_vibe_history_t*)h,l,rr);*l+=dl;*rr+=dr;}
static void stereo_sample_drift(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr){fx_audio_drift_history_t*const hl=(fx_audio_drift_history_t*)h;fx_audio_drift_process_dual_mono_stereo(&r->dsp.drift,hl,hl+1,l,rr,1U);}

static audio_fx_stereo_sample_fn stereo_sample_kernel(uint8_t model)
{
    switch(model)
    {
        case AUDIO_FX_MODEL_LOFI:return stereo_sample_lofi;
        case AUDIO_FX_MODEL_FOLD:return stereo_sample_fold;
        case AUDIO_FX_MODEL_DRIVE:return stereo_sample_drive;
        case AUDIO_FX_MODEL_POINT:return stereo_sample_point;
        case AUDIO_FX_MODEL_SUB:return stereo_sample_sub;
        case AUDIO_FX_MODEL_SUB_LIGHT:return stereo_sample_sub_light;
        case AUDIO_FX_MODEL_RING:return stereo_sample_ring;
        case AUDIO_FX_MODEL_VIBE:return stereo_sample_vibe;
        case AUDIO_FX_MODEL_DRIFT:return stereo_sample_drift;
        default:return NULL;
    }
}

static void stereo_block_lofi(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;fx_audio_lofi_process_stereo(&r->dsp.lofi,l,rr,n);}
static void stereo_block_fold(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;fx_audio_fold_process_stereo(&r->dsp.fold,l,rr,n);}
static void stereo_block_drive(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;fx_audio_drive_process_stereo(&r->dsp.drive,l,rr,n);}
static void stereo_block_point(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;fx_audio_point_process_stereo(&r->dsp.point,l,rr,n);}
static void stereo_block_sub(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;for(uint32_t i=0U;i<n;++i)fx_audio_sub_process_stereo_sample(&r->dsp.sub,&l[i],&rr[i]);}
static void stereo_block_sub_light(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;for(uint32_t i=0U;i<n;++i)fx_audio_sub_light_process_stereo_sample(&r->dsp.sub,&l[i],&rr[i]);}
static void stereo_block_ring(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){(void)h;for(uint32_t i=0U;i<n;++i)fx_audio_ring_process_stereo_sample(&r->dsp.ring,&l[i],&rr[i]);}
static void stereo_block_vibe(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){for(uint32_t i=0U;i<n;++i)stereo_sample_vibe(r,h,&l[i],&rr[i]);}
static void stereo_block_drift(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){fx_audio_drift_history_t*const hl=(fx_audio_drift_history_t*)h;fx_audio_drift_process_dual_mono_stereo(&r->dsp.drift,hl,hl+1,l,rr,n);}

static audio_fx_stereo_block_fn stereo_block_kernel(uint8_t model)
{
    switch(model)
    {
        case AUDIO_FX_MODEL_LOFI:return stereo_block_lofi;
        case AUDIO_FX_MODEL_FOLD:return stereo_block_fold;
        case AUDIO_FX_MODEL_DRIVE:return stereo_block_drive;
        case AUDIO_FX_MODEL_POINT:return stereo_block_point;
        case AUDIO_FX_MODEL_SUB:return stereo_block_sub;
        case AUDIO_FX_MODEL_SUB_LIGHT:return stereo_block_sub_light;
        case AUDIO_FX_MODEL_RING:return stereo_block_ring;
        case AUDIO_FX_MODEL_VIBE:return stereo_block_vibe;
        case AUDIO_FX_MODEL_DRIFT:return stereo_block_drift;
        default:return NULL;
    }
}

static void sample_chain_none(audio_fx_runtime_plan_t*p,float*l,float*r){(void)p;(void)l;(void)r;}
static void sample_chain_one(audio_fx_runtime_plan_t*p,float*l,float*r){audio_fx_runtime_plan_slot_t*s=p->after_filter[0];s->stereo_sample(s->state,s->history,l,r);}
static void sample_chain_two(audio_fx_runtime_plan_t*p,float*l,float*r){audio_fx_runtime_plan_slot_t*a=p->after_filter[0];audio_fx_runtime_plan_slot_t*b=p->after_filter[1];a->stereo_sample(a->state,a->history,l,r);b->stereo_sample(b->state,b->history,l,r);}

static audio_fx_mono_sample_fn mono_sample_kernel(uint8_t model)
{
    switch(model)
    {
        case AUDIO_FX_MODEL_LOFI:return mono_sample_lofi;
        case AUDIO_FX_MODEL_FOLD:return mono_sample_fold;
        case AUDIO_FX_MODEL_DRIVE:return mono_sample_drive;
        case AUDIO_FX_MODEL_POINT:return mono_sample_point;
        case AUDIO_FX_MODEL_SUB:return mono_sample_sub;
        case AUDIO_FX_MODEL_SUB_LIGHT:return mono_sample_sub_light;
        case AUDIO_FX_MODEL_RING:return mono_sample_ring;
        case AUDIO_FX_MODEL_VIBE:return mono_sample_vibe;
        case AUDIO_FX_MODEL_DRIFT:return mono_sample_drift;
        default:return NULL;
    }
}

static void spatial_sample_mono(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr)
{
    const float y=r->prepared_mono(r,h,(*l+*rr)*.5f);*l=y;*rr=y;
}
static void spatial_sample_mid(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr)
{
    const float m=(*l+*rr)*.5f,s=(*l-*rr)*.5f;
    const float y=r->prepared_mono(r,h,m);*l=y+s;*rr=y-s;
}
static void spatial_sample_side(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr)
{
    const float m=(*l+*rr)*.5f,s=(*l-*rr)*.5f;
    const float y=r->prepared_mono(r,h,s);*l=m+y;*rr=m-y;
}
static void spatial_block_mono(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){for(uint32_t i=0U;i<n;++i)spatial_sample_mono(r,h,&l[i],&rr[i]);}
static void spatial_block_mid(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){for(uint32_t i=0U;i<n;++i)spatial_sample_mid(r,h,&l[i],&rr[i]);}
static void spatial_block_side(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){for(uint32_t i=0U;i<n;++i)spatial_sample_side(r,h,&l[i],&rr[i]);}
static void drift_block_mono(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){const float inc=(r->dsp.drift.delay_target-r->dsp.drift.delay)/(float)n;for(uint32_t i=0U;i<n;++i){const float y=fx_audio_drift_process_mono_sample(&r->dsp.drift,(fx_audio_drift_history_t*)h,(l[i]+rr[i])*.5f,inc)*.5f;l[i]=y;rr[i]=y;}r->dsp.drift.delay=r->dsp.drift.delay_target;}
static void drift_block_mid(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){const float inc=(r->dsp.drift.delay_target-r->dsp.drift.delay)/(float)n;for(uint32_t i=0U;i<n;++i){const float m=(l[i]+rr[i])*.5f,s=(l[i]-rr[i])*.5f,y=fx_audio_drift_process_mono_sample(&r->dsp.drift,(fx_audio_drift_history_t*)h,m,inc)*.5f;l[i]=y+s;rr[i]=y-s;}r->dsp.drift.delay=r->dsp.drift.delay_target;}
static void drift_block_side(audio_fx_runtime_slot_t*r,void*h,float*l,float*rr,uint32_t n){const float inc=(r->dsp.drift.delay_target-r->dsp.drift.delay)/(float)n;for(uint32_t i=0U;i<n;++i){const float m=(l[i]+rr[i])*.5f,s=(l[i]-rr[i])*.5f,y=fx_audio_drift_process_mono_sample(&r->dsp.drift,(fx_audio_drift_history_t*)h,s,inc)*.5f;l[i]=m+y;rr[i]=m-y;}r->dsp.drift.delay=r->dsp.drift.delay_target;}

static audio_fx_stereo_sample_fn spatial_sample_kernel(uint8_t model,uint8_t mode)
{
    if(model==AUDIO_FX_MODEL_OFF)return NULL;
    switch(mode)
    {
        case AUDIO_FX_SPATIAL_MONO:return spatial_sample_mono;
        case AUDIO_FX_SPATIAL_MID:return spatial_sample_mid;
        case AUDIO_FX_SPATIAL_SIDE:return spatial_sample_side;
        default:return stereo_sample_kernel(model);
    }
}

static audio_fx_stereo_block_fn spatial_block_kernel(uint8_t model,uint8_t mode)
{
    if(model==AUDIO_FX_MODEL_OFF)return NULL;
    switch(mode)
    {
        case AUDIO_FX_SPATIAL_MONO:return(model==AUDIO_FX_MODEL_DRIFT)?drift_block_mono:spatial_block_mono;
        case AUDIO_FX_SPATIAL_MID:return(model==AUDIO_FX_MODEL_DRIFT)?drift_block_mid:spatial_block_mid;
        case AUDIO_FX_SPATIAL_SIDE:return(model==AUDIO_FX_MODEL_DRIFT)?drift_block_side:spatial_block_side;
        default:return stereo_block_kernel(model);
    }
}

static uint8_t filter_is_per_voice(uint8_t owner)
{
    audio_binding_snapshot_t s;
    if(!audio_note_engine_adapter_snapshot_read(owner,&s)
            ||s.binding.bind_state!=TRACK_RUNTIME_BIND_BOUND)return 0U;
    if((s.flags&AUDIO_RUNTIME_FLAG_GROUP_MASTER)!=0U)return 1U;
    if(s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER
            &&s.type==(uint8_t)TRACK_RUNTIME_TYPE_MULTI)return 1U;
    return (uint8_t)((s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
            &&(s.configured_voice_count>1U));
}

static void rebuild_plan(uint8_t owner)
{
    audio_fx_runtime_plan_t *const plan=&g_audio_fx_plan[owner];
    plan->active_mask=0U;
    plan->before_filter_count=0U;
    plan->after_filter_count=0U;
    for(uint8_t si=0U;si<AUDIO_FX_SLOT_COUNT;++si)
    {
        audio_fx_runtime_slot_t *const r=&g_audio_fx_runtime[owner][si];
        audio_fx_runtime_plan_slot_t *const p=&plan->slot[si];
        p->state=r;
        p->history=(r->config.model==AUDIO_FX_MODEL_VIBE)?(void*)&g_audio_fx_vibe_history[owner]
            :(r->config.model==AUDIO_FX_MODEL_DRIFT)?(void*)&g_audio_fx_drift_history[owner][0]:NULL;
        p->mono_sample=mono_sample_kernel(r->config.model);
        r->prepared_mono=p->mono_sample;
        if(plan->mode[si]>=AUDIO_FX_SPATIAL_COUNT)plan->mode[si]=AUDIO_FX_SPATIAL_STEREO;
        p->stereo_sample=spatial_sample_kernel(r->config.model,plan->mode[si]);
        p->stereo_block=spatial_block_kernel(r->config.model,plan->mode[si]);
        if(r->config.model!=AUDIO_FX_MODEL_OFF)plan->active_mask|=(uint8_t)(1U<<si);
    }
    const audio_fx_filter_pos_t requested=(audio_fx_filter_pos_t)
        ((plan->filter_pos<AUDIO_FX_FILTER_POS_COUNT)
            ? plan->filter_pos:AUDIO_FX_FILTER_POS_PRE);
    plan->filter_pos=(filter_is_per_voice(owner)!=0U)?AUDIO_FX_FILTER_POS_PRE:(uint8_t)requested;
    if(plan->order>=AUDIO_FX_ORDER_COUNT)plan->order=AUDIO_FX_ORDER_A_B;
    const uint8_t first=(plan->order==AUDIO_FX_ORDER_B_A)?1U:0U;
    const uint8_t second=(uint8_t)(first^1U);
    const uint8_t ordered[2]={first,second};
    for(uint8_t pos=0U;pos<2U;++pos)
    {
        audio_fx_runtime_plan_slot_t *const slot=&plan->slot[ordered[pos]];
        if(slot->stereo_block==NULL)continue;
        const uint8_t before=(uint8_t)((plan->filter_pos==AUDIO_FX_FILTER_POS_POST)
            ||((plan->filter_pos==AUDIO_FX_FILTER_POS_MID)&&(pos==0U)));
        if(before!=0U)plan->before_filter[plan->before_filter_count++]=slot;
        else plan->after_filter[plan->after_filter_count++]=slot;
    }
    plan->after_filter_sample=(plan->after_filter_count==2U)?sample_chain_two
        :(plan->after_filter_count==1U)?sample_chain_one:sample_chain_none;
    g_audio_fx_filter_pos_status[owner]=plan->filter_pos;
    __DMB();
}

void audio_fx_runtime_init(void)
{
    memset(g_audio_fx_runtime,0,sizeof(g_audio_fx_runtime));
    memset(g_audio_fx_plan,0,sizeof(g_audio_fx_plan));
    memset(g_audio_fx_vibe_history,0,sizeof(g_audio_fx_vibe_history));
    memset(g_audio_fx_drift_history,0,sizeof(g_audio_fx_drift_history));
    for(uint8_t owner=0U;owner<AUDIO_FX_OWNER_COUNT;++owner){g_audio_fx_plan[owner].mode[0]=AUDIO_FX_SPATIAL_STEREO;g_audio_fx_plan[owner].mode[1]=AUDIO_FX_SPATIAL_STEREO;rebuild_plan(owner);}
    mixer_rebuild_static_plan();
}
uint8_t audio_fx_runtime_is_param(param_id_t id){return (uint8_t)(audio_fx_runtime_param_slot(id,NULL)||routing_param(id)||audio_fx_runtime_is_group_level_param(id));}
uint8_t audio_fx_runtime_get_model(brick_entity_id_t e,audio_fx_slot_t slot){uint8_t owner;return(audio_fx_owner(e,&owner)&&slot<AUDIO_FX_SLOT_COUNT)?g_audio_fx_runtime[owner][slot].config.model:AUDIO_FX_MODEL_OFF;}
uint8_t audio_fx_runtime_is_active(brick_entity_id_t e){return(audio_fx_runtime_get_model(e,AUDIO_FX_SLOT_A)!=AUDIO_FX_MODEL_OFF||audio_fx_runtime_get_model(e,AUDIO_FX_SLOT_B)!=AUDIO_FX_MODEL_OFF)?1U:0U;}
uint8_t audio_fx_runtime_is_comp(brick_entity_id_t e){(void)e;return 0U;}
uint8_t audio_fx_runtime_requires_stereo(brick_entity_id_t e){uint8_t owner;return(audio_fx_owner(e,&owner)&&g_audio_fx_plan[owner].filter_pos!=AUDIO_FX_FILTER_POS_PRE)?1U:0U;}
uint8_t audio_fx_runtime_pre_filter_supported(brick_entity_id_t e)
{
    audio_binding_snapshot_t s;if(e>=BRICK_ENTITY_CAPACITY||!audio_note_engine_adapter_snapshot_read(e,&s)||s.binding.bind_state!=TRACK_RUNTIME_BIND_BOUND)return 0U;
    if((s.flags&AUDIO_RUNTIME_FLAG_GROUP_MASTER)!=0U)return 1U;
    if(s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER&&s.type==(uint8_t)TRACK_RUNTIME_TYPE_MULTI)return 0U;
    if(s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SYNTH&&s.configured_voice_count>1U)return 0U;
    return 1U;
}
audio_fx_placement_t audio_fx_runtime_get_placement(brick_entity_id_t e){(void)e;return AUDIO_FX_PLACEMENT_POST_FILTER;}
audio_fx_filter_pos_t audio_fx_runtime_get_filter_pos(brick_entity_id_t e){uint8_t owner;return audio_fx_owner(e,&owner)?(audio_fx_filter_pos_t)g_audio_fx_plan[owner].filter_pos:AUDIO_FX_FILTER_POS_PRE;}
audio_fx_filter_pos_t audio_fx_runtime_status_get_filter_pos(brick_entity_id_t e){uint8_t owner;if(!audio_fx_owner(e,&owner))return AUDIO_FX_FILTER_POS_PRE;const uint8_t pos=g_audio_fx_filter_pos_status[owner];__DMB();return(pos<AUDIO_FX_FILTER_POS_COUNT)?(audio_fx_filter_pos_t)pos:AUDIO_FX_FILTER_POS_PRE;}
void audio_fx_runtime_rebuild_entity_plan(brick_entity_id_t e){uint8_t owner;if(audio_fx_owner(e,&owner)){rebuild_plan(owner);mixer_rebuild_static_plan();}}

uint8_t audio_fx_runtime_apply_param(brick_entity_id_t e,param_id_t id,float value)
{
    if(audio_fx_runtime_is_group_level_param(id)!=0U)
    {
        audio_binding_snapshot_t s;
        if(!audio_note_engine_adapter_snapshot_read(e,&s)
                ||s.binding.bind_state!=TRACK_RUNTIME_BIND_BOUND
                ||(s.flags&AUDIO_RUNTIME_FLAG_GROUP_CHILD)==0U
                ||s.binding.mix_track_id>=MIXER_MAX_TRACKS)return 0U;
        mixer_set_track_group_fx_level(s.binding.mix_track_id,
            (id==PARAM_GROUP_FX_B_LEVEL)?1U:0U,clamp01(value));
        return 1U;
    }
    uint8_t owner;audio_fx_slot_t si;if(!audio_fx_owner(e,&owner))return 0U;
    audio_binding_snapshot_t topology;
    const uint8_t group_master=(uint8_t)(audio_note_engine_adapter_snapshot_read(e,&topology)
        &&((topology.flags&AUDIO_RUNTIME_FLAG_GROUP_MASTER)!=0U));
    if(group_master!=0U&&(id==PARAM_AUDIO_FX_FILTER_POS||id==PARAM_AUDIO_FX_ORDER))return 0U;
    if(id==PARAM_AUDIO_FX_FILTER_POS){g_audio_fx_plan[owner].filter_pos=(value<0.5f)?0U:(value<1.5f)?1U:2U;rebuild_plan(owner);mixer_rebuild_static_plan();return 1U;}
    if(id==PARAM_AUDIO_FX_ORDER){g_audio_fx_plan[owner].order=(value<0.5f)?0U:1U;rebuild_plan(owner);mixer_rebuild_static_plan();return 1U;}
    if(id==PARAM_AUDIO_FX_MODE_A||id==PARAM_AUDIO_FX_MODE_B){const uint8_t slot=(id==PARAM_AUDIO_FX_MODE_B)?1U:0U;const uint8_t mode=(value<0.5f)?0U:(value<1.5f)?1U:(value<2.5f)?2U:3U;g_audio_fx_plan[owner].mode[slot]=mode;rebuild_plan(owner);mixer_rebuild_static_plan();return 1U;}
    if(!audio_fx_runtime_param_slot(id,&si))return 0U;
    audio_fx_runtime_slot_t*r=&g_audio_fx_runtime[owner][si];
    if(param_kind(id)==1U){r->config.p1=clamp01(value);prepare(r);}
    else if(param_kind(id)==2U){r->config.p2=clamp01(value);prepare(r);}
    else if(param_kind(id)==3U){r->config.p3=clamp127(value);prepare(r);}
    else
    {
        uint8_t model=valid_model(value);const audio_fx_slot_t peer=(si==AUDIO_FX_SLOT_A)?AUDIO_FX_SLOT_B:AUDIO_FX_SLOT_A;
        if(model!=AUDIO_FX_MODEL_OFF&&g_audio_fx_runtime[owner][peer].config.model==model)model=AUDIO_FX_MODEL_OFF;
        if(r->config.model!=model){r->config.model=model;reset_light_state(r);prepare(r);rebuild_plan(owner);mixer_rebuild_static_plan();}
    }
    return 1U;
}

uint8_t audio_fx_runtime_apply_drift_delay_modulated(brick_entity_id_t e,param_id_t id,float value)
{
    uint8_t owner;audio_fx_slot_t si;
    if(!audio_fx_owner(e,&owner)||!audio_fx_runtime_param_slot(id,&si)||param_kind(id)!=1U)return 0U;
    audio_fx_runtime_slot_t*const r=&g_audio_fx_runtime[owner][si];
    if(r->config.model!=AUDIO_FX_MODEL_DRIFT)return 0U;
    fx_audio_drift_set_delay(&r->dsp.drift,value);
    return 1U;
}

void audio_fx_runtime_process_mono(brick_entity_id_t e,float*b,uint32_t n)
{
    uint8_t owner;if(!audio_fx_owner(e,&owner)||!b||!n)return;const uint8_t mask=g_audio_fx_plan[owner].active_mask;if(mask==0U)return;
    for(uint8_t si=0;si<AUDIO_FX_SLOT_COUNT;++si){if((mask&(1U<<si))==0U)continue;audio_fx_runtime_slot_t*r=&g_audio_fx_runtime[owner][si];audio_fx_runtime_plan_slot_t*const p=&g_audio_fx_plan[owner].slot[si];void*h=p->history;if(r->config.model==AUDIO_FX_MODEL_LOFI)fx_audio_lofi_process_mono(&r->dsp.lofi,b,n);else if(r->config.model==AUDIO_FX_MODEL_POINT)fx_audio_point_process_mono(&r->dsp.point,b,n);else if(r->config.model==AUDIO_FX_MODEL_DRIFT){const float inc=(r->dsp.drift.delay_target-r->dsp.drift.delay)/(float)n;for(uint32_t i=0;i<n;++i)b[i]=fx_audio_drift_process_mono_sample(&r->dsp.drift,(fx_audio_drift_history_t*)h,b[i],inc)*.5f;r->dsp.drift.delay=r->dsp.drift.delay_target;}else for(uint32_t i=0;i<n;++i)b[i]=p->mono_sample(r,h,b[i]);}
}
static void process_chain(audio_fx_runtime_plan_slot_t *const *chain,uint8_t count,float*l,float*r,uint32_t n){for(uint8_t i=0U;i<count;++i)chain[i]->stereo_block(chain[i]->state,chain[i]->history,l,r,n);}
void audio_fx_runtime_process_before_filter(brick_entity_id_t e,float*l,float*r,uint32_t n){uint8_t owner;if(audio_fx_owner(e,&owner)&&l&&r&&n)process_chain(g_audio_fx_plan[owner].before_filter,g_audio_fx_plan[owner].before_filter_count,l,r,n);}
void audio_fx_runtime_process_after_filter(brick_entity_id_t e,float*l,float*r,uint32_t n){uint8_t owner;if(audio_fx_owner(e,&owner)&&l&&r&&n)process_chain(g_audio_fx_plan[owner].after_filter,g_audio_fx_plan[owner].after_filter_count,l,r,n);}
uint8_t audio_fx_runtime_process_parallel_slot(brick_entity_id_t e,audio_fx_slot_t si,float*l,float*r,uint32_t n)
{
    uint8_t owner;if(!audio_fx_owner(e,&owner)||si>=AUDIO_FX_SLOT_COUNT||!l||!r||!n)return 0U;
    audio_fx_runtime_plan_t*const p=&g_audio_fx_plan[owner];
    if((p->active_mask&(uint8_t)(1U<<si))==0U)return 0U;
    audio_fx_runtime_plan_slot_t*const s=&p->slot[si];s->stereo_block(s->state,s->history,l,r,n);return 1U;
}
void audio_fx_runtime_process_stereo(brick_entity_id_t e,float*l,float*r,uint32_t n){uint8_t owner;if(!audio_fx_owner(e,&owner)||!l||!r||!n)return;audio_fx_runtime_plan_t*const p=&g_audio_fx_plan[owner];process_chain(p->before_filter,p->before_filter_count,l,r,n);process_chain(p->after_filter,p->after_filter_count,l,r,n);}
audio_fx_sample_plan_handle_t audio_fx_runtime_get_sample_plan(brick_entity_id_t e){uint8_t owner;return audio_fx_owner(e,&owner)?(audio_fx_sample_plan_handle_t)&g_audio_fx_plan[owner]:NULL;}
__attribute__((noinline)) void audio_fx_runtime_process_stereo_sample_prepared(audio_fx_sample_plan_handle_t handle,float*l,float*r){audio_fx_runtime_plan_t*const p=(audio_fx_runtime_plan_t*)handle;p->after_filter_sample(p,l,r);}
void audio_fx_runtime_process_stereo_sample(brick_entity_id_t e,float*l,float*r){audio_fx_sample_plan_handle_t p=audio_fx_runtime_get_sample_plan(e);if(p&&l&&r)audio_fx_runtime_process_stereo_sample_prepared(p,l,r);}
float audio_fx_runtime_process_mono_sample(brick_entity_id_t e,float x){uint8_t owner;if(!audio_fx_owner(e,&owner))return x;audio_fx_runtime_plan_t*const p=&g_audio_fx_plan[owner];switch(p->active_mask){case 1U:return p->slot[0].mono_sample(p->slot[0].state,p->slot[0].history,x);case 2U:return p->slot[1].mono_sample(p->slot[1].state,p->slot[1].history,x);case 3U:x=p->slot[0].mono_sample(p->slot[0].state,p->slot[0].history,x);return p->slot[1].mono_sample(p->slot[1].state,p->slot[1].history,x);default:return x;}}
void audio_fx_runtime_process(brick_entity_id_t e,float*l,float*r,uint32_t n){audio_fx_runtime_process_stereo(e,l,r,n);}
