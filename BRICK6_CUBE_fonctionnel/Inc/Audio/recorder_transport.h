#ifndef RECORDER_TRANSPORT_H
#define RECORDER_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file recorder_transport.h
 * @brief Temporary step-based transport for live recorder control.
 *
 * Why this module exists now:
 * - The live recorder audio path (write/read) already runs in the audio callback.
 * - Record start/stop control must remain deterministic and IRQ-safe.
 * - engine_tasklet provides a stable clock derived from audio DMA frames
 *   (1500 Hz at 48 kHz / 32-frame tick), so it is used as timing master.
 *
 * Current role (pre-sequencer):
 * - Arm a manual recording for a fixed step length (16/32/48/64).
 * - Count steps from engine ticks in main loop context only.
 * - Auto-stop recording once the requested number of steps is reached.
 *
 * Sequencer integration plan:
 * - Future sequencer transport will replace:
 *   1) ticks_per_step logic
 *   2) internal step counter logic
 *   3) manual record trigger source
 * - The recorder DSP/audio API remains valid and unchanged:
 *   live_recorder_write(), live_recorder_read(),
 *   live_recorder_start_record(), live_recorder_stop_record().
 */
typedef struct
{
    uint8_t recording;

    uint32_t steps_recorded;
    uint32_t step_limit;

    uint32_t tick_counter;
    uint32_t ticks_per_step;

    uint32_t last_tick_count;
} recorder_transport_t;

void recorder_transport_init(void);
void recorder_transport_start_record(uint32_t steps);
void recorder_transport_process(void);
uint8_t recorder_transport_is_recording(void);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_TRANSPORT_H */
