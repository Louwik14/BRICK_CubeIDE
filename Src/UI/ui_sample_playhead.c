#include "UI/ui_sample_playhead.h"

#include "Sampler/sample_playhead_contract.h"
#include "stm32h7xx.h"

ui_sample_playhead_view_t ui_sample_playhead_view(
    brick_entity_id_t entity_id,
    uint16_t global_slot)
{
    ui_sample_playhead_view_t view = {0};
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return view;

    const sample_playhead_slot_t *const slot = &g_sample_playhead[entity_id];
    const uint32_t before = slot->sequence;
    if ((before & 1U) != 0U)
        return view;
    __DMB();
    const sample_playhead_snapshot_t snapshot = slot->snapshot;
    __DMB();
    if ((before != slot->sequence) || ((before & 1U) != 0U)
        || (snapshot.active == 0U)
        || (snapshot.global_slot != global_slot)
        || (snapshot.frame_count == 0U))
        return view;

    view.active = 1U;
    view.frame = snapshot.frame;
    view.frame_count = snapshot.frame_count;
    view.region_begin = snapshot.region_begin;
    view.region_end = snapshot.region_end;
    view.loop_begin = snapshot.loop_begin;
    view.slice_count = snapshot.slice_count;
    view.slice_index = snapshot.slice_index;
    return view;
}
