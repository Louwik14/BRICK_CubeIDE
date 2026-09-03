#include "App/Hall/hall_loop.h"

#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "IPC/live_event.h"

void hall_loop_init(void)
{
    live_event_init();
    hall_engine_init();
    hall_adc_init();
}
