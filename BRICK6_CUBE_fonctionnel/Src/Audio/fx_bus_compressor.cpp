#include "fx_bus_compressor.h"

#include "fx_BusCompressorCore.h"

namespace {
EmbeddedPort::BusCompressorCore g_bus_compressor;

float g_threshold_db = -18.0f;
float g_ratio = 2.0f;
uint8_t g_attack_index = 2U;
uint8_t g_release_index = 2U;
float g_makeup_db = 0.0f;
float g_mix = 1.0f;
float g_hpf_hz = 60.0f;

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint8_t clampu8(uint8_t v, uint8_t lo, uint8_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}
} // namespace

extern "C" {

void fx_bus_compressor_init(float sample_rate, uint32_t block_size)
{
    if(sample_rate <= 0.0f)
        return;

    g_bus_compressor.prepare((double)sample_rate, 2, (int)block_size);
}

void fx_bus_compressor_set_threshold_db(float threshold_db)
{
    g_threshold_db = clampf(threshold_db, -40.0f, 0.0f);
}

void fx_bus_compressor_set_ratio(float ratio)
{
    g_ratio = clampf(ratio, 1.0f, 10.0f);
}

void fx_bus_compressor_set_attack_index(uint8_t attack_index)
{
    g_attack_index = clampu8(attack_index, 0U, 5U);
}

void fx_bus_compressor_set_release_index(uint8_t release_index)
{
    g_release_index = clampu8(release_index, 0U, 4U);
}

void fx_bus_compressor_set_makeup(float db)
{
    g_makeup_db = clampf(db, 0.0f, 24.0f);
}

void fx_bus_compressor_set_mix(float mix)
{
    g_mix = clampf(mix, 0.0f, 1.0f);
}

void fx_bus_compressor_set_hpf(float hz)
{
    g_hpf_hz = clampf(hz, 20.0f, 200.0f);
}

void fx_bus_compressor_process_stereo(float *left,
                                      float *right,
                                      uint32_t frames)
{
    if((left == nullptr) || (right == nullptr))
        return;

    const float threshold_db = g_threshold_db;
    const float ratio = g_ratio;
    const int attack_index = (int)g_attack_index;
    const int release_index = (int)g_release_index;
    const float makeup_db = g_makeup_db;
    const float mix = g_mix;
    const float hpf_hz = g_hpf_hz;

    for(uint32_t n = 0U; n < frames; ++n)
    {
        const float l = left[n];
        const float r = right[n];
        const float sc = 0.5f * (fabsf(l) + fabsf(r));

        const float comp_out = g_bus_compressor.process(sc,
                                                        0,
                                                        threshold_db,
                                                        ratio,
                                                        attack_index,
                                                        release_index,
                                                        makeup_db,
                                                        1.0f,
                                                        hpf_hz,
                                                        false,
                                                        0.0f,
                                                        false);

        float gain = 1.0f;
        if(sc > 1e-6f)
            gain = comp_out / sc;

        const float wet_l = l * gain;
        const float wet_r = r * gain;

        left[n] = l + (wet_l - l) * mix;
        right[n] = r + (wet_r - r) * mix;
    }
}

} // extern "C"
