#include <assert.h>
#include <string.h>

#include "Core/engine_lane_authority.h"
#include "Core/track_topology.h"

static void assert_usage(const ui_track_config_t *configs,
                         uint8_t synth,
                         uint8_t sampler,
                         uint8_t drum)
{
    engine_lane_usage_t usage;
    engine_lane_authority_count(configs, TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY, &usage);
    assert(usage.synth_tracks == synth);
    assert(usage.sampler_tracks == sampler);
    assert(usage.drum_tracks == drum);
    assert(usage.total_tracks == (uint8_t)(synth + sampler + drum));
}

static void test_prism_to_off_ignores_fixed_looper(void)
{
    ui_track_config_t configs[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY];
    memset(configs, 0, sizeof(configs));
    configs[TRACK_TOPOLOGY_LOOPER_TRACK_INDEX].family = UI_TRACK_FAMILY_SAMPLER;
    configs[TRACK_TOPOLOGY_LOOPER_TRACK_INDEX].type = UI_TRACK_TYPE_LOOPER;

    assert_usage(configs, 0U, 0U, 0U);

    configs[0].family = UI_TRACK_FAMILY_SYNTH;
    configs[0].type = UI_TRACK_TYPE_PRISM;
    assert_usage(configs, 1U, 0U, 0U);

    configs[0].family = UI_TRACK_FAMILY_OFF;
    configs[0].type = UI_TRACK_TYPE_AUDIO;
    assert_usage(configs, 0U, 0U, 0U);
}

static void test_partial_and_total_engine_disable(void)
{
    ui_track_config_t configs[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY];
    memset(configs, 0, sizeof(configs));
    configs[TRACK_TOPOLOGY_LOOPER_TRACK_INDEX].family = UI_TRACK_FAMILY_SAMPLER;
    configs[TRACK_TOPOLOGY_LOOPER_TRACK_INDEX].type = UI_TRACK_TYPE_LOOPER;
    configs[0].family = UI_TRACK_FAMILY_SYNTH;
    configs[0].type = UI_TRACK_TYPE_PRISM;
    configs[1].family = UI_TRACK_FAMILY_SAMPLER;
    configs[1].type = UI_TRACK_TYPE_RAM;
    configs[2].family = UI_TRACK_FAMILY_DRUM;
    configs[2].type = UI_TRACK_TYPE_DRUM_MD;
    assert_usage(configs, 1U, 1U, 1U);

    configs[0].family = UI_TRACK_FAMILY_OFF;
    assert_usage(configs, 0U, 1U, 1U);
    configs[1].family = UI_TRACK_FAMILY_OFF;
    assert_usage(configs, 0U, 0U, 1U);
    configs[2].family = UI_TRACK_FAMILY_OFF;
    assert_usage(configs, 0U, 0U, 0U);
}

int main(void)
{
    test_prism_to_off_ignores_fixed_looper();
    test_partial_and_total_engine_disable();
    return 0;
}
