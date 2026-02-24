#include "fx_granular.h"
#include "sdram.h"

#include <cstring>

namespace {

constexpr uint32_t kBufferSize = 16000u;
constexpr uint32_t kMaxGrains = 10u;
constexpr uint32_t kMinDurationSamples = 480u;
constexpr uint32_t kMaxDurationSamples = 5760u;

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
};

struct GranularState {
  float sample_rate;
  float density;
  float pitch_semitones;
  float pitch_increment;
  float mix;
  bool freeze;
  float spread; // ✅ NEW
  float stereo_offset;
  float* buffer_l;
  float* buffer_r;
  uint32_t write_pos;

  Grain grains[kMaxGrains];

  uint32_t rng;

  uint32_t spawn_counter;
  uint32_t spawn_interval;

  uint32_t free_index[kMaxGrains];
  uint32_t free_count;
};

alignas(4) SDRAM_BSS float g_granular_buffer_l[kBufferSize];
alignas(4) SDRAM_BSS float g_granular_buffer_r[kBufferSize];

constexpr uint32_t kCacheSize = 2048u;
float cache_l[kCacheSize];
float cache_r[kCacheSize];
uint32_t cache_start = 0u;

GranularState g_state;

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

inline uint32_t next_rand() {
  uint32_t x = g_state.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_state.rng = x;
  return x;
}

inline float rand_0_1() {
  return (next_rand() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

inline float fast_exp2_approx(float x) {
  const float x2 = x * x;
  const float x3 = x2 * x;
  return 1.0f + (0.69314718f * x) + (0.24022651f * x2) + (0.05550411f * x3);
}

inline void update_pitch_increment() {
  const float semitones = clamp(g_state.pitch_semitones, -48.0f, 48.0f);
  const float octaves = semitones * (1.0f / 12.0f);
  float ratio = fast_exp2_approx(octaves);
  if (ratio < 0.0625f) ratio = 0.0625f;
  if (ratio > 16.0f) ratio = 16.0f;
  g_state.pitch_increment = ratio * 65536.0f;
}

inline void update_spawn_probability() {
  float d = clamp(g_state.density, 0.0f, 1.0f);
  float grains_per_sec = 5.0f + d * 120.0f;

  g_state.spawn_interval = (uint32_t)(g_state.sample_rate / grains_per_sec);
  if (g_state.spawn_interval < 1) g_state.spawn_interval = 1;
}

inline float read_interp_fixed(const float* buf, uint32_t pos) {
  uint32_t i0 = pos >> 16;
  uint32_t i1 = i0 + 1;
  if (i1 >= kBufferSize) i1 = 0;

  float frac = (pos & 0xFFFF) * (1.0f / 65536.0f);

  float a = buf[i0];
  return a + (buf[i1] - a) * frac;
}

inline void refill_cache(uint32_t start) {
  cache_start = (start < kBufferSize) ? start : (start % kBufferSize);

  const uint32_t first = kBufferSize - cache_start;
  const uint32_t count0 = (first < kCacheSize) ? first : kCacheSize;
  const uint32_t count1 = kCacheSize - count0;

  std::memcpy(cache_l, &g_state.buffer_l[cache_start], count0 * sizeof(float));
  std::memcpy(cache_r, &g_state.buffer_r[cache_start], count0 * sizeof(float));

  if (count1 > 0u) {
    std::memcpy(&cache_l[count0], g_state.buffer_l, count1 * sizeof(float));
    std::memcpy(&cache_r[count0], g_state.buffer_r, count1 * sizeof(float));
  }
}

inline float read_interp_cached_fixed(const float* sdram_buf, const float* cache_buf,
                                      uint32_t pos) {
  uint32_t i0 = pos >> 16;
  uint32_t i1 = i0 + 1;
  if (i1 >= kBufferSize) i1 = 0;

  const uint32_t rel0 = (i0 + kBufferSize - cache_start) % kBufferSize;
  const uint32_t rel1 = (i1 + kBufferSize - cache_start) % kBufferSize;

  const bool in_cache = (rel0 < kCacheSize) && (rel1 < kCacheSize);
  if (!in_cache) return read_interp_fixed(sdram_buf, pos);

  float frac = (pos & 0xFFFF) * (1.0f / 65536.0f);
  float a = cache_buf[rel0];
  return a + (cache_buf[rel1] - a) * frac;
}

inline void spawn_grain() {
  if (g_state.free_count == 0) return;

  uint32_t idx = g_state.free_index[--g_state.free_count];
  Grain& g = g_state.grains[idx];

  // ✅ SPREAD CONTROL
  uint32_t max_span = (uint32_t)(g_state.spread * (float)kBufferSize);
  if (max_span > 4096u) max_span = 4096u;
  if (max_span < 32u) max_span = 32u;
  float r = rand_0_1();
  r = r * r; // bias vers centre
  uint32_t rand_span = (uint32_t)(r * max_span);
  uint32_t start = g_state.write_pos;
  start = (start >= rand_span) ? (start - rand_span)
                              : (start + kBufferSize - rand_span);

  float pan = rand_0_1();

  uint32_t duration = kMinDurationSamples +
    (next_rand() % (kMaxDurationSamples - kMinDurationSamples));

  g.pos = start << 16;
  g.increment = (uint32_t)g_state.pitch_increment;
  g.gain_l = 1.0f - pan;
  g.gain_r = pan;
  g.env = 0.0f;
  g.remaining = duration;

  uint32_t half = duration >> 1;
  g.env_inc = (half > 0u) ? (1.0f / (float)half) : 1.0f;

  // 🎲 léger jitter d'enveloppe
  float jitter = (rand_0_1() - 0.5f) * 0.08f;
  g.env_inc *= (1.0f + jitter);

  g.interp_toggle = 0;
  g.lastL = 0.0f;
  g.lastR = 0.0f;

  g.active = true;
}

} // namespace

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

// ✅ NEW
extern "C" void fx_granular_set_spread(float spread_0_1) {
  g_state.spread = clamp(spread_0_1, 0.0f, 1.0f);
}

extern "C" void fx_granular_init(float sample_rate) {
  g_state.sample_rate = sample_rate;
  g_state.density = 0.5f;
  g_state.pitch_semitones = 0.0f;
  g_state.mix = 1.0f;
  g_state.freeze = false;
  g_state.spread = 0.5f; // ✅ NEW
  g_state.stereo_offset = 0.5f;
  g_state.buffer_l = g_granular_buffer_l;
  g_state.buffer_r = g_granular_buffer_r;
  g_state.write_pos = 0u;
  g_state.rng = 0x12345678u;

  g_state.spawn_counter = 0;

  g_state.free_count = kMaxGrains;
  for (uint32_t i = 0; i < kMaxGrains; ++i) {
    g_state.grains[i].active = false;
    g_state.free_index[i] = i;
  }

  for (uint32_t i = 0; i < kBufferSize; ++i) {
    g_state.buffer_l[i] = 0.0f;
    g_state.buffer_r[i] = 0.0f;
  }

  refill_cache(0u);

  update_pitch_increment();
  update_spawn_probability();
}

extern "C" void fx_granular_process_block(float* in_l, float* in_r,
                                          float* out_l, float* out_r,
                                          uint32_t frames) {
  if (!in_l || !in_r || !out_l || !out_r) return;

  float wet_mix = g_state.mix;
  float dry_mix = 1.0f - wet_mix;
  float* bufL = g_state.buffer_l;
  float* bufR = g_state.buffer_r;

  const uint32_t limit = kBufferSize << 16;

  for (uint32_t n = 0; n < frames; ++n) {

    float inL = in_l[n];
    float inR = in_r[n];

    if (!g_state.freeze) {
      bufL[g_state.write_pos] = inL;
      bufR[g_state.write_pos] = inR;

      if (++g_state.write_pos >= kBufferSize)
        g_state.write_pos = 0u;

      const uint32_t cache_progress = (g_state.write_pos + kBufferSize - cache_start) % kBufferSize;
      if (cache_progress >= (kCacheSize >> 1)) {
        uint32_t new_start = (g_state.write_pos >= (kCacheSize >> 1))
                               ? (g_state.write_pos - (kCacheSize >> 1))
                               : (g_state.write_pos + kBufferSize - (kCacheSize >> 1));
        refill_cache(new_start);
      }
    }

    if (++g_state.spawn_counter >= g_state.spawn_interval) {
      g_state.spawn_counter = 0;
      spawn_grain();
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    uint32_t active_count = 0u;

    for (uint32_t i = 0; i < kMaxGrains; ++i) {
      Grain& g = g_state.grains[i];
      if (!g.active) continue;

      if (g.interp_toggle == 0) {
        g.lastL = read_interp_cached_fixed(g_state.buffer_l, cache_l, g.pos);
        g.lastR = read_interp_cached_fixed(g_state.buffer_r, cache_r, g.pos);
      }
      g.interp_toggle ^= 1;

      float sL = g.lastL;

      // 🎛️ stereo offset contrôlable
      uint32_t offset = (uint32_t)(g_state.stereo_offset * 512.0f);

      uint32_t posR = g.pos + (offset << 16);
      if (posR >= limit) posR -= limit;

      float sR = read_interp_cached_fixed(g_state.buffer_r, cache_r, posR);

      float x = g.env;
      float env = x * (2.0f - x); // parabole smooth

      wetL += sL * env * g.gain_l;
      wetR += sR * env * g.gain_r;
      ++active_count;

      g.pos += g.increment;
      if (g.pos >= limit)
        g.pos -= limit;

      g.env += g.env_inc;

      if (--g.remaining == 0u || g.env >= 2.0f) {
        g.active = false;
        g_state.free_index[g_state.free_count++] = i;
      }
    }

    float norm = kInvNorm[active_count];
    wetL *= norm;
    wetR *= norm;

    out_l[n] = inL * dry_mix + wetL * wet_mix;
    out_r[n] = inR * dry_mix + wetR * wet_mix;
  }
}
extern "C" void fx_granular_set_stereo_offset(float amount_0_1) {
  g_state.stereo_offset = clamp(amount_0_1, 0.0f, 1.0f);
}
