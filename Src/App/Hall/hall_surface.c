#include "App/Hall/hall_surface.h"

#include "Board/board_surface.h"

static uint8_t g_hall_surface_pressed[HALL_UI_LANE_COUNT];
static uint16_t g_hall_surface_pressure[HALL_UI_LANE_COUNT];
static uint8_t g_hall_surface_binary;

void hall_surface_refresh(void)
{
    board_surface_snapshot_t snapshot;
    g_hall_surface_binary = 1U;
    board_surface_snapshot(&snapshot);
    for (uint8_t lane = 0U; lane < HALL_UI_LANE_COUNT; ++lane)
    {
        const uint8_t down = (snapshot.raw[lane] != 0U) ? 1U : 0U;
        g_hall_surface_pressed[lane] = down;
        g_hall_surface_pressure[lane] = (down != 0U) ? UINT16_MAX : 0U;
    }
}

uint8_t hall_surface_is_pressed(uint8_t lane)
{
    if (lane >= HALL_UI_LANE_COUNT)
    {
        return 0U;
    }

    return g_hall_surface_pressed[lane];
}

uint16_t hall_surface_pressure_u16(uint8_t lane)
{
    if (lane >= HALL_UI_LANE_COUNT)
    {
        return 0U;
    }

    return g_hall_surface_pressure[lane];
}

uint8_t hall_surface_is_binary(void)
{
    return g_hall_surface_binary;
}
