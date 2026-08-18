#include "Audio/fx_audio_lofi.h"
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

enum { Q22_ONE = 4194304U, Q22_MASK = 4194303U };
const char g_fx_audio_lofi_engine_build_id[] __attribute__((used)) =
    "LOFI engines: SOFT/MID/HARD runtime";

static float clamp1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }
static int32_t sat64(int64_t x) { return x > INT32_MAX ? INT32_MAX : (x < INT32_MIN ? INT32_MIN : (int32_t)x); }
static int32_t qmul(int32_t a, int32_t b) { return (int32_t)((((int64_t)a * b) + 0x80000000LL) >> 32); }

static void reset_engine(fx_audio_lofi_state_t *s, uint8_t engine)
{
    if (engine == FX_AUDIO_LOFI_ENGINE_HARD) memset(&s->hybrid, 0, sizeof(s->hybrid));
    else if (engine == FX_AUDIO_LOFI_ENGINE_MID) memset(&s->derez, 0, sizeof(s->derez));
    else memset(&s->derez3, 0, sizeof(s->derez3));
}

void fx_audio_lofi_reset(fx_audio_lofi_state_t *s)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->low_increment = s->high_increment = Q22_ONE;
}

void fx_audio_lofi_set_engine(fx_audio_lofi_state_t *s, fx_audio_lofi_engine_t engine)
{
    if (s == NULL) return;
    if ((uint8_t)engine >= FX_AUDIO_LOFI_ENGINE_COUNT) engine = FX_AUDIO_LOFI_ENGINE_SOFT;
    if (s->engine != (uint8_t)engine) { s->engine = (uint8_t)engine; reset_engine(s, s->engine); }
}

void fx_audio_lofi_prepare(fx_audio_lofi_state_t *s, float bit, float srr)
{
    if (s == NULL) return;
    if (bit < 0.0f) bit = 0.0f; else if (bit > 1.0f) bit = 1.0f;
    if (srr < 0.0f) srr = 0.0f; else if (srr > 1.0f) srr = 1.0f;
    const uint8_t be = bit > 0.0f, se = srr > 0.0f;
    s->mode = se ? (be ? FX_AUDIO_LOFI_MODE_BIT_AND_SRR : FX_AUDIO_LOFI_MODE_SRR_ONLY)
                 : (be ? FX_AUDIO_LOFI_MODE_BIT_ONLY : FX_AUDIO_LOFI_MODE_BYPASS);

    /* HARD keeps the Deluge FLOAT sample-rate reducer unchanged. */
    if (se) {
        s->low_increment = (uint32_t)((float)Q22_ONE * exp2f(srr * 8.0f));
        const uint32_t d = s->low_increment >> 6U;
        s->high_increment = d ? (UINT32_MAX / d) << 6U : UINT32_MAX;
    } else s->low_increment = s->high_increment = Q22_ONE;

    /* Airwindows DeRez A is frequency (1=transparent), B is resolution
     * (1=transparent); expensive cubic mappings are prepared here. */
    const float a = 1.0f - srr;
    const float b = 1.0f - bit;
    s->derez_target_srr = se ? fminf(a * a * a + 0.0005f, 1.0f) : 1.0f;
    s->derez_target_bit = be ? ((1.0f - b) * (1.0f - b) * (1.0f - b)) / 3.0f : 0.0f;
    s->derez_soften = (1.0f + s->derez_target_srr) * 0.5f;

    /* Airwindows DeRez3: A^3 sample-rate control and B*15+1 bit
     * exponent. BRICK reverses A/B so increasing P1/P2 increases damage. */
    if (s->engine == FX_AUDIO_LOFI_ENGINE_HARD
        || s->engine == FX_AUDIO_LOFI_ENGINE_SOFT)
    {
        const float rez_a = 1.0f - srr;
        const uint8_t bits = (uint8_t)(16U-(uint8_t)(bit*15.0f));
        const float bit_scale = (float)(1UL << bits);
        if (s->engine == FX_AUDIO_LOFI_ENGINE_HARD)
        {
            s->hybrid.bit_scale = bit_scale;
            s->hybrid.bit_reciprocal = 1.0f / bit_scale;
        }
        else
        {
            const float bit_factor=exp2f(((1.0f-bit)*15.0f)+1.0f);
            s->derez3.rez = se ? fmaxf((rez_a * rez_a * rez_a)
                                              * (44100.0f / 48000.0f), 0.0005f)
                                      : 1.0f;
            s->derez3.bit_factor = bit_factor;
            s->derez3.bit_inverse = 1.0f / bit_factor;
        }
    }
}

static float deluge_float_srr(fx_audio_lofi_state_t *s,
                              fx_audio_lofi_float_state_t *q,
                              float x, unsigned ch)
{
    if (q->low_pos < Q22_ONE) {
        const float b = (float)q->low_pos, a = (float)(Q22_MASK - q->low_pos);
        q->last_grabbed[ch] = q->grabbed[ch];
        q->grabbed[ch] = (q->last[ch] * a + x * b) * (1.0f / 8388608.0f);
        q->low_pos += s->low_increment;
        q->high_pos = (uint32_t)sat64((int64_t)qmul((int32_t)(q->low_pos & Q22_MASK),
                                                   (int32_t)(s->high_increment << 8)) * 4);
    }
    q->low_pos -= Q22_ONE;
    q->last[ch] = x;
    const uint32_t b = q->high_pos < Q22_ONE ? q->high_pos : Q22_MASK;
    x = (q->last_grabbed[ch] * (float)(Q22_MASK - b)
         + q->grabbed[ch] * (float)b) * (1.0f / 2097152.0f);
    q->high_pos += s->high_increment;
    return x;
}

static void derez_stereo(fx_audio_lofi_state_t *s, float *l, float *r)
{
    fx_audio_lofi_derez_state_t *d = &s->derez;
    const float dry[2] = {*l, *r};
    d->increment_srr = d->increment_srr * 0.999f + s->derez_target_srr * 0.001f;
    d->increment_bit = d->increment_bit * 0.999f + s->derez_target_bit * 0.001f;
    d->position += d->increment_srr;
    float out[2] = {d->held[0], d->held[1]};
    if (d->position > 1.0f) {
        d->position -= 1.0f;
        for (unsigned ch = 0; ch < 2; ++ch) {
            d->held[ch] = d->last[ch] * d->position + dry[ch] * (1.0f - d->position);
            out[ch] += (d->held[ch] - out[ch]) * s->derez_soften;
        }
    }
    if (d->increment_bit > 0.0005f) for (unsigned ch = 0; ch < 2; ++ch) {
        const float bins = out[ch] / d->increment_bit;
        out[ch] = ((out[ch] > 0.0f) ? ceilf(bins) : floorf(bins))
            * d->increment_bit;
        out[ch] *= 1.0f - d->increment_bit;
    }
    d->last[0] = dry[0]; d->last[1] = dry[1]; *l = out[0]; *r = out[1];
}

static float derez_mono(fx_audio_lofi_state_t *s, float x)
{
    fx_audio_lofi_derez_state_t *d=&s->derez; const float dry=x;
    d->increment_srr=d->increment_srr*0.999f+s->derez_target_srr*0.001f;
    d->increment_bit=d->increment_bit*0.999f+s->derez_target_bit*0.001f;
    d->position+=d->increment_srr; float out=d->held[0];
    if(d->position>1.0f){d->position-=1.0f;d->held[0]=d->last[0]*d->position+dry*(1.0f-d->position);out+=(d->held[0]-out)*s->derez_soften;}
    if(d->increment_bit>0.0005f){const float bins=out/d->increment_bit;out=((out>0.0f)?ceilf(bins):floorf(bins))*d->increment_bit;out*=1.0f-d->increment_bit;}
    d->last[0]=dry; return out;
}

enum { D3_AL, D3_BL, D3_CL, D3_INL, D3_UNINL, D3_SAMPL,
       D3_AR, D3_BR, D3_CR, D3_INR, D3_UNINR, D3_SAMPR, D3_CYCLE };

static float derez3_bit(float x,float bit_factor,float bit_inverse)
{
    const float offset=0.5f*bit_inverse;
    return floorf((x*bit_factor)+offset)*bit_inverse;
}

static float hard_pcm_bit(float x, float scale, float reciprocal)
{
    x=clamp1(x);
    return roundf(x*scale)*reciprocal;
}

static float hybrid(fx_audio_lofi_state_t *s, float x, unsigned ch)
{
    if ((s->mode & FX_AUDIO_LOFI_MODE_BIT_ONLY) != 0U)
        x = hard_pcm_bit(x, s->hybrid.bit_scale, s->hybrid.bit_reciprocal);
    if ((s->mode & FX_AUDIO_LOFI_MODE_SRR_ONLY) != 0U)
        x = deluge_float_srr(s, &s->hybrid.srr, x, ch);
    return clamp1(x);
}

static void derez3_stereo(fx_audio_lofi_state_t *s, float *l, float *r)
{
    fx_audio_lofi_derez3_state_t *d = &s->derez3;
    float in_l = *l, in_r = *r;

    if ((s->mode & FX_AUDIO_LOFI_MODE_BIT_ONLY) != 0U)
    {
        /* Preserve DeRez3's unusual offset-before-floor quantizer. */
        in_l = derez3_bit(in_l, d->bit_factor, d->bit_inverse);
        in_r = derez3_bit(in_r, d->bit_factor, d->bit_inverse);
    }
    if ((s->mode & FX_AUDIO_LOFI_MODE_SRR_ONLY) == 0U)
    {
        *l = in_l; *r = in_r;
        return;
    }

    const float rez = d->rez;
    d->bez[D3_CYCLE] += rez;
    d->bez[D3_SAMPL] += in_l * rez;
    d->bez[D3_SAMPR] += in_r * rez;
    if (d->bez[D3_CYCLE] > 1.0f)
    {
        d->bez[D3_CYCLE] -= 1.0f;
        d->bez[D3_CL] = d->bez[D3_BL];
        d->bez[D3_BL] = d->bez[D3_AL];
        d->bez[D3_AL] = in_l;
        d->bez[D3_SAMPL] = 0.0f;
        d->bez[D3_CR] = d->bez[D3_BR];
        d->bez[D3_BR] = d->bez[D3_AR];
        d->bez[D3_AR] = in_r;
        d->bez[D3_SAMPR] = 0.0f;
    }
    const float phase = d->bez[D3_CYCLE];
    const float inv_phase = 1.0f - phase;
    const float cbl = d->bez[D3_CL] * inv_phase + d->bez[D3_BL] * phase;
    const float cbr = d->bez[D3_CR] * inv_phase + d->bez[D3_BR] * phase;
    const float bal = d->bez[D3_BL] * inv_phase + d->bez[D3_AL] * phase;
    const float bar = d->bez[D3_BR] * inv_phase + d->bez[D3_AR] * phase;
    *l = (d->bez[D3_BL] + cbl * inv_phase + bal * phase) * 0.5f;
    *r = (d->bez[D3_BR] + cbr * inv_phase + bar * phase) * 0.5f;
}

static float derez3_mono(fx_audio_lofi_state_t *s,float x)
{
    fx_audio_lofi_derez3_state_t *d=&s->derez3;
    if((s->mode&FX_AUDIO_LOFI_MODE_BIT_ONLY)!=0U)x=derez3_bit(x,d->bit_factor,d->bit_inverse);
    if((s->mode&FX_AUDIO_LOFI_MODE_SRR_ONLY)==0U)return x;
    const float rez=d->rez;d->bez[D3_CYCLE]+=rez;d->bez[D3_SAMPL]+=x*rez;
    if(d->bez[D3_CYCLE]>1.0f){d->bez[D3_CYCLE]-=1.0f;d->bez[D3_CL]=d->bez[D3_BL];d->bez[D3_BL]=d->bez[D3_AL];d->bez[D3_AL]=x;d->bez[D3_SAMPL]=0.0f;}
    const float phase=d->bez[D3_CYCLE],inv=1.0f-phase;
    const float cb=d->bez[D3_CL]*inv+d->bez[D3_BL]*phase;
    const float ba=d->bez[D3_BL]*inv+d->bez[D3_AL]*phase;
    return (d->bez[D3_BL]+cb*inv+ba*phase)*0.5f;
}

static float one(fx_audio_lofi_state_t *s, float x, unsigned ch)
{
    if (s->mode == FX_AUDIO_LOFI_MODE_BYPASS) return x;
    switch (s->engine) {
        case FX_AUDIO_LOFI_ENGINE_HARD: return hybrid(s, x, ch);
        case FX_AUDIO_LOFI_ENGINE_MID: return derez_mono(s, x);
        default: return derez3_mono(s, x);
    }
}

float fx_audio_lofi_process_mono_sample(fx_audio_lofi_state_t *s, float x) { return s ? one(s, x, 0U) : x; }
void fx_audio_lofi_process_stereo_sample(fx_audio_lofi_state_t *s, float *l, float *r)
{
    if (!s || !l || !r || s->mode == FX_AUDIO_LOFI_MODE_BYPASS) return;
    if (s->engine == FX_AUDIO_LOFI_ENGINE_MID) derez_stereo(s, l, r);
    else if (s->engine == FX_AUDIO_LOFI_ENGINE_SOFT) derez3_stereo(s, l, r);
    else { *l = one(s, *l, 0U); *r = one(s, *r, 1U); }
}
void fx_audio_lofi_process_stereo(fx_audio_lofi_state_t *s, float *l, float *r, uint32_t n)
{
    if(!s||!l||!r||s->mode==FX_AUDIO_LOFI_MODE_BYPASS)return;
    if(s->engine==FX_AUDIO_LOFI_ENGINE_MID){for(uint32_t i=0;i<n;++i)derez_stereo(s,&l[i],&r[i]);}
    else if(s->engine==FX_AUDIO_LOFI_ENGINE_SOFT){for(uint32_t i=0;i<n;++i)derez3_stereo(s,&l[i],&r[i]);}
    else {for(uint32_t i=0;i<n;++i){l[i]=hybrid(s,l[i],0U);r[i]=hybrid(s,r[i],1U);}}
}
void fx_audio_lofi_process_mono(fx_audio_lofi_state_t *s, float *b, uint32_t n)
{
    if(!s||!b||s->mode==FX_AUDIO_LOFI_MODE_BYPASS)return;
    if(s->engine==FX_AUDIO_LOFI_ENGINE_MID){for(uint32_t i=0;i<n;++i)b[i]=derez_mono(s,b[i]);}
    else if(s->engine==FX_AUDIO_LOFI_ENGINE_SOFT){for(uint32_t i=0;i<n;++i)b[i]=derez3_mono(s,b[i]);}
    else {for(uint32_t i=0;i<n;++i)b[i]=hybrid(s,b[i],0U);}
}
