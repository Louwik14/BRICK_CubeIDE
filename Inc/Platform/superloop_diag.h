#ifndef SUPERLOOP_DIAG_H_
#define SUPERLOOP_DIAG_H_

#include <stdint.h>

typedef enum
{
    SUPERLOOP_DIAG_POWER = 0,
    SUPERLOOP_DIAG_USB,
    SUPERLOOP_DIAG_ENGINE,
    SUPERLOOP_DIAG_STREAM_1,
    SUPERLOOP_DIAG_AUDIO_BG,
    SUPERLOOP_DIAG_SEQ,
    SUPERLOOP_DIAG_STORAGE,
    SUPERLOOP_DIAG_PATTERN,
    SUPERLOOP_DIAG_MASTER,
    SUPERLOOP_DIAG_STREAM_2,
    SUPERLOOP_DIAG_UI_BOOT,
    SUPERLOOP_DIAG_HALL_UI,
    SUPERLOOP_DIAG_MIDI,
    SUPERLOOP_DIAG_BOOTLOADER,
    SUPERLOOP_DIAG_UI_TASKLET,
    SUPERLOOP_DIAG_UI_RENDER,
    SUPERLOOP_DIAG_DISPLAY,
    SUPERLOOP_DIAG_SERVICE_COUNT
} superloop_diag_service_id_t;

extern volatile uint32_t
    g_superloop_diag_service_count[SUPERLOOP_DIAG_SERVICE_COUNT];
extern volatile uint32_t
    g_superloop_diag_service_max_cycles[SUPERLOOP_DIAG_SERVICE_COUNT];
extern volatile uint32_t g_superloop_diag_interval_max_service_id;
extern volatile uint32_t g_superloop_diag_interval_max_cycles;
extern volatile uint32_t g_usb_audio_diag_transport_gap_service_id;
extern volatile uint32_t g_usb_audio_diag_transport_gap_service_cycles;

uint32_t superloop_diag_begin(void);
void superloop_diag_end(superloop_diag_service_id_t service_id,
                        uint32_t started_cycles);
void superloop_diag_reset(void);
void superloop_diag_interval_reset(void);

#endif /* SUPERLOOP_DIAG_H_ */
