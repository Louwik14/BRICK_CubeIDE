#include "fx_clouds.h"

#include <stddef.h>
#include <stdint.h>

#include "clouds/dsp/granular_processor.h"



namespace {

constexpr size_t kCloudsLargeBufferBytes = 131072;
constexpr size_t kCloudsSmallBufferBytes = 65536;

alignas(4) uint8_t g_clouds_large_buffer[kCloudsLargeBufferBytes];
alignas(4) uint8_t g_clouds_small_buffer[kCloudsSmallBufferBytes];

clouds::GranularProcessor g_clouds;
clouds::ShortFrame g_in[clouds::kMaxBlockSize];
clouds::ShortFrame g_out[clouds::kMaxBlockSize];

float g_position = 0.5f;
float g_size = 0.5f;
float g_pitch = 0.0f;
float g_density = 0.5f;
float g_texture = 0.5f;
float g_dry_wet = 1.0f;
float g_feedback = 0.3f;
float g_stereo_spread = 1.0f;
bool g_freeze = false;

bool g_clouds_ready = false;

inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

inline int16_t float_to_s16(float x) {
  if (x > 1.0f) x = 1.0f;
  if (x < -1.0f) x = -1.0f;
  return static_cast<int16_t>(x * 32767.0f);
}

}  // namespace

extern "C" void fx_clouds_set_position(float position_0_1) {
  g_position = clamp01(position_0_1);
}

extern "C" void fx_clouds_set_size(float size_0_1) {
  g_size = clamp01(size_0_1);
}

extern "C" void fx_clouds_set_pitch(float pitch) {
  g_pitch = pitch;
}

extern "C" void fx_clouds_set_density(float density_0_1) {
  g_density = clamp01(density_0_1);
}

extern "C" void fx_clouds_set_texture(float texture_0_1) {
  g_texture = clamp01(texture_0_1);
}

extern "C" void fx_clouds_set_dry_wet(float dry_wet_0_1) {
  g_dry_wet = clamp01(dry_wet_0_1);
}

extern "C" void fx_clouds_set_feedback(float feedback_0_1) {
  g_feedback = clamp01(feedback_0_1);
}

extern "C" void fx_clouds_set_stereo_spread(float stereo_spread_0_1) {
  g_stereo_spread = clamp01(stereo_spread_0_1);
}

extern "C" void fx_clouds_set_freeze(uint8_t freeze) {
  g_freeze = (freeze != 0u);
}

extern "C" void fx_clouds_init(float sample_rate) {
  (void)sample_rate;

  g_clouds.Init(g_clouds_large_buffer, kCloudsLargeBufferBytes,
                g_clouds_small_buffer, kCloudsSmallBufferBytes);

  g_clouds.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
  g_clouds.set_low_fidelity(false);
  g_clouds.Prepare();

  g_clouds_ready = true;
}

extern "C" void fx_clouds_process_block(float *in_l, float *in_r,
                                         float *out_l, float *out_r,
                                         uint32_t frames) {
  if (!g_clouds_ready || !in_l || !in_r || !out_l || !out_r) {
    return;
  }

  clouds::Parameters* p = g_clouds.mutable_parameters();
  p->position = g_position;
  p->size = g_size;
  p->pitch = g_pitch;
  p->density = g_density;
  p->texture = g_texture;
  p->dry_wet = g_dry_wet;
  p->feedback = g_feedback;
  p->stereo_spread = g_stereo_spread;
  p->freeze = g_freeze;
  p->trigger = false;

  uint32_t offset = 0;
  while (offset < frames) {
    uint32_t chunk = frames - offset;
    if (chunk > clouds::kMaxBlockSize) {
      chunk = clouds::kMaxBlockSize;
    }

    for (uint32_t i = 0; i < chunk; ++i) {
      g_in[i].l = float_to_s16(in_l[offset + i]);
      g_in[i].r = float_to_s16(in_r[offset + i]);
    }

    g_clouds.Process(g_in, g_out, chunk);

    for (uint32_t i = 0; i < chunk; ++i) {
      out_l[offset + i] = static_cast<float>(g_out[i].l) / 32768.0f;
      out_r[offset + i] = static_cast<float>(g_out[i].r) / 32768.0f;
    }

    offset += chunk;
  }
}
