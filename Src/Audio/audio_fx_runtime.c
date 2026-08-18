#include "Audio/audio_fx_runtime.h"
#include <stddef.h>
#include "Audio/fx_audio_fold.h"
#include "Audio/fx_audio_lofi.h"
#include "Audio/fx_audio_drive.h"
#include "Audio/fx_audio_point.h"
#include "Audio/fx_audio_sub.h"
#include "Audio/fx_audio_ring.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Board/board_audio_format.h"
#include "Storage/memory_layout.h"

typedef union { fx_audio_lofi_state_t lofi; fx_audio_fold_state_t fold; fx_audio_drive_state_t drive; fx_audio_point_state_t point; fx_audio_sub_state_t sub; fx_audio_ring_state_t ring; } audio_fx_runtime_dsp_t;
/* AUDIO-local projection of the canonical CONTROL values.  This is copied
 * field-by-field by the parameter event consumer; it is never shared. */
typedef struct { uint8_t model; float p1,p2,p3; } audio_fx_audio_config_t;
typedef struct { audio_fx_audio_config_t config; audio_fx_runtime_dsp_t dsp; } audio_fx_runtime_t;
_Static_assert(sizeof(audio_fx_runtime_dsp_t)==88U,"Audio FX DSP union size changed");
_Static_assert(sizeof(audio_fx_runtime_t)==104U,"Audio FX runtime size changed");
AUDIO_HOT static audio_fx_runtime_t g_audio_fx_runtime[BRICK_ENTITY_CAPACITY];

static float clamp01(float v){return v<=0.0f?0.0f:(v>=1.0f?1.0f:v);}
static float clamp127(float v){return v<=0.0f?0.0f:(v>=127.0f?127.0f:v);}
static uint8_t valid_model(float value)
{
    const uint8_t m=(value<0.0f)?0U:(uint8_t)(value+0.5f);
    return (m==AUDIO_FX_MODEL_LOFI||m==AUDIO_FX_MODEL_FOLD||m==AUDIO_FX_MODEL_DRIVE||m==AUDIO_FX_MODEL_POINT||m==AUDIO_FX_MODEL_SUB||m==AUDIO_FX_MODEL_SUB_LIGHT||m==AUDIO_FX_MODEL_RING)?m:AUDIO_FX_MODEL_OFF;
}
static void reset_dsp(audio_fx_runtime_t*r)
{
    switch(r->config.model){case AUDIO_FX_MODEL_LOFI:fx_audio_lofi_reset(&r->dsp.lofi);break;
    case AUDIO_FX_MODEL_FOLD:fx_audio_fold_reset(&r->dsp.fold);break;
    case AUDIO_FX_MODEL_DRIVE:fx_audio_drive_reset(&r->dsp.drive);break;
    case AUDIO_FX_MODEL_POINT:fx_audio_point_reset(&r->dsp.point);break;default:break;}
    if(r->config.model==AUDIO_FX_MODEL_SUB||r->config.model==AUDIO_FX_MODEL_SUB_LIGHT)fx_audio_sub_reset(&r->dsp.sub);
    else if(r->config.model==AUDIO_FX_MODEL_RING)fx_audio_ring_reset(&r->dsp.ring);
}
static void prepare(audio_fx_runtime_t*r)
{
    switch(r->config.model){case AUDIO_FX_MODEL_LOFI:
        fx_audio_lofi_set_engine(&r->dsp.lofi,(fx_audio_lofi_engine_t)audio_fx_lofi_model_index_from_control((uint8_t)(clamp127(r->config.p3)+0.5f)));
        fx_audio_lofi_prepare(&r->dsp.lofi,r->config.p1,r->config.p2);break;
    case AUDIO_FX_MODEL_FOLD:fx_audio_fold_prepare(&r->dsp.fold,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f);break;
    case AUDIO_FX_MODEL_DRIVE:fx_audio_drive_prepare(&r->dsp.drive,r->config.p1,r->config.p2,clamp127(r->config.p3));break;
    case AUDIO_FX_MODEL_POINT:fx_audio_point_prepare(&r->dsp.point,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f,(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
    case AUDIO_FX_MODEL_SUB:case AUDIO_FX_MODEL_SUB_LIGHT:fx_audio_sub_prepare(&r->dsp.sub,r->config.p1,r->config.p2,clamp127(r->config.p3)/127.0f,(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
    case AUDIO_FX_MODEL_RING:fx_audio_ring_prepare(&r->dsp.ring,r->config.p1,r->config.p2,clamp127(r->config.p3),(float)BOARD_AUDIO_SAMPLE_RATE_HZ);break;
    default:break;}
}
static float mono_one(audio_fx_runtime_t*r,float x)
{
    switch(r->config.model){case AUDIO_FX_MODEL_LOFI:return fx_audio_lofi_process_mono_sample(&r->dsp.lofi,x);
    case AUDIO_FX_MODEL_FOLD:return fx_audio_fold_process_mono_sample(&r->dsp.fold,x);
    case AUDIO_FX_MODEL_DRIVE:return fx_audio_drive_process_sample(&r->dsp.drive,x);
    case AUDIO_FX_MODEL_POINT:return fx_audio_point_process_mono_sample(&r->dsp.point,x);default:break;}
    if(r->config.model==AUDIO_FX_MODEL_SUB)return fx_audio_sub_process_mono_sample(&r->dsp.sub,x);
    if(r->config.model==AUDIO_FX_MODEL_SUB_LIGHT)return fx_audio_sub_light_process_mono_sample(&r->dsp.sub,x);
    if(r->config.model==AUDIO_FX_MODEL_RING)return fx_audio_ring_process_mono_sample(&r->dsp.ring,x);
    return x;
}
void audio_fx_runtime_init(void){for(brick_entity_id_t e=0;e<BRICK_ENTITY_CAPACITY;++e)g_audio_fx_runtime[e]=(audio_fx_runtime_t){0};}
uint8_t audio_fx_runtime_is_param(param_id_t id){return id==PARAM_AUDIO_FX_P1||id==PARAM_AUDIO_FX_P2||id==PARAM_AUDIO_FX_P3||id==PARAM_AUDIO_FX_MODEL;}
uint8_t audio_fx_runtime_is_active(brick_entity_id_t e){return e<BRICK_ENTITY_CAPACITY&&g_audio_fx_runtime[e].config.model!=AUDIO_FX_MODEL_OFF;}
uint8_t audio_fx_runtime_is_comp(brick_entity_id_t e){(void)e;return 0U;}
uint8_t audio_fx_runtime_requires_stereo(brick_entity_id_t e){(void)e;return 0U;}
uint8_t audio_fx_runtime_pre_filter_supported(brick_entity_id_t e)
{
    audio_binding_snapshot_t s;if(e>=BRICK_ENTITY_CAPACITY||!audio_note_engine_adapter_snapshot_read(e,&s)||s.binding.bind_state!=TRACK_RUNTIME_BIND_BOUND)return 0U;
    if((s.flags&AUDIO_RUNTIME_FLAG_GROUP_MASTER)!=0U)return 1U;
    if(s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER&&s.type==(uint8_t)TRACK_RUNTIME_TYPE_MULTI)return 0U;
    if(s.family==(uint8_t)TRACK_RUNTIME_FAMILY_SYNTH&&s.configured_voice_count>1U)return 0U;
    return 1U;
}
audio_fx_placement_t audio_fx_runtime_get_placement(brick_entity_id_t e)
{
    if(e>=BRICK_ENTITY_CAPACITY)return AUDIO_FX_PLACEMENT_POST_FILTER;
    return AUDIO_FX_PLACEMENT_POST_FILTER;
}
uint8_t audio_fx_runtime_apply_param(brick_entity_id_t e,param_id_t id,float value)
{
    if(e>=BRICK_ENTITY_CAPACITY||!audio_fx_runtime_is_param(id))return 0U;
    audio_fx_runtime_t*r=&g_audio_fx_runtime[e];
    if(id==PARAM_AUDIO_FX_P1){r->config.p1=clamp01(value);prepare(r);}
    else if(id==PARAM_AUDIO_FX_P2){r->config.p2=clamp01(value);prepare(r);}
    else if(id==PARAM_AUDIO_FX_P3){r->config.p3=clamp127(value);if(r->config.model==AUDIO_FX_MODEL_LOFI||r->config.model==AUDIO_FX_MODEL_FOLD||r->config.model==AUDIO_FX_MODEL_DRIVE||r->config.model==AUDIO_FX_MODEL_POINT||r->config.model==AUDIO_FX_MODEL_SUB||r->config.model==AUDIO_FX_MODEL_SUB_LIGHT||r->config.model==AUDIO_FX_MODEL_RING)prepare(r);}
    else {const uint8_t m=valid_model(value);if(r->config.model!=m){r->config.model=m;reset_dsp(r);prepare(r);}}return 1U;
}
void audio_fx_runtime_process_mono(brick_entity_id_t e,float*b,uint32_t n)
{
    if(e>=BRICK_ENTITY_CAPACITY||!b||!n||!audio_fx_runtime_is_active(e))return;
    audio_fx_runtime_t*r=&g_audio_fx_runtime[e];
    if(r->config.model==AUDIO_FX_MODEL_LOFI){fx_audio_lofi_process_mono(&r->dsp.lofi,b,n);return;}
    if(r->config.model==AUDIO_FX_MODEL_POINT){fx_audio_point_process_mono(&r->dsp.point,b,n);return;}
    for(uint32_t i=0;i<n;++i)b[i]=mono_one(r,b[i]);
}
void audio_fx_runtime_process_stereo(brick_entity_id_t e,float*l,float*rgt,uint32_t n)
{
    if(e>=BRICK_ENTITY_CAPACITY||!l||!rgt||!n||!audio_fx_runtime_is_active(e))return;
    audio_fx_runtime_t*r=&g_audio_fx_runtime[e];
    if(r->config.model==AUDIO_FX_MODEL_LOFI)fx_audio_lofi_process_stereo(&r->dsp.lofi,l,rgt,n);
    else if(r->config.model==AUDIO_FX_MODEL_FOLD)fx_audio_fold_process_stereo(&r->dsp.fold,l,rgt,n);
    else if(r->config.model==AUDIO_FX_MODEL_DRIVE)fx_audio_drive_process_stereo(&r->dsp.drive,l,rgt,n);
    else if(r->config.model==AUDIO_FX_MODEL_POINT)fx_audio_point_process_stereo(&r->dsp.point,l,rgt,n);
    else if(r->config.model==AUDIO_FX_MODEL_SUB)for(uint32_t i=0;i<n;++i)fx_audio_sub_process_stereo_sample(&r->dsp.sub,&l[i],&rgt[i]);
    else if(r->config.model==AUDIO_FX_MODEL_SUB_LIGHT)for(uint32_t i=0;i<n;++i)fx_audio_sub_light_process_stereo_sample(&r->dsp.sub,&l[i],&rgt[i]);
    else if(r->config.model==AUDIO_FX_MODEL_RING)for(uint32_t i=0;i<n;++i)fx_audio_ring_process_stereo_sample(&r->dsp.ring,&l[i],&rgt[i]);
}
void audio_fx_runtime_process_stereo_sample(brick_entity_id_t e,float*l,float*r)
{if(e>=BRICK_ENTITY_CAPACITY||!l||!r||!audio_fx_runtime_is_active(e))return;audio_fx_runtime_t*s=&g_audio_fx_runtime[e];if(s->config.model==AUDIO_FX_MODEL_LOFI)fx_audio_lofi_process_stereo_sample(&s->dsp.lofi,l,r);else if(s->config.model==AUDIO_FX_MODEL_FOLD){*l=fx_audio_fold_process_mono_sample(&s->dsp.fold,*l);*r=fx_audio_fold_process_mono_sample(&s->dsp.fold,*r);}else if(s->config.model==AUDIO_FX_MODEL_DRIVE){*l=fx_audio_drive_process_sample(&s->dsp.drive,*l);*r=fx_audio_drive_process_sample(&s->dsp.drive,*r);}else if(s->config.model==AUDIO_FX_MODEL_POINT)fx_audio_point_process_stereo_sample(&s->dsp.point,l,r);else if(s->config.model==AUDIO_FX_MODEL_SUB)fx_audio_sub_process_stereo_sample(&s->dsp.sub,l,r);else if(s->config.model==AUDIO_FX_MODEL_SUB_LIGHT)fx_audio_sub_light_process_stereo_sample(&s->dsp.sub,l,r);else if(s->config.model==AUDIO_FX_MODEL_RING)fx_audio_ring_process_stereo_sample(&s->dsp.ring,l,r);}
float audio_fx_runtime_process_mono_sample(brick_entity_id_t e,float x){return(e>=BRICK_ENTITY_CAPACITY||!audio_fx_runtime_is_active(e))?x:mono_one(&g_audio_fx_runtime[e],x);}
void audio_fx_runtime_process(brick_entity_id_t e,float*l,float*r,uint32_t n){audio_fx_runtime_process_stereo(e,l,r,n);}
