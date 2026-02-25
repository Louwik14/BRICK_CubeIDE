#include "fx_granular.h"

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

} // namespace

struct fx_granular_state {
  float sample_rate;
  float density;
  float pitch_semitones;
  float pitch_increment;
  float mix;
  bool freeze;
  float spread;
  float stereo_offset;
  float* buffer_l;
  float* buffer_r;
  uint32_t buffer_size;
  uint32_t write_pos;

  Grain grains[kMaxGrains];

  uint32_t rng;

  uint32_t spawn_counter;
  uint32_t spawn_interval;

  uint32_t free_index[kMaxGrains];
  uint32_t free_count;
};

namespace {

inline float clamp(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

inline uint32_t next_rand(fx_granular_state_t* s) {
  uint32_t x = s->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->rng = x;
  return x;
}

inline float rand_0_1(fx_granular_state_t* s) {
  return (next_rand(s) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline float fast_exp2_approx(float x) {
  const float x2 = x * x;
  const float x3 = x2 * x;
  return 1.0f + (0.69314718f * x) + (0.24022651f * x2) + (0.05550411f * x3);
}

inline void update_pitch_increment(fx_granular_state_t* s) {
  const float semitones = clamp(s->pitch_semitones, -48.0f, 48.0f);
  const float octaves = semitones * (1.0f / 12.0f);
  float ratio = fast_exp2_approx(octaves);
  if (ratio < 0.0625f) ratio = 0.0625f;
  if (ratio > 16.0f) ratio = 16.0f;
  s->pitch_increment = ratio * 65536.0f;
}

inline void update_spawn_probability(fx_granular_state_t* s) {
  float d = clamp(s->density, 0.0f, 1.0f);
  float grains_per_sec = 5.0f + d * 120.0f;

  s->spawn_interval = (uint32_t)(s->sample_rate / grains_per_sec);
  if (s->spawn_interval < 1u) s->spawn_interval = 1u;
}

inline float read_interp_fixed(const float* buf, uint32_t pos, uint32_t buffer_size) {
  uint32_t i0 = pos >> 16;
  uint32_t i1 = i0 + 1u;
  if (i1 >= buffer_size) i1 = 0u;

  float frac = (pos & 0xFFFFu) * (1.0f / 65536.0f);

  float a = buf[i0];
  return a + (buf[i1] - a) * frac;
}

constexpr float kInvNorm[] = {
    1.0000f, 1.0000f, 0.9000f, 0.8300f, 0.7700f, 0.7100f, 0.6700f,
    0.6300f, 0.6000f, 0.5700f, 0.5500f, 0.5300f, 0.5100f, 0.4950f,
    0.4800f, 0.4650f, 0.4500f, 0.4350f, 0.4200f, 0.4100f, 0.4000f
};

inline void spawn_grain(fx_granular_state_t* s) {
  if (s->free_count == 0u) return;

  uint32_t idx = s->free_index[--s->free_count];
  Grain& g = s->grains[idx];

  uint32_t max_span = (uint32_t)(s->spread * (float)s->buffer_size);
  if (max_span < 32u) max_span = 32u;
  float r = rand_0_1(s);
  r = r * r;
  uint32_t rand_span = (uint32_t)(r * max_span);
  uint32_t start = s->write_pos;
  start = (start >= rand_span) ? (start - rand_span)
                              : (start + s->buffer_size - rand_span);

  float pan = rand_0_1(s);

  uint32_t duration = kMinDurationSamples +
    (next_rand(s) % (kMaxDurationSamples - kMinDurationSamples));

  g.pos = start << 16;
  g.increment = (uint32_t)s->pitch_increment;
  g.gain_l = 1.0f - pan;
  g.gain_r = pan;
  g.env = 0.0f;
  g.remaining = duration;

  uint32_t half = duration >> 1;
  g.env_inc = (half > 0u) ? (1.0f / (float)half) : 1.0f;

  float jitter = (rand_0_1(s) - 0.5f) * 0.08f;
  g.env_inc *= (1.0f + jitter);

  g.interp_toggle = 0u;
  g.lastL = 0.0f;
  g.lastR = 0.0f;

  g.holdL = 0.0f;
  g.holdR = 0.0f;

  g.active = true;
}

} // namespace

extern "C" size_t fx_granular_state_size(void) {
  return sizeof(fx_granular_state_t);
}

extern "C" size_t fx_granular_buffer_size(void) {
  return sizeof(float) * kBufferSize;
}

extern "C" void fx_granular_set_density(fx_granular_state_t* s, float density_0_1) {
  if (!s) return;
  s->density = clamp(density_0_1, 0.0f, 1.0f);
  update_spawn_probability(s);
}

extern "C" void fx_granular_set_pitch(fx_granular_state_t* s, float semitones_m48_p48) {
  if (!s) return;
  s->pitch_semitones = clamp(semitones_m48_p48, -48.0f, 48.0f);
  update_pitch_increment(s);
}

extern "C" void fx_granular_set_freeze(fx_granular_state_t* s, bool freeze) {
  if (!s) return;
  s->freeze = freeze;
}

extern "C" void fx_granular_set_mix(fx_granular_state_t* s, float mix_0_1) {
  if (!s) return;
  s->mix = clamp(mix_0_1, 0.0f, 1.0f);
}

extern "C" void fx_granular_set_spread(fx_granular_state_t* s, float spread_0_1) {
  if (!s) return;
  s->spread = clamp(spread_0_1, 0.0f, 1.0f);
}

extern "C" void fx_granular_set_stereo_offset(fx_granular_state_t* s, float amount_0_1) {
  if (!s) return;
  s->stereo_offset = clamp(amount_0_1, 0.0f, 1.0f);
}

extern "C" void fx_granular_init(fx_granular_state_t* s,
                                  float sample_rate,
                                  float* buffer_l,
                                  float* buffer_r,
                                  uint32_t buffer_frames) {
  if (!s || !buffer_l || !buffer_r || (buffer_frames == 0u))
    return;

  s->sample_rate = sample_rate;
  s->density = 0.5f;
  s->pitch_semitones = 0.0f;
  s->mix = 1.0f;
  s->freeze = false;
  s->spread = 0.5f;
  s->stereo_offset = 0.5f;
  s->buffer_l = buffer_l;
  s->buffer_r = buffer_r;
  s->buffer_size = buffer_frames;
  s->write_pos = 0u;
  s->rng = 0x12345678u;

  s->spawn_counter = 0u;

  s->free_count = kMaxGrains;
  for (uint32_t i = 0u; i < kMaxGrains; ++i) {
    s->grains[i].active = false;
    s->free_index[i] = i;
  }

  for (uint32_t i = 0u; i < s->buffer_size; ++i) {
    s->buffer_l[i] = 0.0f;
    s->buffer_r[i] = 0.0f;
  }

  update_pitch_increment(s);
  update_spawn_probability(s);
}

extern "C" void fx_granular_process_block(fx_granular_state_t* s,
                                          float* in_l, float* in_r,
                                          float* out_l, float* out_r,
                                          uint32_t frames) {
  if (!s || !in_l || !in_r || !out_l || !out_r || !s->buffer_l || !s->buffer_r || (s->buffer_size == 0u))
    return;

  float wet_mix = s->mix;
  float dry_mix = 1.0f - wet_mix;
  float* bufL = s->buffer_l;
  float* bufR = s->buffer_r;

  const uint32_t limit = s->buffer_size << 16;
  const uint32_t offset_fixed = ((uint32_t)(s->stereo_offset * 512.0f)) << 16;

  for (uint32_t n = 0u; n < frames; ++n) {

    float inL = in_l[n];
    float inR = in_r[n];

    if (!s->freeze) {
      bufL[s->write_pos] = inL;
      bufR[s->write_pos] = inR;

      if (++s->write_pos >= s->buffer_size)
        s->write_pos = 0u;
    }

    if (++s->spawn_counter >= s->spawn_interval) {
      s->spawn_counter = 0u;
      spawn_grain(s);
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    uint32_t active_count = 0u;

    for (uint32_t i = 0u; i < kMaxGrains; ++i) {
      Grain& g = s->grains[i];
      if (!g.active) continue;

      if (g.interp_toggle == 0u) {
        g.lastL = read_interp_fixed(bufL, g.pos, s->buffer_size);

        uint32_t posR = g.pos + offset_fixed;
        if (posR >= limit) posR -= limit;
        g.lastR = read_interp_fixed(bufR, posR, s->buffer_size);
      }
      g.interp_toggle ^= 1u;

      constexpr float kSlew = 0.35f;
      g.holdL += (g.lastL - g.holdL) * kSlew;
      g.holdR += (g.lastR - g.holdR) * kSlew;

      float sL = g.holdL;
      float sR = g.holdR;

      float x = g.env;
      float env = x * (2.0f - x);

      if (g.remaining < kReleaseSamples) {
        env *= (float)g.remaining * (1.0f / (float)kReleaseSamples);
      }

      wetL += sL * env * g.gain_l;
      wetR += sR * env * g.gain_r;
      ++active_count;

      g.pos += g.increment;
      if (g.pos >= limit)
        g.pos -= limit;

      g.env += g.env_inc;

      if (--g.remaining == 0u) {
        g.active = false;
        s->free_index[s->free_count++] = i;
      }
    }

    float norm = kInvNorm[active_count];
    wetL *= norm;
    wetR *= norm;

    out_l[n] = inL * dry_mix + wetL * wet_mix;
    out_r[n] = inR * dry_mix + wetR * wet_mix;
  }
}
