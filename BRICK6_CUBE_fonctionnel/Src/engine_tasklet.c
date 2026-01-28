#include "engine_tasklet.h"

#if BRICK6_REFACTOR_STEP_3

volatile uint32_t engine_tick_count = 0U;
static uint32_t engine_frames_accum = 0U;
static uint32_t engine_frames_per_tick = 0U;

static void engine_tick(void)
{
  engine_tick_count++;
}

void engine_tasklet_init(uint32_t sample_rate)
{
  engine_tick_count = 0U;
  engine_frames_accum = 0U;
  engine_frames_per_tick = sample_rate / 1000U;

  if (engine_frames_per_tick == 0U)
  {
    engine_frames_per_tick = 1U;
  }
}

void engine_tasklet_notify_frames(uint32_t frames)
{
  engine_frames_accum += frames;
}

void engine_tasklet_poll(void)
{
  while (engine_frames_accum >= engine_frames_per_tick)
  {
    engine_frames_accum -= engine_frames_per_tick;
    engine_tick();
  }
}

#endif
