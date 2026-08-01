#include <assert.h>

#include "UI/ui_track_catalog.h"

static void set_play_configs(ui_track_config_t configs[UI_TRACK_COUNT])
{
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        configs[track].family = UI_TRACK_FAMILY_OFF;
        configs[track].type = UI_TRACK_TYPE_AUDIO;
    }
}

int main(void)
{
    static const ui_track_family_t expected[] = {
        UI_TRACK_FAMILY_OFF,
        UI_TRACK_FAMILY_SYNTH,
        UI_TRACK_FAMILY_DRUM,
        UI_TRACK_FAMILY_MIDI,
        UI_TRACK_FAMILY_EXTERNAL,
        UI_TRACK_FAMILY_SAMPLER,
    };
    ui_track_config_t configs[UI_TRACK_COUNT];
    set_play_configs(configs);

    assert(ui_track_catalog_cfg_family_order_count() == 6U);
    for (uint8_t index = 0U; index < 6U; ++index)
    {
        uint8_t resolved_index = 0U;
        assert(ui_track_catalog_cfg_family_order_at(index) == expected[index]);
        assert(ui_track_catalog_cfg_family_order_index(expected[index], &resolved_index));
        assert(resolved_index == index);
    }

    ui_track_family_t current = UI_TRACK_FAMILY_OFF;
    for (uint8_t index = 1U; index < 6U; ++index)
    {
        current = ui_track_catalog_cfg_family_step(current, 1, 0U, configs);
        assert(current == expected[index]);
    }
    assert(ui_track_catalog_cfg_family_step(current, 1, 0U, configs) == UI_TRACK_FAMILY_OFF);

    current = UI_TRACK_FAMILY_OFF;
    for (int8_t index = 4; index >= 0; --index)
    {
        current = ui_track_catalog_cfg_family_step(current, -1, 0U, configs);
        assert(current == expected[index]);
    }
    assert(ui_track_catalog_cfg_family_step(current, -1, 0U, configs) == UI_TRACK_FAMILY_SAMPLER);

    configs[0].family = UI_TRACK_FAMILY_EXTERNAL;
    configs[0].type = UI_TRACK_TYPE_EXTERNAL;
    for (uint8_t track = 1U; track <= 4U; ++track)
    {
        configs[track].family = UI_TRACK_FAMILY_SAMPLER;
        configs[track].type = UI_TRACK_TYPE_STREAM;
    }
    assert(ui_track_catalog_cfg_family_step(UI_TRACK_FAMILY_EXTERNAL, 1, 0U, configs)
           == UI_TRACK_FAMILY_OFF);
    assert(ui_track_catalog_cfg_family_step(UI_TRACK_FAMILY_OFF, -1, 0U, configs)
           == UI_TRACK_FAMILY_EXTERNAL);
    assert(ui_track_catalog_cfg_family_step(UI_TRACK_FAMILY_INPUT1, 1, 0U, configs)
           == UI_TRACK_FAMILY_INPUT1);
    assert(!ui_track_catalog_family_is_available(0U, UI_TRACK_FAMILY_INPUT1, configs));

    return 0;
}
