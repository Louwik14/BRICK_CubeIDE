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

typedef enum { SIM_ENCODER_PAGE = 0, SIM_ENCODER_A, SIM_ENCODER_B, SIM_ENCODER_C,
               SIM_ENCODER_COUNT } sim_encoder_t;

int sim_engine_init(sim_engine_t *engine);
int sim_engine_advance(sim_engine_t *engine, uint32_t milliseconds);
int sim_engine_render(sim_engine_t *engine);
const uint8_t *sim_engine_framebuffer(const sim_engine_t *engine);
size_t sim_engine_framebuffer_size(void);
const char *sim_engine_page_name(const sim_engine_t *engine);
uint8_t sim_engine_page_id(const sim_engine_t *engine);
uint32_t sim_engine_hal_tick(void);
void sim_engine_set_hal_tick(uint32_t tick);
int sim_engine_button_from_name(const char *name, unsigned *button);
const char *sim_engine_button_name(unsigned button);
int sim_engine_encoder_from_name(const char *name, unsigned *encoder);
const char *sim_engine_encoder_name(unsigned encoder);
int sim_engine_press(sim_engine_t *engine, unsigned button);
int sim_engine_release(sim_engine_t *engine, unsigned button);
int sim_engine_tap(sim_engine_t *engine, unsigned button, uint32_t down_ms);
int sim_engine_combo(sim_engine_t *engine, const unsigned *buttons, size_t count,
                     uint32_t down_ms);
int sim_engine_rotate(sim_engine_t *engine, unsigned encoder, int delta);
void sim_engine_release_all(sim_engine_t *engine);
uint64_t sim_engine_held_mask(void);
int sim_engine_subpage(const sim_engine_t *engine, uint8_t *subpage);
uint8_t sim_engine_active_track(const sim_engine_t *engine);
uint8_t sim_engine_transport_running(const sim_engine_t *engine);
uint32_t sim_engine_tempo_milli(const sim_engine_t *engine);

#endif
