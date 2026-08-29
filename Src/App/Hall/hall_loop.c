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

void hall_loop_process(void)
{
    /* Musical Hall decisions are made by hall_engine_process_sample() in the
     * acquisition callback. Keep this hook for diagnostics/UI maintenance. */
    hall_engine_process();
}
