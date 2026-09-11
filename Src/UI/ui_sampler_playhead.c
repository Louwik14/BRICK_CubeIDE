#include "UI/ui_sampler_playhead.h"

#include "IPC/sampler_ram_playhead_contract.h"
#include "stm32h7xx.h"

ui_sampler_playhead_view_t ui_sampler_playhead_view(
    brick_entity_id_t entity_id,
    uint16_t global_slot)
{
    ui_sampler_playhead_view_t view = {0U, 0.0f};
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return view;

    const sampler_ram_playhead_slot_t *const slot =
        &g_sampler_ram_playhead[entity_id];
    const uint32_t before = slot->sequence;
    if ((before & 1U) != 0U)
        return view;
    __DMB();
    const sampler_ram_playhead_snapshot_t snapshot = slot->snapshot;
    __DMB();
    if ((before != slot->sequence) || ((before & 1U) != 0U)
        || (snapshot.active == 0U)
        || (snapshot.global_slot != global_slot)
        || (snapshot.frame_count == 0U))
        return view;

    view.active = 1U;
    view.normalized_position = (float)snapshot.frame
        / (float)snapshot.frame_count;
    if (view.normalized_position > 1.0f)
        view.normalized_position = 1.0f;
    return view;
}
