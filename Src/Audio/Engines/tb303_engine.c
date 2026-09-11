/*
 * Embedded C/float adaptation of Robin Schmidt's Open303 DSP core (MIT).
 * It preserves the SAW303/SQUARE303 mipmapped oscillator strategy,
 * TeeBeeFilter TB_303 topology, measured cutoff/envelope
 * mapping, accent articulation and the fixed analogue-path filters.
 * https://github.com/RobinSchmidt/Open303
 */
#include "Audio/Engines/tb303_engine.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Platform/memory_layout.h"

#define TB303_FS                 48000.0f
#define TB303_TABLE_LEN          512U
#define TB303_TABLE_MASK         (TB303_TABLE_LEN - 1U)
#define TB303_MIP_COUNT          9U
#define TB303_PI                 3.14159265358979323846f
#define TB303_TWO_PI             (2.0f * TB303_PI)
#define TB303_TINY               1.0e-20f
#define TB303_PHASE_SCALE        ((float)TB303_TABLE_LEN / TB303_FS)
#define TB303_FILTER_SCALE       (TB303_TWO_PI / TB303_FS)

typedef struct { float b0,b1,a1,x1,y1; } tb303_onepole_t;
typedef struct { float b0,b1,b2,a1,a2,x1,x2,y1,y2; } tb303_biquad_t;

typedef struct
{
    float phase;
    float frequency;
    float target_frequency;
    float pitch_coeff;
    float tune;
    float cut;
    float res;
    float env_mod;
    float decay;
    float accent;
    float env_scaler;
    float env_offset;
    float nominal_cutoff;
    float resonance_factor;
    float main_env;
    float main_env_coeff;
    float accent_env;
    float accent_coeff;
    float amp_env;
    float amp_decay_coeff;
    float amp_release_coeff;
    float vcf_b0;
    float vcf_k;
    float vcf_g;
    float vcf_target_b0;
    float vcf_target_k;
    float vcf_target_g;
    float vcf_step_b0;
    float vcf_step_k;
    float vcf_step_g;
    float y1,y2,y3,y4;
    tb303_onepole_t pre_hp;
    tb303_onepole_t feedback_hp;
    tb303_onepole_t post_hp;
    tb303_onepole_t allpass;
    tb303_biquad_t declick;
    tb303_biquad_t notch;
    uint8_t wave;
    uint8_t gate;
    uint8_t active;
    uint8_t slide;
    uint8_t pending_release;
    uint8_t note;
    uint8_t vcf_rate;
    uint8_t vcf_interp_remaining;
    uint8_t vcf_primed;
} tb303_runtime_t;

AUDIO_HOT static tb303_runtime_t g_tb303[BRICK6_TB303_INSTANCE_COUNT];
#include "tb303_tables.inc"

static float tb303_clamp(float x,float lo,float hi)
{ return x<lo?lo:(x>hi?hi:x); }

static void tb303_onepole_hp(tb303_onepole_t *f,float hz,float fs)
{
    const float x=expf(-TB303_TWO_PI*hz/fs);
    f->b0=0.5f*(1.0f+x);f->b1=-f->b0;f->a1=x;
}
static void tb303_onepole_ap(tb303_onepole_t *f,float hz,float fs)
{
    const float t=tanf(TB303_PI*hz/fs);const float x=(t-1.0f)/(t+1.0f);
    f->b0=x;f->b1=1.0f;f->a1=-x;
}
static float tb303_onepole_process(tb303_onepole_t *f,float x)
{ const float y=f->b0*x+f->b1*f->x1+f->a1*f->y1+TB303_TINY;f->x1=x;f->y1=y;return y; }

static void tb303_biquad_lp(tb303_biquad_t *f,float hz,float gain_db,float fs)
{
    const float w=TB303_TWO_PI*hz/fs,s=sinf(w),c=cosf(w);
    const float q=powf(10.0f,gain_db/20.0f),a=s/(2.0f*q),z=1.0f/(1.0f+a);
    f->a1=2.0f*c*z;f->a2=(a-1.0f)*z;f->b1=(1.0f-c)*z;f->b0=0.5f*f->b1;f->b2=f->b0;
}
static void tb303_biquad_notch(tb303_biquad_t *f,float hz,float bw,float fs)
{
    const float w=TB303_TWO_PI*hz/fs,s=sinf(w),c=cosf(w);
    const float a=s*sinhf(0.5f*logf(2.0f)*bw*w/s),z=1.0f/(1.0f+a);
    f->a1=2.0f*c*z;f->a2=(a-1.0f)*z;f->b0=z;f->b1=-2.0f*c*z;f->b2=z;
}
static float tb303_biquad_process(tb303_biquad_t *f,float x)
{
    const float y=f->b0*x+f->b1*f->x1+f->b2*f->x2+f->a1*f->y1+f->a2*f->y2+TB303_TINY;
    f->x2=f->x1;f->x1=x;f->y2=f->y1;f->y1=y;return y;
}

static void tb303_clear_signal_state(tb303_runtime_t *v)
{
    v->phase=0.0f;v->main_env=0.0f;v->accent_env=0.0f;v->amp_env=0.0f;
    v->y1=v->y2=v->y3=v->y4=0.0f;
    v->pre_hp.x1=v->pre_hp.y1=0.0f;v->feedback_hp.x1=v->feedback_hp.y1=0.0f;
    v->post_hp.x1=v->post_hp.y1=0.0f;v->allpass.x1=v->allpass.y1=0.0f;
    v->declick.x1=v->declick.x2=v->declick.y1=v->declick.y2=0.0f;
    v->notch.x1=v->notch.x2=v->notch.y1=v->notch.y2=0.0f;
    v->gate=0U;v->active=0U;v->pending_release=0U;v->note=0xFFU;
    v->vcf_interp_remaining=0U;v->vcf_primed=0U;
}

static void tb303_update_env_map(tb303_runtime_t *v)
{
    const float c0=313.815278606f,c1=2394.41198682f;
    const float cutoff=c0*powf(c1/c0,v->cut);
    const float c=logf(cutoff/c0)/logf(c1/c0),e=v->env_mod;
    const float slo=3.77399632511f*e+0.736965594166f;
    const float shi=4.19454878841f*e+0.864344900642f;
    v->env_scaler=(1.0f-c)*slo+c*shi;v->env_offset=0.0482929309436f*c+0.294391201442f;
    v->nominal_cutoff=cutoff;
}
static void tb303_update_resonance(tb303_runtime_t *v)
{ v->resonance_factor=(1.0f-expf(-3.0f*v->res))/(1.0f-expf(-3.0f)); }
static void tb303_update_decay(tb303_runtime_t *v,uint8_t accented)
{
    const float normal_ms=200.0f*powf(10.0f,v->decay);
    const float main_ms=(accented!=0U)?200.0f:normal_ms;
    v->main_env_coeff=expf(-1.0f/(0.001f*main_ms*TB303_FS));
    v->accent_coeff=expf(-1.0f/(0.015f*TB303_FS));
    v->amp_decay_coeff=1.0f-expf(-1.0f/(1.230f*TB303_FS));
    const float release_ms=(accented!=0U)?50.0f:1.0f;
    v->amp_release_coeff=1.0f-expf(-1.0f/(0.001f*release_ms*TB303_FS));
}
static float tb303_exp2_fast(float x);
static void tb303_filter_coeff_target(const tb303_runtime_t *v,float cutoff,
                                      float *b0,float *k_out,float *g)
{
    cutoff=tb303_clamp(cutoff,200.0f,20000.0f);
    const float wc=cutoff*TB303_FILTER_SCALE;
    const float fx=wc*0.707106781187f/TB303_TWO_PI;
    const float r=v->resonance_factor;
    *b0=(0.00045522346f+6.1922189f*fx)/(1.0f+12.358354f*fx+4.4156345f*fx*fx);
    float k=fx*(fx*(fx*(fx*(fx*(fx+7198.6997f)-5837.7917f)-476.47308f)+614.95611f)+213.87126f)+16.998792f;
    *g=((k*(1.0f/17.0f)-1.0f)*r+1.0f)*(1.0f+r);*k_out=k*r;
}
static void tb303_filter_coeff_update(tb303_runtime_t *v,float env)
{
    if(v->vcf_rate==0U)
    {
        const float cutoff=v->nominal_cutoff*tb303_exp2_fast(env);
        tb303_filter_coeff_target(v,cutoff,&v->vcf_b0,&v->vcf_k,&v->vcf_g);
        v->vcf_interp_remaining=0U;v->vcf_primed=1U;
        return;
    }
    if(v->vcf_interp_remaining==0U)
    {
        const float cutoff=v->nominal_cutoff*tb303_exp2_fast(env);
        tb303_filter_coeff_target(v,cutoff,&v->vcf_target_b0,&v->vcf_target_k,&v->vcf_target_g);
        if(v->vcf_primed==0U)
        {
            v->vcf_b0=v->vcf_target_b0;v->vcf_k=v->vcf_target_k;v->vcf_g=v->vcf_target_g;
            v->vcf_step_b0=0.0f;v->vcf_step_k=0.0f;v->vcf_step_g=0.0f;v->vcf_primed=1U;
        }
        else
        {
            v->vcf_step_b0=(v->vcf_target_b0-v->vcf_b0)*0.25f;
            v->vcf_step_k=(v->vcf_target_k-v->vcf_k)*0.25f;
            v->vcf_step_g=(v->vcf_target_g-v->vcf_g)*0.25f;
        }
        v->vcf_interp_remaining=4U;
    }
    v->vcf_b0+=v->vcf_step_b0;v->vcf_k+=v->vcf_step_k;v->vcf_g+=v->vcf_step_g;
    --v->vcf_interp_remaining;
}
static float tb303_filter(tb303_runtime_t *v,float x)
{
    const float y0=x-tb303_onepole_process(&v->feedback_hp,v->vcf_k*v->y4);
    v->y1+=2.0f*v->vcf_b0*(y0-v->y1+v->y2);v->y2+=v->vcf_b0*(v->y1-2.0f*v->y2+v->y3);
    v->y3+=v->vcf_b0*(v->y2-2.0f*v->y3+v->y4);v->y4+=v->vcf_b0*(v->y3-2.0f*v->y4);
    return 2.0f*v->vcf_g*v->y4;
}
static float tb303_exp2_fast(float x)
{
    static const float scale[17]={0.00390625f,0.0078125f,0.015625f,
        0.03125f,0.0625f,0.125f,0.25f,0.5f,1.0f,2.0f,4.0f,8.0f,
        16.0f,32.0f,64.0f,128.0f,256.0f};
    x=tb303_clamp(x,-8.0f,8.0f);
    int32_t whole=(int32_t)x;if(x<(float)whole)--whole;
    const float f=x-(float)whole;
    const float p=1.0f+f*(0.693147181f+f*(0.240226507f
        +f*(0.0555041087f+f*(0.00961812911f+f*0.00133335581f))));
    return p*scale[whole+8];
}
static float tb303_osc(tb303_runtime_t *v,float inc,uint8_t mip)
{
    uint32_t i=(uint32_t)v->phase&TB303_TABLE_MASK;float frac=v->phase-(float)((uint32_t)v->phase);
    float y=g_tb303_tables[v->wave][mip][i]+frac*(g_tb303_tables[v->wave][mip][i+1U]-g_tb303_tables[v->wave][mip][i]);
    if(v->wave!=0U)y*=0.5f;
    v->phase+=inc;
    while(v->phase>=(float)TB303_TABLE_LEN)v->phase-=(float)TB303_TABLE_LEN;
    return y;
}

static float tb303_note_hz(uint8_t note,float tune);

void brick6_tb303_runtime_reset_instance(uint8_t id)
{
    if(id>=BRICK6_TB303_INSTANCE_COUNT)return;
    tb303_runtime_t *v=&g_tb303[id];memset(v,0,sizeof(*v));
    v->note=0xFFU;
    v->cut=0.5f;v->env_mod=0.25f;v->decay=0.699f;v->pitch_coeff=expf(-1.0f/(0.012f*TB303_FS));
    tb303_update_env_map(v);tb303_update_resonance(v);tb303_update_decay(v,0U);tb303_onepole_hp(&v->pre_hp,44.486f,TB303_FS);
    tb303_onepole_hp(&v->feedback_hp,150.0f,TB303_FS);tb303_onepole_hp(&v->post_hp,24.167f,TB303_FS);
    tb303_onepole_ap(&v->allpass,14.008f,TB303_FS);tb303_biquad_lp(&v->declick,200.0f,-3.0103f,TB303_FS);
    tb303_biquad_notch(&v->notch,7.5164f,4.7f,TB303_FS);
}
void brick6_tb303_runtime_init(void)
{ for(uint8_t i=0;i<BRICK6_TB303_INSTANCE_COUNT;++i)brick6_tb303_runtime_reset_instance(i); }

void brick6_tb303_runtime_restart_voice(uint8_t id)
{ if(id<BRICK6_TB303_INSTANCE_COUNT)tb303_clear_signal_state(&g_tb303[id]); }
void brick6_tb303_runtime_sync_voice(uint8_t source,uint8_t destination)
{
    if((source>=BRICK6_TB303_INSTANCE_COUNT)||(destination>=BRICK6_TB303_INSTANCE_COUNT)
            ||(source==destination))return;
    const tb303_runtime_t*s=&g_tb303[source];tb303_runtime_t*d=&g_tb303[destination];
    d->wave=s->wave;d->tune=s->tune;d->cut=s->cut;d->res=s->res;
    d->env_mod=s->env_mod;d->decay=s->decay;d->accent=s->accent;d->slide=s->slide;
    if(d->vcf_rate!=s->vcf_rate){d->vcf_rate=s->vcf_rate;d->vcf_interp_remaining=0U;}
    tb303_update_env_map(d);tb303_update_resonance(d);
    tb303_update_decay(d,d->accent>0.0f);
    if(d->note<128U)d->target_frequency=tb303_note_hz(d->note,d->tune);
}

static float tb303_note_hz(uint8_t note,float tune)
{ return 440.0f*powf(2.0f,(((float)note+tune)-69.0f)/12.0f); }
void brick6_tb303_runtime_note_on(uint8_t id,uint8_t note,uint8_t velocity)
{
    (void)velocity;if(id>=BRICK6_TB303_INSTANCE_COUNT)return;tb303_runtime_t *v=&g_tb303[id];
    const uint8_t legato=(uint8_t)((v->slide!=0U)&&((v->gate!=0U)||(v->pending_release!=0U)));
    if(legato==0U)tb303_clear_signal_state(v);
    v->pending_release=0U;v->target_frequency=tb303_note_hz(note,v->tune);v->note=note;
    const uint8_t accented=(v->accent>0.0f)?1U:0U;tb303_update_decay(v,accented);
    if(legato==0U){v->frequency=v->target_frequency;v->main_env=1.0f/v->main_env_coeff;v->accent_env=v->main_env;v->amp_env=1.0f;}
    v->gate=1U;v->active=1U;
}
void brick6_tb303_runtime_initialize_held_note(uint8_t id,uint8_t note,uint8_t velocity)
{ brick6_tb303_runtime_note_on(id,note,velocity); }
void brick6_tb303_runtime_note_off(uint8_t id,uint8_t note)
{
    if(id>=BRICK6_TB303_INSTANCE_COUNT)return;
    tb303_runtime_t *v=&g_tb303[id];if((v->gate==0U)||(note!=v->note))return;
    if(v->slide!=0U)v->pending_release=1U;else v->gate=0U;
}
void brick6_tb303_runtime_all_notes_off(uint8_t id)
{ if(id<BRICK6_TB303_INSTANCE_COUNT)tb303_clear_signal_state(&g_tb303[id]); }

uint8_t brick6_tb303_runtime_render_instance(uint8_t id,float *out,uint32_t frames)
{
    if((id>=BRICK6_TB303_INSTANCE_COUNT)||(out==NULL))return 0U;
    tb303_runtime_t *v=&g_tb303[id];
    if(v->pending_release!=0U){v->pending_release=0U;v->gate=0U;}
    for(uint32_t n=0;n<frames;++n)
    {
        if(v->active==0U){out[n]=0.0f;continue;}v->frequency=v->target_frequency+v->pitch_coeff*(v->frequency-v->target_frequency);
        v->main_env*=v->main_env_coeff;v->accent_env+= (v->main_env-v->accent_env)*(1.0f-v->accent_coeff);
        const float env=v->env_scaler*(v->main_env-v->env_offset)+v->accent*v->accent_env;
        tb303_filter_coeff_update(v,env);
        const float inc=v->frequency*TB303_PHASE_SCALE;
        uint32_t inc_bits;memcpy(&inc_bits,&inc,sizeof(inc_bits));
        int mip_i=(int)((inc_bits>>23U)&0xFFU)-125;
        if(mip_i<0)mip_i=0;else if(mip_i>=(int)TB303_MIP_COUNT)mip_i=(int)TB303_MIP_COUNT-1;
        const uint8_t mip=(uint8_t)mip_i;
        float y=-tb303_osc(v,inc,mip);y=tb303_onepole_process(&v->pre_hp,y);y=tb303_filter(v,y);
        if(v->gate!=0U)v->amp_env+=(0.0f-v->amp_env)*v->amp_decay_coeff;
        else v->amp_env+=(0.0f-v->amp_env)*v->amp_release_coeff;
        float amp=v->amp_env;if(v->gate!=0U)amp+=0.45f*v->main_env+4.0f*v->accent*v->main_env;
        amp=tb303_biquad_process(&v->declick,amp);y=tb303_onepole_process(&v->allpass,y);y=tb303_onepole_process(&v->post_hp,y);y=tb303_biquad_process(&v->notch,y);
        out[n]=y*amp*0.25118864f;if((v->gate==0U)&&(fabsf(v->amp_env)<1.0e-6f)&&(fabsf(y)<1.0e-6f))v->active=0U;
    }
    return v->active;
}

void brick6_tb303_runtime_set_wave(uint8_t id,uint8_t x){if(id<BRICK6_TB303_INSTANCE_COUNT)g_tb303[id].wave=(x!=0U);}
void brick6_tb303_runtime_set_tune(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT){tb303_runtime_t*v=&g_tb303[id];v->tune=tb303_clamp(x,-12.0f,12.0f);if(v->note<128U)v->target_frequency=tb303_note_hz(v->note,v->tune);}}
void brick6_tb303_runtime_set_cut(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT){g_tb303[id].cut=tb303_clamp(x,0,1);tb303_update_env_map(&g_tb303[id]);}}
void brick6_tb303_runtime_set_res(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT){g_tb303[id].res=tb303_clamp(x,0,1);tb303_update_resonance(&g_tb303[id]);}}
void brick6_tb303_runtime_set_env_mod(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT){g_tb303[id].env_mod=tb303_clamp(x,0,1);tb303_update_env_map(&g_tb303[id]);}}
void brick6_tb303_runtime_set_decay(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT){g_tb303[id].decay=tb303_clamp(x,0,1);tb303_update_decay(&g_tb303[id],g_tb303[id].accent>0);}}
void brick6_tb303_runtime_set_accent(uint8_t id,float x){if(id<BRICK6_TB303_INSTANCE_COUNT)g_tb303[id].accent=tb303_clamp(x,0,1);}
void brick6_tb303_runtime_set_slide(uint8_t id,uint8_t x){if(id<BRICK6_TB303_INSTANCE_COUNT)g_tb303[id].slide=(x!=0U);}
void brick6_tb303_runtime_set_vcf_rate(uint8_t id,uint8_t x){if(id<BRICK6_TB303_INSTANCE_COUNT){tb303_runtime_t*v=&g_tb303[id];const uint8_t rate=(x!=0U);if(v->vcf_rate!=rate){v->vcf_rate=rate;v->vcf_interp_remaining=0U;}}}
