#include "Audio/env_adsr.h"
#include "Audio/vca_env.h"

#include <stdint.h>
#include <string.h>

static int check_vca(vca_env_type_t type)
{
    vca_env_t sample_env;
    vca_env_t block_env;
    vca_env_init(&sample_env, 48000.0f);
    vca_env_set_type(&sample_env, type);
    vca_env_set_attack(&sample_env, 0.00031f);
    vca_env_set_decay(&sample_env, 0.00047f);
    vca_env_set_sustain(&sample_env, 0.37f);
    vca_env_set_release(&sample_env, 0.00073f);
    block_env = sample_env;
    vca_env_gate_on(&sample_env);
    vca_env_gate_on(&block_env);

    static const uint8_t chunks[] = {1U, 7U, 31U, 64U, 3U, 29U};
    for (uint32_t pass = 0U; pass < 20U; ++pass)
    {
        if (pass == 7U)
        {
            vca_env_set_sustain(&sample_env, 0.78f);
            vca_env_set_sustain(&block_env, 0.78f);
        }
        if (pass == 11U)
        {
            vca_env_gate_off(&sample_env);
            vca_env_gate_off(&block_env);
        }

        const uint32_t frames = chunks[pass % (sizeof(chunks) / sizeof(chunks[0]))];
        float reference[64] = {0.0f};
        float actual[64] = {0.0f};
        uint32_t reference_frames = 0U;
        while (reference_frames < frames)
        {
            float gain = 0.0f;
            const uint8_t running = (type == VCA_ENV_TYPE_LINEAR)
                ? vca_env_process_linear(&sample_env, &gain)
                : vca_env_process_daisy(&sample_env, &gain);
            if (running == 0U)
            {
                break;
            }
            reference[reference_frames++] = gain;
        }
        const uint32_t actual_frames =
            vca_env_process_block(&block_env, actual, frames);
        if ((actual_frames != reference_frames)
                || (memcmp(actual, reference, reference_frames * sizeof(float)) != 0)
                || (memcmp(&block_env, &sample_env, sizeof(block_env)) != 0))
        {
            return 1;
        }
    }
    return 0;
}

static int check_peaks(void)
{
    env_adsr_t sample_env;
    env_adsr_t block_env;
    env_adsr_init(&sample_env, 48000.0f);
    env_adsr_set_attack(&sample_env, 2300U);
    env_adsr_set_decay(&sample_env, 2900U);
    env_adsr_set_sustain(&sample_env, 16384U);
    env_adsr_set_release(&sample_env, 2600U);
    block_env = sample_env;
    env_adsr_gate_on(&sample_env);
    env_adsr_gate_on(&block_env);

    static const uint8_t chunks[] = {1U, 7U, 31U, 64U, 3U, 29U};
    for (uint32_t pass = 0U; pass < 24U; ++pass)
    {
        if (pass == 3U)
        {
            env_adsr_set_sustain(&sample_env, 8192U);
            env_adsr_set_sustain(&block_env, 8192U);
        }
        if (pass == 6U)
        {
            env_adsr_retrigger(&sample_env, false);
            env_adsr_retrigger(&block_env, false);
        }
        if (pass == 7U)
        {
            env_adsr_set_attack(&sample_env, 1100U);
            env_adsr_set_attack(&block_env, 1100U);
        }
        if (pass == 9U)
        {
            env_adsr_gate_off(&sample_env);
            env_adsr_gate_off(&block_env);
        }
        if (pass == 10U)
        {
            env_adsr_set_release(&sample_env, 1700U);
            env_adsr_set_release(&block_env, 1700U);
        }
        if (pass == 14U)
        {
            env_adsr_gate_on(&sample_env);
            env_adsr_gate_on(&block_env);
        }
        if (pass == 17U)
        {
            env_adsr_set_decay(&sample_env, 900U);
            env_adsr_set_decay(&block_env, 900U);
        }
        if (pass == 19U)
        {
            env_adsr_retrigger(&sample_env, true);
            env_adsr_retrigger(&block_env, true);
        }
        if (pass == 22U)
        {
            env_adsr_gate_off(&sample_env);
            env_adsr_gate_off(&block_env);
        }
        const uint32_t frames =
            chunks[pass % (sizeof(chunks) / sizeof(chunks[0]))];
        float reference[64] = {0.0f};
        float actual[64] = {0.0f};
        uint32_t reference_frames = 0U;
        while (reference_frames < frames)
        {
            float gain = 0.0f;
            if (env_adsr_process_vca_sample(&sample_env, &gain) == 0U)
            {
                break;
            }
            reference[reference_frames++] = gain;
        }
        const uint32_t actual_frames =
            env_adsr_process_vca_block(&block_env, actual, frames);
        if ((actual_frames != reference_frames)
                || (memcmp(actual, reference, reference_frames * sizeof(float)) != 0)
                || (memcmp(&block_env, &sample_env, sizeof(block_env)) != 0))
        {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    return check_vca(VCA_ENV_TYPE_DAISY)
        || check_vca(VCA_ENV_TYPE_LINEAR)
        || check_peaks();
}
