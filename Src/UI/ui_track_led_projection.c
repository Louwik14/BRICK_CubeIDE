#include "UI/ui_track_led_projection.h"

#include <stddef.h>

#include "Track/entity_topology.h"
#include "Track/track_state.h"
#include "UI/ui_core.h"

uint8_t ui_track_led_project_hall(uint8_t hall,
                                  ui_track_led_projection_t *out_projection)
{
    if (out_projection == NULL)
    {
        return 0U;
    }

    out_projection->visible = 0U;
    out_projection->color = UI_TRACK_LED_COLOR_NONE;

    if (hall >= BRICK_ENTITY_CAPACITY)
    {
        return 0U;
    }

    entity_topology_descriptor_t descriptor = { 0 };
    if (entity_topology_get((brick_entity_id_t)hall, &descriptor) == 0U)
    {
        return 0U;
    }

    if (hall < BRICK_ENTITY_TOP_LEVEL_COUNT)
    {
        out_projection->visible = 1U;
        out_projection->color =
            (track_state_get_family(hall) == TRACK_FAMILY_OFF)
                ? UI_TRACK_LED_COLOR_TOP_LEVEL_OFF
                : UI_TRACK_LED_COLOR_TOP_LEVEL_ACTIVE;
    }
    else
    {
        /* Child visibility belongs to the live GROUP topology.  Child activity
         * is the lane descriptor's authority, not a synthetic Track type. */
        if ((entity_topology_group_is_active() == 0U)
                || (descriptor.parent_entity_id != BRICK_ENTITY_GROUP_MASTER_ID))
        {
            return 1U;
        }

        out_projection->visible = 1U;
        out_projection->color = (descriptor.active != 0U)
            ? UI_TRACK_LED_COLOR_GROUP_CHILD_ACTIVE
            : UI_TRACK_LED_COLOR_GROUP_CHILD_INACTIVE;
    }

    if (hall == ui_get_active_lane())
    {
        out_projection->color = UI_TRACK_LED_COLOR_FOCUS;
    }

    return 1U;
}
