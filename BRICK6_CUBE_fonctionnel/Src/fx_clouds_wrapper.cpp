#include "fx_clouds_wrapper.h"

#include <stddef.h>

#include "mutable_instruments/clouds/dsp/granular_processor.h"

namespace {

constexpr size_t kCloudsLargeBufferBytes = 131072;
constexpr size_t kCloudsSmallBufferBytes = 65536;

clouds::GranularProcessor g_clouds;
bool g_clouds_initialized = false;

alignas(4) uint8_t g_clouds_large_buffer[kCloudsLargeBufferBytes];
alignas(4) uint8_t g_clouds_small_buffer[kCloudsSmallBufferBytes];

clouds::ShortFrame g_in[clouds::kMaxBlockSize];
clouds::ShortFrame g_out[clouds::kMaxBlockSize];

inline int16_t float_to_s16(float x) {
  if (x > 1.0f) x = 1.0f;
  if (x < -1.0f) x = -1.0f;
  return static_cast<int16_t>(x * 32767.0f);
}

}  // namespace

extern "C" void fx_clouds_init(float sample_rate) {
  (void)sample_rate;
  // INCONNU: GranularProcessor n'expose pas de setter de sample-rate public.
  g_clouds.Init(
      g_clouds_large_buffer, kCloudsLargeBufferBytes,
      g_clouds_small_buffer, kCloudsSmallBufferBytes);
  g_clouds.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
  g_clouds.Prepare();
  g_clouds_initialized = true;
}

extern "C" void fx_clouds_process_block(
    float* inL, float* inR,
    float* outL, float* outR,
    uint32_t frames) {
  if (!g_clouds_initialized || !inL || !inR || !outL || !outR) {
    return;
  }

  uint32_t offset = 0;
  while (offset < frames) {
    const uint32_t chunk =
        (frames - offset) > clouds::kMaxBlockSize
            ? clouds::kMaxBlockSize
            : (frames - offset);

    for (uint32_t i = 0; i < chunk; ++i) {
      g_in[i].l = float_to_s16(inL[offset + i]);
      g_in[i].r = float_to_s16(inR[offset + i]);
    }

    g_clouds.Process(g_in, g_out, chunk);

    for (uint32_t i = 0; i < chunk; ++i) {
      outL[offset + i] = static_cast<float>(g_out[i].l) / 32768.0f;
      outR[offset + i] = static_cast<float>(g_out[i].r) / 32768.0f;
    }

    offset += chunk;
  }
}
