#include "Audio/fx_audio_vibe.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "Audio/deluge_tables.h"
#define VIBE_RING_MASK (FX_AUDIO_VIBE_RING_SIZE-1U)
static int32_t q31(float v){if(v>=1.0f)return INT32_MAX;if(v<=-1.0f)return INT32_MIN;return(int32_t)(v*2147483647.0f);}
static int32_t sine_q31(uint32_t p){const uint32_t i=p>>24,f=(p>>8)&65535U;return(int32_t)sineWaveSmall[i]*(int32_t)(65536U-f)+(int32_t)sineWaveSmall[i+1U]*(int32_t)f;}
static float lerp(float a,float b,float f){return a+(b-a)*f;}
void fx_audio_vibe_reset(fx_audio_vibe_state_t*s,fx_audio_vibe_history_t*h){if(!s||!h)return;memset(s,0,sizeof(*s));memset(h,0,sizeof(*h));}
void fx_audio_vibe_prepare(fx_audio_vibe_state_t*s,float rate,float depth,float delay){if(!s)return;if(rate<.01f)rate=.01f;if(rate>12.0f)rate=12.0f;if(depth<0)depth=0;if(depth>1)depth=1;if(delay<0)delay=0;if(delay>1)delay=1;s->phase_increment=(uint32_t)(rate*(4294967296.0f/48000.0f));s->depth=q31(depth);s->offset=q31(delay*2.0f-1.0f);}
float fx_audio_vibe_process_wet_mono_sample(fx_audio_vibe_state_t*s,fx_audio_vibe_history_t*h,float x){if(!s||!h)return x;const float lfo=sine_q31(s->phase)*(1.0f/2147483648.0f),off=s->offset*(1.0f/2147483648.0f),dep=s->depth*(1.0f/2147483648.0f),centre=511.0f*(off+1.0f)*.25f,amp=centre*dep*2.0f;float d=(centre+lfo*amp*.5f)*(160.0f/147.0f);if(d<1)d=1;const uint32_t w=s->write,whole=(uint32_t)d,p=(w-whole)&VIBE_RING_MASK;const float wet=lerp(h->ring[p].l,h->ring[(p-1U)&VIBE_RING_MASK].l,d-(float)whole);h->ring[w]=(fx_audio_vibe_frame_t){x,x};s->write=(w+1U)&VIBE_RING_MASK;s->phase+=s->phase_increment;return wet;}
void fx_audio_vibe_process_wet_stereo_sample(fx_audio_vibe_state_t*s,fx_audio_vibe_history_t*h,float*l,float*r){if(!s||!h||!l||!r)return;const float lfo=sine_q31(s->phase)*(1.0f/2147483648.0f),off=s->offset*(1.0f/2147483648.0f),dep=s->depth*(1.0f/2147483648.0f),centre=511.0f*(off+1.0f)*.25f,amp=centre*dep*2.0f;float dl=(centre+lfo*amp*.5f)*(160.0f/147.0f),dr=(centre-lfo*amp*.5f)*(160.0f/147.0f);if(dl<1)dl=1;if(dr<1)dr=1;const uint32_t w=s->write,wl=(uint32_t)dl,wr=(uint32_t)dr,pl=(w-wl)&VIBE_RING_MASK,pr=(w-wr)&VIBE_RING_MASK;const float wetl=lerp(h->ring[pl].l,h->ring[(pl-1U)&VIBE_RING_MASK].l,dl-(float)wl),wetr=lerp(h->ring[pr].r,h->ring[(pr-1U)&VIBE_RING_MASK].r,dr-(float)wr);h->ring[w]=(fx_audio_vibe_frame_t){*l,*r};s->write=(w+1U)&VIBE_RING_MASK;s->phase+=s->phase_increment;*l=wetl;*r=wetr;}
void fx_audio_vibe_process_wet_stereo(fx_audio_vibe_state_t*s,fx_audio_vibe_history_t*h,float*l,float*r,uint32_t n){if(!l||!r)return;for(uint32_t i=0;i<n;++i)fx_audio_vibe_process_wet_stereo_sample(s,h,&l[i],&r[i]);}
