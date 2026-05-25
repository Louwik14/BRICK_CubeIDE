#include "adsr_daisy.h"
#include "adsr_daisy_c.h"
#include <math.h>

using namespace daisysp;


void Adsr::Init(float sample_rate, int blockSize)
{
    sample_rate_  = sample_rate / blockSize;
    attackShape_  = -1.f;
    attackTarget_ = 0.0f;
    attackTime_   = -1.f;
    decayTime_    = -1.f;
    releaseTime_  = -1.f;
    sus_level_    = 0.7f;
    x_            = 0.0f;
    gate_         = false;
    mode_         = ADSR_SEG_IDLE;

    SetTime(ADSR_SEG_ATTACK, 0.1f);
    SetTime(ADSR_SEG_DECAY, 0.1f);
    SetTime(ADSR_SEG_RELEASE, 0.1f);
}

void Adsr::Retrigger(bool hard)
{
    mode_ = ADSR_SEG_ATTACK;
    if(hard)
        x_ = 0.f;
}

void Adsr::SetTime(int seg, float time)
{
    switch(seg)
    {
        case ADSR_SEG_ATTACK: SetAttackTime(time, 0.0f); break;
        case ADSR_SEG_DECAY:
        {
            SetTimeConstant(time, decayTime_, decayD0_);
        }
        break;
        case ADSR_SEG_RELEASE:
        {
            SetTimeConstant(time, releaseTime_, releaseD0_);
        }
        break;
        default: return;
    }
}

void Adsr::SetAttackTime(float timeInS, float shape)
{
    if((timeInS != attackTime_) || (shape != attackShape_))
    {
        attackTime_  = timeInS;
        attackShape_ = shape;
        if(timeInS > 0.f)
        {
            float x         = shape;
            float target    = 9.f * powf(x, 10.f) + 0.3f * x + 1.01f;
            attackTarget_   = target;
            float logTarget = logf(1.f - (1.f / target)); // -1 for decay
            attackD0_       = 1.f - expf(logTarget / (timeInS * sample_rate_));
        }
        else
            attackD0_ = 1.f; // instant change
    }
}
void Adsr::SetDecayTime(float timeInS)
{
    SetTimeConstant(timeInS, decayTime_, decayD0_);
}
void Adsr::SetReleaseTime(float timeInS)
{
    SetTimeConstant(timeInS, releaseTime_, releaseD0_);
}


void Adsr::SetTimeConstant(float timeInS, float& time, float& coeff)
{
    if(timeInS != time)
    {
        time = timeInS;
        if(time > 0.f)
        {
            const float target = logf(1. / M_E);
            coeff              = 1.f - expf(target / (time * sample_rate_));
        }
        else
            coeff = 1.f; // instant change
    }
}


float Adsr::Process(bool gate)
{
    float out = 0.0f;

    if(gate && !gate_) // rising edge
        mode_ = ADSR_SEG_ATTACK;
    else if(!gate && gate_) // falling edge
        mode_ = ADSR_SEG_RELEASE;
    gate_ = gate;

    float D0(attackD0_);
    if(mode_ == ADSR_SEG_DECAY)
        D0 = decayD0_;
    else if(mode_ == ADSR_SEG_RELEASE)
        D0 = releaseD0_;

    float target = mode_ == ADSR_SEG_DECAY ? sus_level_ : -0.01f;
    switch(mode_)
    {
        case ADSR_SEG_IDLE: out = 0.0f; break;
        case ADSR_SEG_ATTACK:
            x_ += D0 * (attackTarget_ - x_);
            out = x_;
            if(out > 1.f)
            {
                x_ = out = 1.f;
                mode_    = ADSR_SEG_DECAY;
            }
            break;
        case ADSR_SEG_DECAY:
        case ADSR_SEG_RELEASE:
            x_ += D0 * (target - x_);
            out = x_;
            if(out < 0.0f)
            {
                x_ = out = 0.f;
                mode_    = ADSR_SEG_IDLE;
            }
        default: break;
    }
    return out;
}

extern "C" {

enum
{
    ADSR_DAISY_C_SEG_IDLE = 0,
    ADSR_DAISY_C_SEG_ATTACK = 1,
    ADSR_DAISY_C_SEG_DECAY = 2,
    ADSR_DAISY_C_SEG_SUSTAIN = 3,
    ADSR_DAISY_C_SEG_RELEASE = 4
};

static constexpr float kAdsrDaisyCSustainEpsilon = 1.0e-5f;
static constexpr uint32_t kAdsrDaisyCLutSize = 512U;
static constexpr float kAdsrDaisyCMinTimeS = 0.001f;
static constexpr float kAdsrDaisyCMaxTimeS = 5.0f;
static constexpr float kAdsrDaisyCDefaultSampleRate = 48000.0f;
static constexpr float kAdsrDaisyCLog2MinTime = -9.965784284662087f;
static constexpr float kAdsrDaisyCInvLog2TimeRange = 0.08138130233100371f;
static constexpr float kAdsrDaisyCInvLog2 = 1.4426950408889634f;

static float g_adsr_daisy_c_time_coeff_lut[kAdsrDaisyCLutSize + 1U];
static float g_adsr_daisy_c_attack_coeff_lut[kAdsrDaisyCLutSize + 1U];
static float g_adsr_daisy_c_log2_lut[257U];
static uint8_t g_adsr_daisy_c_lut_ready = 0U;

static float adsr_daisy_c_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void adsr_daisy_c_init_luts()
{
    if(g_adsr_daisy_c_lut_ready != 0U)
        return;

    constexpr float attack_target = 1.01f;
    const float attack_log_target = logf(1.0f - (1.0f / attack_target));
    const float time_log_ratio = logf(kAdsrDaisyCMaxTimeS / kAdsrDaisyCMinTimeS);

    for(uint32_t i = 0U; i <= 256U; ++i)
    {
        const float x = 1.0f + ((float)i * (1.0f / 256.0f));
        g_adsr_daisy_c_log2_lut[i] = logf(x) * kAdsrDaisyCInvLog2;
    }

    for(uint32_t i = 0U; i <= kAdsrDaisyCLutSize; ++i)
    {
        const float norm = (float)i * (1.0f / (float)kAdsrDaisyCLutSize);
        const float time_s = kAdsrDaisyCMinTimeS * expf(time_log_ratio * norm);
        g_adsr_daisy_c_time_coeff_lut[i] = 1.0f - expf(-1.0f / (time_s * kAdsrDaisyCDefaultSampleRate));
        g_adsr_daisy_c_attack_coeff_lut[i] = 1.0f - expf(attack_log_target / (time_s * kAdsrDaisyCDefaultSampleRate));
    }

    g_adsr_daisy_c_lut_ready = 1U;
}

static float adsr_daisy_c_log2_lut(float x)
{
    union
    {
        float f;
        uint32_t u;
    } bits;

    bits.f = adsr_daisy_c_clampf(x, 1.0e-20f, 1.0e20f);
    const int32_t exponent = (int32_t)((bits.u >> 23) & 0xffU) - 127;
    const uint32_t mantissa_bits = bits.u & 0x7fffffU;
    uint32_t index = mantissa_bits >> 15;
    if(index >= 256U)
        index = 255U;

    const float frac = (float)(mantissa_bits & 0x7fffU) * (1.0f / 32768.0f);
    const float a = g_adsr_daisy_c_log2_lut[index];
    const float b = g_adsr_daisy_c_log2_lut[index + 1U];
    return (float)exponent + a + ((b - a) * frac);
}

static float adsr_daisy_c_time_lut_lookup(const float *table, float time_s)
{
    const float clamped = adsr_daisy_c_clampf(time_s, kAdsrDaisyCMinTimeS, kAdsrDaisyCMaxTimeS);
    float pos = (adsr_daisy_c_log2_lut(clamped) - kAdsrDaisyCLog2MinTime) * kAdsrDaisyCInvLog2TimeRange * (float)kAdsrDaisyCLutSize;
    if(pos <= 0.0f)
        return table[0];
    if(pos >= (float)kAdsrDaisyCLutSize)
        return table[kAdsrDaisyCLutSize];

    const uint32_t index = (uint32_t)pos;
    const float frac = pos - (float)index;
    const float a = table[index];
    const float b = table[index + 1U];
    return a + ((b - a) * frac);
}

static void adsr_daisy_c_set_time_constant(float time_s, float sample_rate, float *time, float *coeff)
{
    if((time == nullptr) || (coeff == nullptr))
        return;

    if(time_s != *time)
    {
        *time = time_s;
        if(time_s > 0.0f)
        {
            if((sample_rate > 47999.0f) && (sample_rate < 48001.0f))
            {
                *coeff = adsr_daisy_c_time_lut_lookup(g_adsr_daisy_c_time_coeff_lut, time_s);
            }
            else
            {
                *coeff = 1.0f - expf(-1.0f / (time_s * sample_rate));
            }
        }
        else
        {
            *coeff = 1.0f;
        }
    }
}

static void adsr_daisy_c_recover_state_if_needed(adsr_daisy_c_t *env)
{
    if(env == nullptr)
        return;

    if(env->sample_rate <= 1.0f)
    {
        env->sample_rate = 48000.0f;
    }

    if((env->sus_level < -0.01f) || (env->sus_level > 1.0f))
    {
        env->sus_level = 1.0f;
    }

    if((env->attack_target <= 0.0f) || (env->attack_d0 <= 0.0f))
    {
        const float attack = (env->attack_time > 0.0f) ? env->attack_time : 0.001f;
        adsr_daisy_c_set_attack(env, attack);
    }
    if(env->decay_d0 <= 0.0f)
    {
        const float decay = (env->decay_time > 0.0f) ? env->decay_time : 0.001f;
        adsr_daisy_c_set_decay(env, decay);
    }
    if(env->release_d0 <= 0.0f)
    {
        const float release = (env->release_time > 0.0f) ? env->release_time : 0.001f;
        adsr_daisy_c_set_release(env, release);
    }
}

void adsr_daisy_c_init(adsr_daisy_c_t *env, float sample_rate, uint32_t block_size)
{
    adsr_daisy_c_init_luts();
    if(env == nullptr)
        return;

    const uint32_t bs = (block_size > 0U) ? block_size : 1U;
    env->sample_rate = sample_rate / (float)bs;
    env->sus_level = 0.7f;
    env->x = 0.0f;
    env->attack_shape = -1.0f;
    env->attack_target = 0.0f;
    env->attack_time = -1.0f;
    env->decay_time = -1.0f;
    env->release_time = -1.0f;
    env->attack_d0 = 0.0f;
    env->decay_d0 = 0.0f;
    env->release_d0 = 0.0f;
    env->mode = ADSR_DAISY_C_SEG_IDLE;
    env->gate = 0U;

    adsr_daisy_c_set_attack(env, 0.1f);
    adsr_daisy_c_set_decay(env, 0.1f);
    adsr_daisy_c_set_release(env, 0.1f);
}

void adsr_daisy_c_reset(adsr_daisy_c_t *env)
{
    if(env == nullptr)
        return;

    env->x = 0.0f;
    env->gate = 0U;
    env->mode = ADSR_DAISY_C_SEG_IDLE;
}

void adsr_daisy_c_set_attack(adsr_daisy_c_t *env, float time_s)
{
    if(env == nullptr)
        return;

    const float shape = 0.0f;
    if((time_s != env->attack_time) || (shape != env->attack_shape))
    {
        env->attack_time = time_s;
        env->attack_shape = shape;
        env->attack_target = 1.01f;
        if(time_s > 0.0f)
        {
            if((env->sample_rate > 47999.0f) && (env->sample_rate < 48001.0f))
            {
                env->attack_d0 = adsr_daisy_c_time_lut_lookup(g_adsr_daisy_c_attack_coeff_lut, time_s);
            }
            else
            {
                const float log_target = logf(1.0f - (1.0f / env->attack_target));
                env->attack_d0 = 1.0f - expf(log_target / (time_s * env->sample_rate));
            }
        }
        else
        {
            env->attack_d0 = 1.0f;
        }
    }
}

void adsr_daisy_c_set_decay(adsr_daisy_c_t *env, float time_s)
{
    if(env == nullptr)
        return;

    adsr_daisy_c_set_time_constant(time_s, env->sample_rate, &env->decay_time, &env->decay_d0);
}

void adsr_daisy_c_set_sustain(adsr_daisy_c_t *env, float sustain)
{
    if(env == nullptr)
        return;

    sustain = (sustain <= 0.0f) ? -0.01f : (sustain > 1.0f) ? 1.0f : sustain;
    env->sus_level = sustain;
}

void adsr_daisy_c_set_release(adsr_daisy_c_t *env, float time_s)
{
    if(env == nullptr)
        return;

    adsr_daisy_c_set_time_constant(time_s, env->sample_rate, &env->release_time, &env->release_d0);
}

void adsr_daisy_c_retrigger(adsr_daisy_c_t *env, uint8_t hard)
{
    if(env == nullptr)
        return;

    adsr_daisy_c_recover_state_if_needed(env);
    env->mode = ADSR_DAISY_C_SEG_ATTACK;
    if(hard != 0U)
    {
        env->x = 0.0f;
    }
}

float adsr_daisy_c_process(adsr_daisy_c_t *env, uint8_t gate)
{
    if(env == nullptr)
        return 0.0f;

    float out = 0.0f;
    const uint8_t gate_high = (gate != 0U) ? 1U : 0U;

    if((gate_high != 0U) && (env->gate == 0U))
    {
        env->mode = ADSR_DAISY_C_SEG_ATTACK;
    }
    else if((gate_high == 0U) && (env->gate != 0U))
    {
        env->mode = ADSR_DAISY_C_SEG_RELEASE;
    }
    env->gate = gate_high;

    float d0 = env->attack_d0;
    if(env->mode == ADSR_DAISY_C_SEG_DECAY)
    {
        d0 = env->decay_d0;
    }
    else if(env->mode == ADSR_DAISY_C_SEG_RELEASE)
    {
        d0 = env->release_d0;
    }

    const float target = (env->mode == ADSR_DAISY_C_SEG_DECAY) ? env->sus_level : -0.01f;
    switch(env->mode)
    {
        case ADSR_DAISY_C_SEG_IDLE:
            out = 0.0f;
            break;
        case ADSR_DAISY_C_SEG_SUSTAIN:
            env->x = env->sus_level;
            out = env->x;
            break;
        case ADSR_DAISY_C_SEG_ATTACK:
            env->x += d0 * (env->attack_target - env->x);
            out = env->x;
            if(out > 1.0f)
            {
                env->x = 1.0f;
                out = 1.0f;
                env->mode = ADSR_DAISY_C_SEG_DECAY;
            }
            break;
        case ADSR_DAISY_C_SEG_DECAY:
        case ADSR_DAISY_C_SEG_RELEASE:
            env->x += d0 * (target - env->x);
            out = env->x;
            if((env->mode == ADSR_DAISY_C_SEG_DECAY)
               && (fabsf(out - env->sus_level) <= kAdsrDaisyCSustainEpsilon))
            {
                env->x = env->sus_level;
                out = env->x;
                env->mode = ADSR_DAISY_C_SEG_SUSTAIN;
            }
            if(out < 0.0f)
            {
                env->x = 0.0f;
                out = 0.0f;
                env->mode = ADSR_DAISY_C_SEG_IDLE;
            }
            break;
        default:
            break;
    }

    return out;
}

uint8_t adsr_daisy_c_is_running(const adsr_daisy_c_t *env)
{
    if(env == nullptr)
        return 0U;

    return (env->mode != ADSR_DAISY_C_SEG_IDLE) ? 1U : 0U;
}

uint8_t adsr_daisy_c_is_sustaining(const adsr_daisy_c_t *env)
{
    if(env == nullptr)
        return 0U;

    return (env->mode == ADSR_DAISY_C_SEG_SUSTAIN) ? 1U : 0U;
}

float adsr_daisy_c_current_level(const adsr_daisy_c_t *env)
{
    if(env == nullptr)
        return 0.0f;

    return env->x;
}

} // extern "C"
