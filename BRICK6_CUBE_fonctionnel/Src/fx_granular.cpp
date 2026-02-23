#include "fx_granular.h"

namespace {

constexpr uint32_t kBufferSize = 48000u;
constexpr uint32_t kMaxGrains = 20u;
constexpr uint32_t kMinDurationSamples = 480u;    // 10 ms @ 48k
constexpr uint32_t kMaxDurationSamples = 5760u;   // 120 ms @ 48k
constexpr float kBaseSpawnScale = 0.22f;

struct Grain {
  float pos;
  float increment;
  float gain_l;
  float gain_r;
  float env;
  float env_inc;
  uint32_t remaining;
  bool active;
};

struct GranularState {
  float sample_rate;
  float density;
  float pitch_semitones;
  float pitch_increment;
  float mix;
  bool freeze;

  float buffer_l[kBufferSize];
  float buffer_r[kBufferSize];
  uint32_t write_pos;

  Grain grains[kMaxGrains];

  uint32_t rng;
  float spawn_probability;
};

GranularState g_state;

constexpr float kInvNorm[kMaxGrains + 1u] = {
    1.0000f, 1.0000f, 0.9000f, 0.8300f, 0.7700f, 0.7100f, 0.6700f,
    0.6300f, 0.6000f, 0.5700f, 0.5500f, 0.5300f, 0.5100f, 0.4950f,
    0.4800f, 0.4650f, 0.4500f, 0.4350f, 0.4200f, 0.4100f, 0.4000f};

inline float clamp(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

inline uint32_t next_rand() {
  // xorshift32
  uint32_t x = g_state.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_state.rng = x;
  return x;
}

inline float rand_0_1() {
  return static_cast<float>(next_rand() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline float fast_exp2_approx(float x) {
  // Cubic fit on [-4, 4], sufficient for musical pitch control.
  const float x2 = x * x;
  const float x3 = x2 * x;
  return 1.0f + (0.69314718f * x) + (0.24022651f * x2) + (0.05550411f * x3);
}

inline void update_pitch_increment() {
  const float semitones = clamp(g_state.pitch_semitones, -48.0f, 48.0f);
  const float octaves = semitones / 12.0f;
  float ratio = fast_exp2_approx(octaves);
  if (ratio < 0.0625f) ratio = 0.0625f;
  if (ratio > 16.0f) ratio = 16.0f;
  g_state.pitch_increment = ratio;
}

inline void update_spawn_probability() {
  g_state.spawn_probability = clamp(g_state.density, 0.0f, 1.0f) * kBaseSpawnScale;
}

inline float read_interp(const float* buf, float pos) {
  int32_t i0 = static_cast<int32_t>(pos);
  if (i0 < 0) i0 += static_cast<int32_t>(kBufferSize);
  if (i0 >= static_cast<int32_t>(kBufferSize)) i0 -= static_cast<int32_t>(kBufferSize);

  int32_t i1 = i0 + 1;
  if (i1 >= static_cast<int32_t>(kBufferSize)) i1 = 0;

  const float frac = pos - static_cast<float>(i0);
  const float a = buf[i0];
  const float b = buf[i1];
  return a + (b - a) * frac;
}

inline void spawn_grain() {
  for (uint32_t i = 0; i < kMaxGrains; ++i) {
    Grain& g = g_state.grains[i];
    if (g.active) {
      continue;
    }

    const uint32_t rand_span = static_cast<uint32_t>(rand_0_1() * 24000.0f) + 32u;
    uint32_t start = g_state.write_pos;
    if (start >= rand_span) {
      start -= rand_span;
    } else {
      start += (kBufferSize - rand_span);
    }

    const float pan = rand_0_1();
    const uint32_t duration = kMinDurationSamples +
                              static_cast<uint32_t>(rand_0_1() * static_cast<float>(kMaxDurationSamples - kMinDurationSamples));

    g.pos = static_cast<float>(start);
    g.increment = g_state.pitch_increment;
    g.gain_l = 1.0f - pan;
    g.gain_r = pan;
    g.env = 0.0f;
    g.remaining = duration;

    const uint32_t half_duration = duration >> 1;
    if (half_duration > 0u) {
      g.env_inc = 1.0f / static_cast<float>(half_duration);
    } else {
      g.env_inc = 1.0f;
    }

    g.active = true;
    return;
  }
}

}  // namespace

extern "C" void fx_granular_set_density(float density_0_1) {
  g_state.density = clamp(density_0_1, 0.0f, 1.0f);
  update_spawn_probability();
}

extern "C" void fx_granular_set_pitch(float semitones_m48_p48) {
  g_state.pitch_semitones = clamp(semitones_m48_p48, -48.0f, 48.0f);
  update_pitch_increment();
}

extern "C" void fx_granular_set_freeze(bool freeze) {
  g_state.freeze = freeze;
}

extern "C" void fx_granular_set_mix(float mix_0_1) {
  g_state.mix = clamp(mix_0_1, 0.0f, 1.0f);
}

extern "C" void fx_granular_init(float sample_rate) {
  g_state.sample_rate = sample_rate;
  g_state.density = 0.5f;
  g_state.pitch_semitones = 0.0f;
  g_state.mix = 1.0f;
  g_state.freeze = false;
  g_state.write_pos = 0u;
  g_state.rng = 0x12345678u;

  for (uint32_t i = 0; i < kBufferSize; ++i) {
    g_state.buffer_l[i] = 0.0f;
    g_state.buffer_r[i] = 0.0f;
  }

  for (uint32_t i = 0; i < kMaxGrains; ++i) {
    g_state.grains[i].active = false;
  }

  update_pitch_increment();
  update_spawn_probability();
}

extern "C" void fx_granular_process_block(float* in_l, float* in_r,
                                           float* out_l, float* out_r,
                                           uint32_t frames) {
  if (!in_l || !in_r || !out_l || !out_r) {
    return;
  }

  const float dry_mix = 1.0f - g_state.mix;
  const float wet_mix = g_state.mix;

  for (uint32_t n = 0; n < frames; ++n) {
    const float inL = in_l[n];
    const float inR = in_r[n];

    if (!g_state.freeze) {
      g_state.buffer_l[g_state.write_pos] = inL;
      g_state.buffer_r[g_state.write_pos] = inR;
      ++g_state.write_pos;
      if (g_state.write_pos >= kBufferSize) {
        g_state.write_pos = 0u;
      }
    }

    if (rand_0_1() < g_state.spawn_probability) {
      spawn_grain();
    }
    if (rand_0_1() < (g_state.spawn_probability * 0.35f)) {
      spawn_grain();
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    uint32_t active_count = 0u;

    for (uint32_t i = 0; i < kMaxGrains; ++i) {
      Grain& g = g_state.grains[i];
      if (!g.active) {
        continue;
      }

      const float sL = read_interp(g_state.buffer_l, g.pos);
      const float sR = read_interp(g_state.buffer_r, g.pos);
      const float env = (g.env <= 1.0f) ? g.env : (2.0f - g.env);

      wetL += sL * env * g.gain_l;
      wetR += sR * env * g.gain_r;
      ++active_count;

      g.pos += g.increment;
      if (g.pos >= static_cast<float>(kBufferSize)) {
        g.pos -= static_cast<float>(kBufferSize);
      }

      g.env += g.env_inc;
      if (g.remaining > 0u) {
        --g.remaining;
      }

      if (g.remaining == 0u || g.env >= 2.0f) {
        g.active = false;
      }
    }

    wetL *= kInvNorm[active_count];
    wetR *= kInvNorm[active_count];

    out_l[n] = inL * dry_mix + wetL * wet_mix;
    out_r[n] = inR * dry_mix + wetR * wet_mix;
  }
}
