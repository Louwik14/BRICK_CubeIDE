/* DSP provenance: connortreacy/digix0x, digix0x.ino (bec716c7).
 * The 5 us filter coefficients and measured oscillator LUTs are unchanged.
 * Product timing and voice ownership are supplied by BRICK. */
#include "Audio/Engines/acid_engine.h"
#include "Audio/audio_float.h"
#include <math.h>
#include <string.h>

static const uint16_t acid_tanh[256] = {
#include "AcidTables/303filt_tanh.h"
};
static const uint16_t acid_note[4096] = {
#include "AcidTables/midi_note_table_48k_invert3.h"
};
static const int16_t acid_saw_edge[2048] = {
#include "AcidTables/saw_edge2.h"
};
static const float acid_cv[6144] = {
#include "AcidTables/303filt_coeff_5us.h"
};
static const float acid_decay[1024] = {
#include "AcidTables/303_vcf_decay_table.h"
};
static const int16_t acid_square[64U*1024U] = {
#include "AcidTables/sqwavs.h"
};

#define ACID_RATIO (50.0f / 48.0f)
#define ACID_OUT_SCALE (25.0f / 6.0f)
typedef struct {
    float cap1,cap2,cap3,cap4,cap_out,cap_reso,cap_reso2;
    float cap_vca1,cap_vca2,last_average;
    float k0,k1,k2,k3,k4,k5,k11,k12,k13,k14,k15;
    float vcf_env,vca_env,vca_env_decay,vca_delay_cap,last_vca_env;
    float accent,accent_cap1,accent_cap2,accent_vcf,accent_vca;
    float slide_cap,slide_alpha,current_note_value;
    float vcf_decay_coeff,vcf_attack_alpha,vca_attack_mul,vca_release_alpha,vca_decay_initial;
    float accent_attack_alpha,accent_decay_alpha,vca_delay_decay;
    float cut,res,env_mod,decay,accent_knob,tune;
    uint32_t saw,freq;
    uint16_t freq_offset,threshold,freq_inv;
    uint16_t vcf_timer;
    uint8_t phase,sq_note,note,wave,slide,gate,active,pending_release;
    uint8_t vcf_state,vca_state,vca_timer1,vca_timer2,slide_timer,cut_timer,cut_divisor;
} acid_voice_t;
static acid_voice_t g_acid[BRICK6_ACID_INSTANCE_COUNT];

static float acid_clamp(float x,float lo,float hi)
{ return x<lo?lo:(x>hi?hi:x); }
static float acid_alpha(float x)
{ return 1.0f-powf(1.0f-x,ACID_RATIO); }
static float acid_decay_coeff(float x)
{ return powf(x,ACID_RATIO); }
static void acid_set_pitch(acid_voice_t *v,float value)
{
    /* Digix tables cover MIDI notes 12..75, with 64 fractional positions.
     * Keep the measured square-note selection in the same domain. */
    int index=(int)(value+v->tune*64.0f);
    if(index<0)index=0;
    if(index>4094)index=4094;
    /* The 48k LUT is interleaved: even entries are phase increments,
     * odd entries are saw-correction reciprocals. Slide must never read
     * an odd entry as an oscillator increment. */
    index&=~1;
    const uint16_t interim=acid_note[index];
    v->sq_note=(uint8_t)((unsigned)index>>6);
    v->freq=(uint32_t)interim<<10;
    v->freq_offset=interim>>3;
    v->threshold=interim>>2;
    v->freq_inv=acid_note[index+1];
}
static void acid_update_res(acid_voice_t *v)
{
    const float pot=v->res*1023.0f;
    const float inverse=1023.0f-pot;
    v->k15=0.000385f*inverse;
    v->k14=acid_alpha(0.000168f-0.000000045f*inverse);
    v->k13=acid_alpha(0.000165f-0.0000000675f*inverse);
    v->k12=acid_alpha(10.0f*(0.000168f-0.000000045f*inverse));
    v->k11=acid_alpha(10.0f*(0.000165f-0.0000000675f*inverse));
}
static void acid_update_cut(acid_voice_t *v)
{
    int index=(int)(328.0f+0.332f*1023.0f*v->cut
        +615.0f*(v->vcf_env-0.326f)*(0.16667f+0.0008138f*1023.0f*v->env_mod)
        +1353.0f*v->accent_vcf);
    if(index<0)index=0;
    if(index>1023)index=1023;
    index*=6;
    v->k1=acid_cv[index];v->k2=acid_cv[index+1];
    v->k3=acid_cv[index+2];v->k4=acid_cv[index+3];
    v->k5=acid_cv[index+4];v->k0=acid_cv[index+5];
}
static float acid_osc(acid_voice_t *v)
{
    v->saw+=v->freq;
    if(v->wave) {
        const unsigned i=v->saw>>22;
        const uint32_t frac=(v->saw>>6)&0xffffU;
        const unsigned base=(unsigned)v->sq_note*1024U;
        const int32_t a=acid_square[base+i];
        const int32_t b=acid_square[base+((i+1U)&1023U)];
        return (float)((a*(int32_t)(65536U-frac)+b*(int32_t)frac)>>16);
    }
    int32_t wave=(int32_t)(v->saw>>16);
    const uint16_t phase=(uint16_t)(wave+v->freq_offset);
    if(phase<v->threshold) {
        int64_t index=((int64_t)(int16_t)wave*(int64_t)v->freq_inv)>>13;
        index+=1024;
        if(index<0)index=0;
        if(index>2047)index=2047;
        wave-=acid_saw_edge[index];
    }
    return (float)(0x7fff-wave);
}
static float acid_saturate(float wave,float feedback)
{
    int32_t x=(int32_t)(wave*0.5f-feedback);
    if(x>65024)x=65024;
    if(x< -65024)x= -65024;
    const uint32_t magnitude=(uint32_t)(x<0?-x:x);
    const unsigned i=magnitude>>8;
    const unsigned frac=magnitude&255U;
    const int32_t y=(int32_t)((acid_tanh[i]*(256U-frac)+acid_tanh[i+1U]*frac)>>8);
    return (float)(x<0?-y:y);
}
static void acid_filter_step_5us(acid_voice_t *v,float input)
{
    v->cap1+=v->k1*(v->k0*input-(v->cap1+v->cap2));
    v->cap2+=v->k2*v->cap1;
    v->cap3+=v->k3*(v->cap2-(v->cap3+v->cap4));
    float delta=v->k4*v->cap3;
    v->cap4+=delta;
    delta-=v->k5*v->cap_out;
    v->cap_out+=delta;
    const float k6=0.000100528f; /* Ts*2*pi*0.4*8, Ts=5 us */
    const float k10=0.000628319f; /* Ts*2*pi*8/0.4 */
    const float feedback=v->res*5.195f*delta-k6*(v->cap_reso-v->cap_reso2);
    v->cap_reso+=feedback;
    v->cap_reso2+=feedback-k10*v->cap_reso2;
}
static void acid_env_step(acid_voice_t *v)
{
    if(v->vcf_state==0U)v->vcf_env=0.0f;
    else if(v->vcf_state==1U) {
        v->vcf_env+=v->vcf_attack_alpha*(1.0f-v->vcf_env);
        if(v->vcf_env>0.99998f){v->vcf_env=0.99998f;v->vcf_state=2U;v->vcf_timer=0U;}
        float signal=v->accent*v->vcf_env-0.362f-v->accent_cap1;
        if(signal>0.0f){v->accent_cap1+=v->k13*signal;v->accent_cap1-=v->k14*v->accent_cap1;v->accent_vcf=v->k15*signal+v->accent_cap1;}
        else {v->accent_cap1-=v->k14*v->accent_cap1;v->accent_vcf=v->accent_cap1;}
        v->accent_cap2+=v->accent_attack_alpha*(v->accent*v->vcf_env-v->accent_cap2);
        v->accent_vca=v->accent_cap2>0.09f?v->accent_cap2-0.09f:0.0f;
    } else if(v->vcf_state==2U) {
        if(++v->vcf_timer==10U) {
            v->vcf_timer=0U;v->vcf_env*=v->vcf_decay_coeff;
            if(v->vcf_env<0.000015f){v->vcf_env=0.0f;v->vcf_state=3U;}
            float signal=v->accent*v->vcf_env-0.362f-v->accent_cap1;
            if(signal>0.0f){v->accent_cap1+=v->k11*signal;v->accent_cap1-=v->k12*v->accent_cap1;v->accent_vcf=v->k15*signal+v->accent_cap1;}
            else {v->accent_cap1-=v->k12*v->accent_cap1;v->accent_vcf=v->accent_cap1;}
            v->accent_cap2+=v->accent_decay_alpha*(v->accent*v->vcf_env-v->accent_cap2);
            v->accent_vca=v->accent_cap2>0.09f?v->accent_cap2-0.09f:0.0f;
        }
    } else if(++v->vcf_timer==10U) {
        v->vcf_timer=0U;v->accent_cap1-=v->k12*v->accent_cap1;v->accent_vcf=v->accent_cap1;
        if(v->accent_vcf<0.000015f){v->accent_vcf=0.0f;v->vcf_state=0U;}
    }
    if(v->vca_state==0U){v->vca_env=0.0f;v->vca_delay_cap*=v->vca_delay_decay;}
    else if(v->vca_state==1U) {
        v->vca_delay_cap+=0.002f*ACID_RATIO;
        if(v->vca_delay_cap>0.4828f){
            v->vca_delay_cap=0.565f;
            v->vca_env=v->vca_env*v->vca_attack_mul+0.0076f*ACID_RATIO;
            if(v->vca_env>0.99998f){v->vca_env=0.99998f;v->vca_state=2U;v->vca_timer1=0U;v->vca_timer2=0U;v->vca_env_decay=v->vca_decay_initial;}
        }
    } else if(v->vca_state==2U) {
        if(++v->vca_timer1==10U){
            v->vca_timer1=0U;v->vca_env*=v->vca_env_decay;
            if(++v->vca_timer2==100U){v->vca_timer2=0U;v->vca_env_decay-=0.000001f*ACID_RATIO;}
            if(v->vca_env<0.000015f){v->vca_env=0.0f;v->vca_state=0U;}
        }
    } else {
        v->vca_delay_cap*=v->vca_delay_decay;
        v->vca_env+=v->vca_release_alpha*(v->last_vca_env-v->vca_env);
        if(v->vca_env<=0.000015f){v->vca_env=0.0f;v->vca_state=0U;}
    }
    if(v->slide_timer!=255U && ++v->slide_timer==10U){
        v->slide_timer=0U;
        v->slide_cap+=v->slide_alpha*(v->current_note_value-v->slide_cap);
        acid_set_pitch(v,v->slide_cap);
    }
}
void brick6_acid_runtime_reset_instance(uint8_t id)
{
    if(id>=BRICK6_ACID_INSTANCE_COUNT)return;
    acid_voice_t *v=&g_acid[id];memset(v,0,sizeof(*v));
    v->note=255U;v->slide_timer=255U;v->cut_divisor=3U;v->slide_alpha=acid_alpha(0.00909f);
    v->vcf_attack_alpha=acid_alpha(0.15f);
    v->vca_attack_mul=powf(1.076f,ACID_RATIO);
    v->vca_release_alpha=acid_alpha(0.004f);
    v->accent_attack_alpha=acid_alpha(0.0404f);
    v->accent_decay_alpha=acid_alpha(0.404f);
    v->vca_delay_decay=acid_decay_coeff(0.9998f);
    v->cut=0.5f;v->env_mod=0.25f;v->decay=0.699f;
    v->vca_decay_initial=acid_decay_coeff(0.99991f);
    v->vca_env_decay=v->vca_decay_initial;acid_update_res(v);acid_update_cut(v);
    v->vcf_decay_coeff=acid_decay_coeff(acid_decay[(unsigned)(v->decay*1023.0f)]);
}
void brick6_acid_runtime_init(void)
{for(uint8_t i=0U;i<BRICK6_ACID_INSTANCE_COUNT;++i)brick6_acid_runtime_reset_instance(i);}
void brick6_acid_runtime_restart_voice(uint8_t id)
{
    if(id>=BRICK6_ACID_INSTANCE_COUNT)return;
    acid_voice_t *v=&g_acid[id];
    v->gate=0U;v->active=0U;v->pending_release=0U;
    v->vca_state=0U;v->vcf_state=0U;v->vca_env=0.0f;v->vcf_env=0.0f;
    v->cap1=v->cap2=v->cap3=v->cap4=v->cap_out=0.0f;
    v->cap_reso=v->cap_reso2=v->cap_vca1=v->cap_vca2=0.0f;
    v->last_average=0.0f;v->accent_cap1=v->accent_cap2=0.0f;
    v->accent_vcf=v->accent_vca=0.0f;v->phase=0U;
}
void brick6_acid_runtime_sync_voice(uint8_t source,uint8_t destination)
{
    if(source>=BRICK6_ACID_INSTANCE_COUNT||destination>=BRICK6_ACID_INSTANCE_COUNT)return;
    acid_voice_t *s=&g_acid[source],*d=&g_acid[destination];
    d->wave=s->wave;d->tune=s->tune;d->cut=s->cut;d->res=s->res;d->env_mod=s->env_mod;
    d->decay=s->decay;d->accent_knob=s->accent_knob;d->slide=s->slide;d->cut_divisor=s->cut_divisor;
    acid_update_res(d);
}
void brick6_acid_runtime_note_on(uint8_t id,uint8_t note,uint8_t velocity)
{
    (void)velocity;if(id>=BRICK6_ACID_INSTANCE_COUNT)return;
    acid_voice_t *v=&g_acid[id];
    const uint8_t legato=(uint8_t)(v->slide&&(v->gate||v->pending_release));
    if(!legato){v->vcf_state=1U;v->vca_state=1U;v->slide_timer=255U;v->vcf_env=0.0f;v->vca_env=0.0f;v->slide_cap=(float)((int)note-12)*64.0f;}
    else v->slide_timer=0U;
    v->current_note_value=(float)((int)note-12)*64.0f;
    v->note=note;v->accent=v->accent_knob;v->gate=1U;v->active=1U;v->pending_release=0U;
    v->vcf_decay_coeff=acid_decay_coeff(v->accent>0.0f?0.9972f:acid_decay[(unsigned)(v->decay*1023.0f)]);
    if(!legato)acid_set_pitch(v,v->current_note_value);
}
void brick6_acid_runtime_initialize_held_note(uint8_t id,uint8_t note,uint8_t velocity)
{brick6_acid_runtime_note_on(id,note,velocity);}
void brick6_acid_runtime_note_off(uint8_t id,uint8_t note)
{
    if(id>=BRICK6_ACID_INSTANCE_COUNT)return;
    acid_voice_t *v=&g_acid[id];if(!v->gate||v->note!=note)return;
    if(v->slide)v->pending_release=1U;
    else {v->gate=0U;v->last_vca_env=0.0f;v->vca_state=3U;}
}
void brick6_acid_runtime_all_notes_off(uint8_t id)
{if(id<BRICK6_ACID_INSTANCE_COUNT){acid_voice_t *v=&g_acid[id];v->gate=0U;v->active=0U;v->vca_state=0U;v->vcf_state=0U;}}
__attribute__((noinline)) uint8_t brick6_acid_runtime_render_instance(uint8_t id,float *out,uint32_t frames)
{
    if(id>=BRICK6_ACID_INSTANCE_COUNT||!out||frames>AUDIO_BLOCK_SIZE)return 0U;
    acid_voice_t *v=&g_acid[id];
    if(v->pending_release){v->pending_release=0U;v->gate=0U;v->last_vca_env=0.0f;v->vca_state=3U;}
    for(uint32_t n=0U;n<frames;++n){
        if(!v->active){out[n]=0.0f;continue;}
        const float wave=acid_osc(v);
        const float input=acid_saturate(wave,v->cap_reso2);
        /* Integer 25/6 scheduler: exactly 25 filter ticks per six 48 kHz
         * frames, carried between blocks. No variable-duration DSP step. */
        v->phase=(uint8_t)(v->phase+25U);
        while(v->phase>=6U){acid_filter_step_5us(v,input);v->phase=(uint8_t)(v->phase-6U);}
        /* The original four-step output multiplier represented filter ticks
         * per DAC sample. At 200k/48k this physical scale is 25/6. */
        const float average=ACID_OUT_SCALE*v->cap_out;
        const float delta=average-v->last_average;v->last_average=average;
        v->cap_vca1+=delta-0.00785f*ACID_RATIO*v->cap_vca1;
        v->cap_vca2+=delta-0.0204f*ACID_RATIO*v->cap_vca2;
        if(++v->cut_timer>=v->cut_divisor){v->cut_timer=0U;acid_update_cut(v);}
        acid_env_step(v);
        const float result=(v->vca_env+2.855f*v->accent_vca)
            *(v->cap_vca1+0.42f*v->res*5.195f*v->cap_vca2)*0.04f;
        out[n]=acid_clamp(result/32768.0f,-1.0f,1.0f);
        if(!v->gate&&v->vca_state==0U&&v->vcf_state==0U)v->active=0U;
    }
    return v->active;
}
void brick6_acid_runtime_set_wave(uint8_t id,uint8_t x){if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].wave=x!=0U;}
void brick6_acid_runtime_set_tune(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT){g_acid[id].tune=acid_clamp(x,-12.0f,12.0f);acid_set_pitch(&g_acid[id],g_acid[id].slide_cap);}}
void brick6_acid_runtime_set_cut(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].cut=acid_clamp(x,0.0f,1.0f);}
void brick6_acid_runtime_set_res(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT){g_acid[id].res=acid_clamp(x,0.0f,1.0f);acid_update_res(&g_acid[id]);}}
void brick6_acid_runtime_set_env_mod(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].env_mod=acid_clamp(x,0.0f,1.0f);}
void brick6_acid_runtime_set_decay(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT){acid_voice_t*v=&g_acid[id];v->decay=acid_clamp(x,0.0f,1.0f);v->vcf_decay_coeff=acid_decay_coeff(v->accent>0.0f?0.9972f:acid_decay[(unsigned)(v->decay*1023.0f)]);}}
void brick6_acid_runtime_set_accent(uint8_t id,float x){if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].accent_knob=acid_clamp(x,0.0f,1.0f);}
void brick6_acid_runtime_set_slide(uint8_t id,uint8_t x){if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].slide=x!=0U;}
void brick6_acid_runtime_set_cutoff_rate(uint8_t id,uint8_t x)
{
    static const uint8_t divisors[4]={3U,6U,8U,12U};
    if(id<BRICK6_ACID_INSTANCE_COUNT)g_acid[id].cut_divisor=divisors[x<4U?x:0U];
}
