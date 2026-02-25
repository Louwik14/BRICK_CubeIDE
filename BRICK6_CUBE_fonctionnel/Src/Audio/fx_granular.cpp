#include "fx_granular.h"

#include <string.h>

namespace {

constexpr uint32_t kBufferSize = 48000u;
constexpr uint32_t kMaxGrains = 20u;
constexpr uint32_t kMinDurationSamples = 480u;
constexpr uint32_t kMaxDurationSamples = 5760u;
constexpr uint32_t kReleaseSamples = 4u;

struct Grain {
  uint32_t pos;
  uint32_t increment;
  float gain_l;
  float gain_r;
  float env;
  float env_inc;
  uint32_t remaining;
  bool active;

  uint8_t interp_toggle;
  float lastL;
  float lastR;
  float holdL;
  float holdR;
};

struct GranularState {
  float sample_rate;
  float density;
  float pitch_semitones;
  float pitch_increment;
  float mix;
  bool freeze;
  float spread;
  float stereo_offset;

  float *buffer_l;
  float *buffer_r;
  uint32_t write_pos;

  Grain grains[kMaxGrains];

  uint32_t rng;
  uint32_t spawn_counter;
  uint32_t spawn_interval;

  uint32_t free_index[kMaxGrains];
  uint32_t free_count;
};

struct GranularDefaults {
  float sample_rate;
  float density;
  float pitch_semitones;
  float mix;
  bool freeze;
  float spread;
  float stereo_offset;
};

static GranularDefaults g_defaults = {
  48000.0f,
  0.5f,
  0.0f,
  1.0f,
  false,
  0.5f,
  0.5f
};

static GranularState *g_bound_state = nullptr;

constexpr float kInvNorm[] = {
    1.0000f, 1.0000f, 0.9000f, 0.8300f, 0.7700f, 0.7100f, 0.6700f,
    0.6300f, 0.6000f, 0.5700f, 0.5500f, 0.5300f, 0.5100f, 0.4950f,
    0.4800f, 0.4650f, 0.4500f, 0.4350f, 0.4200f, 0.4100f, 0.4000f
};

inline float clamp(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

inline uint32_t next_rand(GranularState *st) {
  uint32_t x = st->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  st->rng = x;
  return x;
}

inline float rand_0_1(GranularState *st) {
  return (next_rand(st) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline float fast_exp2_approx(float x) {
  const float x2 = x * x;
  const float x3 = x2 * x;
  return 1.0f + (0.69314718f * x) + (0.24022651f * x2) + (0.05550411f * x3);
}

inline void update_pitch_increment(GranularState *st) {
  const float semitones = clamp(st->pitch_semitones, -48.0f, 48.0f);
  const float octaves = semitones * (1.0f / 12.0f);
  float ratio = fast_exp2_approx(octaves);
  if (ratio < 0.0625f) ratio = 0.0625f;
  if (ratio > 16.0f) ratio = 16.0f;
  st->pitch_increment = ratio * 65536.0f;
}

inline void update_spawn_probability(GranularState *st) {
  float d = clamp(st->density, 0.0f, 1.0f);
  float grains_per_sec = 5.0f + d * 120.0f;

  st->spawn_interval = (uint32_t)(st->sample_rate / grains_per_sec);
  if (st->spawn_interval < 1) st->spawn_interval = 1;
}

inline float read_interp_fixed(const float* buf, uint32_t pos) {
  uint32_t i0 = pos >> 16;
  uint32_t i1 = i0 + 1;
  if (i1 >= kBufferSize) i1 = 0;

  float frac = (pos & 0xFFFF) * (1.0f / 65536.0f);

  float a = buf[i0];
  return a + (buf[i1] - a) * frac;
}

inline void spawn_grain(GranularState *st) {
  if (st->free_count == 0) return;

  uint32_t idx = st->free_index[--st->free_count];
  Grain& g = st->grains[idx];

  uint32_t max_span = (uint32_t)(st->spread * (float)kBufferSize);
  if (max_span < 32u) max_span = 32u;
  float r = rand_0_1(st);
  r = r * r;
  uint32_t rand_span = (uint32_t)(r * max_span);
  uint32_t start = st->write_pos;
  start = (start >= rand_span) ? (start - rand_span)
                              : (start + kBufferSize - rand_span);

  float pan = rand_0_1(st);

  uint32_t duration = kMinDurationSamples +
    (next_rand(st) % (kMaxDurationSamples - kMinDurationSamples));

  g.pos = start << 16;
  g.increment = (uint32_t)st->pitch_increment;
  g.gain_l = 1.0f - pan;
  g.gain_r = pan;
  g.env = 0.0f;
  g.remaining = duration;

  uint32_t half = duration >> 1;
  g.env_inc = (half > 0u) ? (1.0f / (float)half) : 1.0f;

  float jitter = (rand_0_1(st) - 0.5f) * 0.08f;
  g.env_inc *= (1.0f + jitter);

  g.interp_toggle = 0;
  g.lastL = 0.0f;
  g.lastR = 0.0f;
  g.holdL = 0.0f;
  g.holdR = 0.0f;

  g.active = true;
}

inline GranularState *state_from_mem(void *mem)
{
  return reinterpret_cast<GranularState *>(mem);
}

} // namespace

extern "C" size_t fx_granular_state_size(void)
{
  return sizeof(GranularState) + (size_t)(2u * kBufferSize * sizeof(float));
}

extern "C" void fx_granular_init_state(void *mem, float sample_rate)
{
  if(mem == nullptr)
    return;

  uint8_t *base = reinterpret_cast<uint8_t *>(mem);
  GranularState *st = reinterpret_cast<GranularState *>(base);
  memset(st, 0, sizeof(*st));

  float *buf = reinterpret_cast<float *>(base + sizeof(GranularState));
  st->buffer_l = buf;
  st->buffer_r = buf + kBufferSize;

  st->sample_rate = sample_rate;
  st->density = g_defaults.density;
  st->pitch_semitones = g_defaults.pitch_semitones;
  st->mix = g_defaults.mix;
  st->freeze = g_defaults.freeze;
  st->spread = g_defaults.spread;
  st->stereo_offset = g_defaults.stereo_offset;
  st->write_pos = 0u;
  st->rng = 0x12345678u;
  st->spawn_counter = 0u;

  st->free_count = kMaxGrains;
  for(uint32_t i = 0; i < kMaxGrains; ++i)
  {
    st->grains[i].active = false;
    st->free_index[i] = i;
  }

  for(uint32_t i = 0; i < kBufferSize; ++i)
  {
    st->buffer_l[i] = 0.0f;
    st->buffer_r[i] = 0.0f;
  }

  update_pitch_increment(st);
  update_spawn_probability(st);

  g_bound_state = st;
}

extern "C" void fx_granular_process_state(void *mem,
                                           float *inout_l,
                                           float *inout_r,
                                           uint32_t frames)
{
  GranularState *st = state_from_mem(mem);
  if(st == nullptr || inout_l == nullptr || inout_r == nullptr)
    return;

  const float wet_mix = st->mix;
  const float dry_mix = 1.0f - wet_mix;

  const uint32_t limit = kBufferSize << 16;
  const uint32_t offset_fixed = ((uint32_t)(st->stereo_offset * 512.0f)) << 16;

  for(uint32_t n = 0; n < frames; ++n)
  {
    const float inL = inout_l[n];
    const float inR = inout_r[n];

    if(!st->freeze)
    {
      st->buffer_l[st->write_pos] = inL;
      st->buffer_r[st->write_pos] = inR;

      if(++st->write_pos >= kBufferSize)
        st->write_pos = 0u;
    }

    if(++st->spawn_counter >= st->spawn_interval)
    {
      st->spawn_counter = 0;
      spawn_grain(st);
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    uint32_t active_count = 0u;

    for(uint32_t i = 0; i < kMaxGrains; ++i)
    {
      Grain& g = st->grains[i];
      if(!g.active) continue;

      if(g.interp_toggle == 0)
      {
        g.lastL = read_interp_fixed(st->buffer_l, g.pos);

        uint32_t posR = g.pos + offset_fixed;
        if(posR >= limit) posR -= limit;
        g.lastR = read_interp_fixed(st->buffer_r, posR);
      }
      g.interp_toggle ^= 1;

      constexpr float kSlew = 0.35f;
      g.holdL += (g.lastL - g.holdL) * kSlew;
      g.holdR += (g.lastR - g.holdR) * kSlew;

      const float sL = g.holdL;
      const float sR = g.holdR;

      const float x = g.env;
      float env = x * (2.0f - x);

      if(g.remaining < kReleaseSamples)
      {
        env *= (float)g.remaining * (1.0f / (float)kReleaseSamples);
      }

      wetL += sL * env * g.gain_l;
      wetR += sR * env * g.gain_r;
      ++active_count;

      g.pos += g.increment;
      if(g.pos >= limit)
        g.pos -= limit;

      g.env += g.env_inc;

      if(--g.remaining == 0u)
      {
        g.active = false;
        st->free_index[st->free_count++] = i;
      }
    }

    const float norm = kInvNorm[active_count];
    wetL *= norm;
    wetR *= norm;

    inout_l[n] = inL * dry_mix + wetL * wet_mix;
    inout_r[n] = inR * dry_mix + wetR * wet_mix;
  }
}

/* Compat API */
extern "C" void fx_granular_init(float sample_rate)
{
  g_defaults.sample_rate = sample_rate;
}

extern "C" void fx_granular_process_block(float* in_l, float* in_r,
                                          float* out_l, float* out_r,
                                          uint32_t frames)
{
  (void)out_l;
  (void)out_r;
  fx_granular_process_state(g_bound_state, in_l, in_r, frames);
}

extern "C" void fx_granular_set_density(float density_0_1)
{
  g_defaults.density = clamp(density_0_1, 0.0f, 1.0f);
  if(g_bound_state)
  {
    g_bound_state->density = g_defaults.density;
    update_spawn_probability(g_bound_state);
  }
}

extern "C" void fx_granular_set_pitch(float semitones_m48_p48)
{
  g_defaults.pitch_semitones = clamp(semitones_m48_p48, -48.0f, 48.0f);
  if(g_bound_state)
  {
    g_bound_state->pitch_semitones = g_defaults.pitch_semitones;
    update_pitch_increment(g_bound_state);
  }
}

extern "C" void fx_granular_set_freeze(bool freeze)
{
  g_defaults.freeze = freeze;
  if(g_bound_state)
    g_bound_state->freeze = freeze;
}

extern "C" void fx_granular_set_mix(float mix_0_1)
{
  g_defaults.mix = clamp(mix_0_1, 0.0f, 1.0f);
  if(g_bound_state)
    g_bound_state->mix = g_defaults.mix;
}

extern "C" void fx_granular_set_spread(float spread_0_1)
{
  g_defaults.spread = clamp(spread_0_1, 0.0f, 1.0f);
  if(g_bound_state)
    g_bound_state->spread = g_defaults.spread;
}

extern "C" void fx_granular_set_stereo_offset(float amount_0_1)
{
  g_defaults.stereo_offset = clamp(amount_0_1, 0.0f, 1.0f);
  if(g_bound_state)
    g_bound_state->stereo_offset = g_defaults.stereo_offset;
}
