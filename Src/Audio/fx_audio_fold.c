#include "Audio/fx_audio_fold.h"
#include <stddef.h>
extern const float *fx_audio_fold_warps_lut(void);
static inline float warps_channel(float scale,float bias,float x)
{
    float sum=x+bias+(0.25f*x*bias);
    sum*=scale;
    float scaled=sum*(2048.0f/(2.25f*1.02f));
    if(scaled>=2048.0f)return fx_audio_fold_warps_lut()[4096];
    if(scaled<=-2048.0f)return fx_audio_fold_warps_lut()[0];
    const int32_t integral=(int32_t)scaled;
    const float fraction=scaled-(float)integral;
    const float *const table=fx_audio_fold_warps_lut()+2048;
    const float a=table[integral],b=table[integral+1];
    return a+(b-a)*fraction;
}
static inline float warps_xmod_channel(float scale,float bias,float xmod,float x)
{
    const float x2=bias+(x*xmod);
    float sum=x+x2+(0.25f*x*x2);
    sum*=scale;
    float scaled=sum*(2048.0f/(2.25f*1.02f));
    if(scaled>=2048.0f)return fx_audio_fold_warps_lut()[4096];
    if(scaled<=-2048.0f)return fx_audio_fold_warps_lut()[0];
    const int32_t integral=(int32_t)scaled;
    const float fraction=scaled-(float)integral;
    const float *const table=fx_audio_fold_warps_lut()+2048;
    const float a=table[integral],b=table[integral+1];
    return a+(b-a)*fraction;
}
void fx_audio_fold_reset(fx_audio_fold_state_t *s){if(s)*s=(fx_audio_fold_state_t){0};}
void fx_audio_fold_prepare(fx_audio_fold_state_t *s,float fold,float bias,float xmod)
{
    if(!s)return;
    const float f=fold<=0.0f?0.0f:(fold>=1.0f?1.0f:fold);
    const float b=bias<=0.0f?0.0f:(bias>=1.0f?1.0f:bias);
    s->scale=(f>0.0f)?(0.02f+f):0.0f;s->bias=(b*2.0f)-1.0f;
    s->xmod=xmod<=0.0f?0.0f:(xmod>=1.0f?1.0f:xmod);
}
float fx_audio_fold_process_mono_sample(const fx_audio_fold_state_t *s,float x)
{if(!s||s->scale==0.0f)return x;return s->xmod>0.0f?warps_xmod_channel(s->scale,s->bias,s->xmod,x):warps_channel(s->scale,s->bias,x);}
void fx_audio_fold_process_stereo(const fx_audio_fold_state_t *s,float*l,float*r,uint32_t n)
{if(!s||!l||!r||s->scale==0.0f)return;const float scale=s->scale,bias=s->bias,xmod=s->xmod;if(xmod>0.0f){for(uint32_t i=0;i<n;++i){l[i]=warps_xmod_channel(scale,bias,xmod,l[i]);r[i]=warps_xmod_channel(scale,bias,xmod,r[i]);}}else{for(uint32_t i=0;i<n;++i){l[i]=warps_channel(scale,bias,l[i]);r[i]=warps_channel(scale,bias,r[i]);}}}
