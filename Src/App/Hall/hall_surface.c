#include "App/Hall/hall_surface.h"

#include <stddef.h>

#include "Board/board_product.h"
#include "Board/board_surface.h"

static uint8_t g_hall_surface_pressed[HALL_KEY_COUNT];
static uint16_t g_hall_surface_pressure[HALL_KEY_COUNT];
static uint8_t g_hall_surface_binary;

static const board_product_capabilities_t *hall_surface_caps(void)
{
    return board_product_capabilities();
}

void hall_surface_refresh(void)
{
    const board_product_capabilities_t *caps = hall_surface_caps();
    g_hall_surface_binary = ((caps != NULL) && (caps->has_step_binary_lanes != 0U)) ? 1U : 0U;

    if (g_hall_surface_binary != 0U)
    {
        board_surface_snapshot_t snapshot;
        board_surface_snapshot(&snapshot);
        for (uint8_t lane = 0U; lane < HALL_KEY_COUNT; ++lane)
        {
            const uint8_t down = (snapshot.raw[lane] != 0U) ? 1U : 0U;
            g_hall_surface_pressed[lane] = down;
            g_hall_surface_pressure[lane] = (down != 0U) ? UINT16_MAX : 0U;
        }
        return;
    }

    for (uint8_t lane = 0U; lane < HALL_KEY_COUNT; ++lane)
    {
        g_hall_surface_pressed[lane] = hall_engine_is_pressed(lane);
        g_hall_surface_pressure[lane] = (uint16_t)((uint32_t)hall_engine_get_value(lane) * 655U);
    }
}

uint8_t hall_surface_is_pressed(uint8_t lane)
{
    if (lane >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_surface_pressed[lane];
}

uint16_t hall_surface_pressure_u16(uint8_t lane)
{
    if (lane >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return g_hall_surface_pressure[lane];
}

uint8_t hall_surface_is_binary(void)
{
    return g_hall_surface_binary;
}
