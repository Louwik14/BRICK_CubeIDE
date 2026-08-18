#include "Audio/fx_audio_ring.h"
#include "Audio/audio_fx_runtime.h"
#include <math.h>
#include <stddef.h>
static float diode(float x){const float sign=x>0.0f?1.0f:-1.0f;float dead=fabsf(x)-0.667f;dead+=fabsf(dead);dead*=dead;return 0.04324765822726063f*dead*sign;}
static inline float fast_reciprocal(float x){union{float f;uint32_t i;}v={x};v.i=0x7ef311c3U-v.i;float y=v.f;y*=2.0f-x*y;y*=2.0f-x*y;return y;}
static float soft_limit(float x){const float x2=x*x;return x*(27.0f+x2)*fast_reciprocal(27.0f+9.0f*x2);}
static inline float digital(float signal,float carrier,float gain){const float x=signal*carrier*gain;return x/(1.0f+fabsf(x));}
static inline float analog(float signal,float carrier,float gain){const float ring=diode(signal+carrier)+diode(signal-carrier);return soft_limit(ring*gain);}
static inline float soft_limit_light(float x){if(x>=1.5f)return 1.0f;if(x<=-1.5f)return-1.0f;const float x2=x*x;return x-(x*x2*(4.0f/27.0f));}
static inline float analog_light1(float signal,float carrier,float gain){const float ring=diode(signal+carrier)+diode(signal-carrier);return soft_limit_light(ring*gain);}
static inline int16_t clip16(float x){const float v=x*32768.0f;if(v>=32767.0f)return 32767;if(v<=-32768.0f)return -32768;return(int16_t)(int32_t)v;}
static inline float xor_warps(float a,float b){const int16_t ia=clip16(a),ib=clip16(b),bits=(int16_t)((uint16_t)ia^(uint16_t)ib);const float mod=(float)bits*(1.0f/32768.0f),sum=(a+b)*0.7f;return sum+(mod-sum)*(96.0f/127.0f);}
static inline float comparator_warps(float a,float b){const float aa=fabsf(a),ab=fabsf(b);const float window=aa>ab?a:b,window2=aa>ab?aa:-ab;const float fraction=((96.0f/127.0f)*2.995f)-2.0f;return window+(window2-window)*fraction;}
void fx_audio_ring_reset(fx_audio_ring_state_t*s){if(s)*s=(fx_audio_ring_state_t){0};}
void fx_audio_ring_prepare(fx_audio_ring_state_t*s,float freq,float wave,float model,float sr){if(!s)return;const float hz=20.0f*exp2f(freq*7.64385619f);md_phase_set_frequency(&s->carrier,hz,sr>0.0f?sr:44100.0f);s->wave=audio_fx_ring_wave_index_from_control((uint8_t)(wave*127.0f+0.5f));s->model=audio_fx_ring_model_index_from_control((uint8_t)(model+0.5f));const float p=96.0f/127.0f;s->gain=s->model==1U?(4.0f*(1.0f+p*8.0f)):(4.0f+p*24.0f);}
static inline float carrier(fx_audio_ring_state_t*s){if(s->wave==0U)return md_phase_sine_next(&s->carrier);const uint32_t phase=s->carrier.phase;s->carrier.phase+=s->carrier.increment;if(s->wave==3U)return(phase&0x80000000U)?-1.0f:1.0f;const float p=(float)phase*(1.0f/4294967296.0f);if(s->wave==2U)return(2.0f*p)-1.0f;return 1.0f-(4.0f*fabsf(p-0.5f));}
static inline float process(float x,float c,const fx_audio_ring_state_t*s){switch(s->model){case 0U:return x*c;case 1U:return digital(x,c,s->gain);case 2U:return analog(x,c,s->gain);case 3U:return xor_warps(x,c);case 4U:return comparator_warps(x,c);default:return analog_light1(x,c,s->gain);}}
float fx_audio_ring_process_mono_sample(fx_audio_ring_state_t*s,float x){if(!s)return x;const float c=carrier(s);return process(x,c,s);}
void fx_audio_ring_process_stereo_sample(fx_audio_ring_state_t*s,float*l,float*r){if(!s||!l||!r)return;const float c=carrier(s);switch(s->model){case 0U:*l*=c;*r*=c;break;case 1U:*l=digital(*l,c,s->gain);*r=digital(*r,c,s->gain);break;case 2U:*l=analog(*l,c,s->gain);*r=analog(*r,c,s->gain);break;case 3U:*l=xor_warps(*l,c);*r=xor_warps(*r,c);break;case 4U:*l=comparator_warps(*l,c);*r=comparator_warps(*r,c);break;default:*l=analog_light1(*l,c,s->gain);*r=analog_light1(*r,c,s->gain);break;}}
