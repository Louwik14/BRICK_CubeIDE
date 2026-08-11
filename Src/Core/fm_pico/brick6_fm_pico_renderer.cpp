#include "Core/fm_pico/brick6_fm_pico_renderer.h"

#include <string.h>

#include "Core/fm_pico/OpsAlg6.h"
#include "Core/fm_pico/EnvGen.h"
#include "Core/fm_pico/brick6_fm_pico_tables.h"
#include "Storage/memory_layout.h"

namespace
{
constexpr uint8_t kVoiceCount = 16U;
constexpr uint8_t kOperatorCount = BRICK6_FM_PICO_OPERATOR_COUNT;
constexpr uint8_t kEnvCount = BRICK6_FM_PICO_ENV_COUNT;

struct pico_voice_t
{
    DX::OpsAlg6<EnvGen> ops;
    uint8_t active;
};

AUDIO_WARM static pico_voice_t g_voice[kVoiceCount];

static uint8_t rate_to_6(uint8_t rate)
{
    return (uint8_t)(((uint16_t)rate * 164U) >> 8);
}

/* DX7 KRS changes in three-key groups. The result is in MSFA qRate units. */
static uint8_t rate_scaling_qrate(uint8_t note, uint8_t scale)
{
    if (scale == 0U || note <= 21U)
        return 0U;

    const uint8_t group = (uint8_t)((note - 21U) / 3U);
    const uint16_t scaled = (uint16_t)group * (uint16_t)(scale > 7U ? 7U : scale);
    return (scaled > 63U) ? 63U : (uint8_t)((scaled + 4U) >> 3);
}

static uint8_t level_to_atten8(uint8_t level)
{
    static const uint8_t table_log[100] = {
        127,122,118,114,110,107,104,102,100,98,96,94,92,90,88,86,85,84,82,81,
        79,78,77,76,75,74,73,72,71,70,69,68,67,66,65,64,63,62,61,60,
        59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,40,
        39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20,
        19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    };
    if (level > 99U)
        level = 99U;
    return (uint8_t)(table_log[level] << 1);
}

static void configure_voice(pico_voice_t *voice,
                            const brick6_fm_pico_config_t *config)
{
    if (voice == nullptr || config == nullptr)
        return;

    voice->ops.setOpsSync(config->sync != 0U);
    voice->ops.setOpsFdbk(config->feedback > 7U ? 7U : config->feedback);
    voice->ops.setOpsAlg(config->algorithm & 31U);

    for (uint8_t op = 0U; op < kOperatorCount; ++op)
    {
        EnvGen *const env = voice->ops.getEgPointer(op);
        /* BRICK already provides the resolved MSFA phase increment.  It is
           not the 14-bit DX7 frequency-table index expected by setOpsFreq(). */
        voice->ops.setOpsPhaseInc(op, (uint32_t)config->phase_inc[op]);

        const uint8_t rate_scale = rate_scaling_qrate(config->note,
                                                       config->rate_scale[op]);
        for (uint8_t stage = 0U; stage < kEnvCount; ++stage)
        {
            uint8_t rate = rate_to_6(config->rates[op][stage]);
            rate = (rate > (uint8_t)(63U - rate_scale))
                ? 63U : (uint8_t)(rate + rate_scale);
            env->setRate6(stage, rate);
            uint16_t attenuation = (config->operator_on[op] != 0U)
                ? (uint16_t)level_to_atten8(config->levels[op][stage])
                    + (uint16_t)level_to_atten8(config->output_level[op])
                : 0xFFU;
            if (attenuation > 0xFFU)
                attenuation = 0xFFU;
            env->setAtten8(stage, (uint8_t)attenuation);
        }
        env->setAmpMod(0U);
    }
}
}

void brick6_fm_pico_renderer_init(void)
{
    brick6_fm_pico_tables_init();
    for (uint8_t i = 0U; i < kVoiceCount; ++i)
        brick6_fm_pico_renderer_reset(i);
}

void brick6_fm_pico_renderer_reset(uint8_t instance_id)
{
    if (instance_id >= kVoiceCount)
        return;
    g_voice[instance_id].active = 0U;
    g_voice[instance_id].ops.setOpsAlg(0U);
    g_voice[instance_id].ops.setOpsSync(true);
    g_voice[instance_id].ops.setOpsFdbk(0U);
}

void brick6_fm_pico_renderer_note_on(uint8_t instance_id,
                                     const brick6_fm_pico_config_t *config)
{
    if (instance_id >= kVoiceCount || config == nullptr)
        return;
    configure_voice(&g_voice[instance_id], config);
    for (uint8_t op = 0U; op < kOperatorCount; ++op)
        g_voice[instance_id].ops.getEgPointer(op)->keyOn();
    g_voice[instance_id].ops.keyOn();
    g_voice[instance_id].active = 1U;
}

void brick6_fm_pico_renderer_note_off(uint8_t instance_id)
{
    if (instance_id >= kVoiceCount)
        return;
    for (uint8_t op = 0U; op < kOperatorCount; ++op)
        g_voice[instance_id].ops.getEgPointer(op)->keyOff();
}

void brick6_fm_pico_renderer_render(uint8_t instance_id,
                                    const brick6_fm_pico_config_t *config,
                                    float *out_mono,
                                    uint32_t frames)
{
    if (instance_id >= kVoiceCount || config == nullptr || out_mono == nullptr)
        return;

    configure_voice(&g_voice[instance_id], config);
    constexpr float kOutputScale = 1.0f / 65536.0f;
    for (uint32_t i = 0U; i < frames; ++i)
        out_mono[i] = (float)g_voice[instance_id].ops() * kOutputScale;
}

uint8_t brick6_fm_pico_renderer_is_complete(uint8_t instance_id,
                                            uint8_t carrier_mask)
{
    if (instance_id >= kVoiceCount)
        return 1U;
    for (uint8_t op = 0U; op < kOperatorCount; ++op)
    {
        if ((carrier_mask & (uint8_t)(1U << op)) != 0U
                && g_voice[instance_id].ops.getEgPointer(op)->isComplete() == false)
            return 0U;
    }
    return 1U;
}
