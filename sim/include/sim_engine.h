#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define SIM_FRAMEBUFFER_WIDTH 128U
#define SIM_FRAMEBUFFER_HEIGHT 64U
#define SIM_FRAMEBUFFER_SIZE 1024U

typedef struct
{
    uint64_t time_ms;
    int initialized;
    int renderer_ready;
    char error[160];
} sim_engine_t;

int sim_engine_init(sim_engine_t *engine);
int sim_engine_advance(sim_engine_t *engine, uint32_t milliseconds);
int sim_engine_render(sim_engine_t *engine);
const uint8_t *sim_engine_framebuffer(const sim_engine_t *engine);
size_t sim_engine_framebuffer_size(void);
const char *sim_engine_page_name(const sim_engine_t *engine);
uint8_t sim_engine_page_id(const sim_engine_t *engine);
uint32_t sim_engine_hal_tick(void);
void sim_engine_set_hal_tick(uint32_t tick);

#endif

