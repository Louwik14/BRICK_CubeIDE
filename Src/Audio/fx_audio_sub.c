#include "fx_audio_sub.h"
#include <math.h>
#include <stddef.h>
__attribute__((always_inline)) static inline float one(fx_audio_sub_state_t*s,fx_audio_sub_channel_t*c,float x){const float dry=x,eq=s->eq,remain=s->one_minus_eq;if(x>0.0f){if(c->was_negative)c->sub_octave^=1U;c->was_negative=0U;}else c->was_negative=1U;c->d=c->d*remain+x*eq;float t=(c->sub_octave?fabsf(c->d):-fabsf(c->d))*s->sub+x*s->fundamental;c->a+=t*eq;const float a2=c->a*c->a;c->a-=a2*c->a*eq;c->a+=(c->a>0.0f)?-s->dcblock:s->dcblock;t=c->a*s->basstrim;c->b=c->b*remain+t*eq;c->c=c->c*remain+c->b*eq;if(s->wet_only)return c->c;return dry*s->dry+c->c*s->wet;}
__attribute__((always_inline)) static inline float one_light(fx_audio_sub_state_t*s,fx_audio_sub_channel_t*c,float x){const float dry=x,eq=s->eq,remain=s->one_minus_eq;if(x>0.0f){if(c->was_negative)c->sub_octave^=1U;c->was_negative=0U;}else c->was_negative=1U;c->d=c->d*remain+x*eq;float t=(c->sub_octave?fabsf(c->d):-fabsf(c->d))*s->sub+x;c->a+=t*eq;const float a2=c->a*c->a;c->a-=a2*c->a*eq;c->a+=(c->a>0.0f)?-s->dcblock:s->dcblock;t=c->a*s->basstrim;c->b=c->b*remain+t*eq;if(s->wet_only)return c->b;return dry*s->dry+c->b*s->wet;}
void fx_audio_sub_reset(fx_audio_sub_state_t*s){if(s)*s=(fx_audio_sub_state_t){0};}
void fx_audio_sub_prepare(fx_audio_sub_state_t*s,float sub,float tone,float mix,float sr){if(!s)return;s->fundamental=1.0f;s->sub=sub;s->eq=0.01f+(tone*tone*tone*tone/(sr>0.0f?sr:44100.0f))*32000.0f;s->one_minus_eq=1.0f-s->eq;s->dcblock=s->eq/320.0f;s->basstrim=0.01f/s->eq+1.0f;s->wet=mix*2.0f;s->dry=2.0f-s->wet;if(s->wet>1.0f)s->wet=1.0f;if(s->dry>1.0f)s->dry=1.0f;s->wet_only=(uint8_t)(s->wet==1.0f&&s->dry==0.0f);}
float fx_audio_sub_process_mono_sample(fx_audio_sub_state_t*s,float x){return s?one(s,&s->left,x):x;}
void fx_audio_sub_process_stereo_sample(fx_audio_sub_state_t*s,float*l,float*r){if(!s||!l||!r)return;*l=one(s,&s->left,*l);*r=one(s,&s->right,*r);}
float fx_audio_sub_light_process_mono_sample(fx_audio_sub_state_t*s,float x){return s?one_light(s,&s->left,x):x;}
void fx_audio_sub_light_process_stereo_sample(fx_audio_sub_state_t*s,float*l,float*r){if(!s||!l||!r)return;*l=one_light(s,&s->left,*l);*r=one_light(s,&s->right,*r);}
