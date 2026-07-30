#ifndef METRONOME_RUNTIME_H
#define METRONOME_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    METRONOME_CLICK_NORMAL = 0,
    METRONOME_CLICK_ACCENT = 1
} metronome_click_type_t;

typedef struct
{
    uint8_t enabled;
    uint8_t rendering;
    uint8_t level_u7;
    uint8_t _pad;
} metronome_runtime_diag_state_t;

void metronome_runtime_init(void);
void metronome_runtime_set_level_u7(uint8_t level);
void metronome_runtime_trigger_at(uint16_t offset, metronome_click_type_t type);
void metronome_runtime_render_main_monitor(float *main_l, float *main_r, uint32_t frames);
void metronome_runtime_stop(void);
void metronome_runtime_get_diag_state(metronome_runtime_diag_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* METRONOME_RUNTIME_H */
