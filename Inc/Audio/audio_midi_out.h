#ifndef AUDIO_MIDI_OUT_H
#define AUDIO_MIDI_OUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_MIDI_OUT_QUEUE_CAPACITY 128U

typedef enum
{
    AUDIO_MIDI_OUT_PRIORITY_NORMAL = 0,
    AUDIO_MIDI_OUT_PRIORITY_CRITICAL = 1
} audio_midi_out_priority_t;

typedef struct
{
    uint32_t current_depth;
    uint32_t high_water;
    uint32_t submitted;
    uint32_t drained;
    uint32_t dropped;
    uint32_t critical_failures;
    uint32_t replaced_low_priority;
} audio_midi_out_diag_t;

void audio_midi_out_init(void);
uint8_t audio_midi_out_submit_raw(uint8_t status,
                                  uint8_t data1,
                                  uint8_t data2,
                                  uint8_t len,
                                  uint64_t sample_time,
                                  uint8_t priority);
uint8_t audio_midi_out_note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint64_t sample_time);
uint8_t audio_midi_out_note_off(uint8_t channel, uint8_t note, uint8_t velocity, uint64_t sample_time);
uint8_t audio_midi_out_program_change(uint8_t channel, uint8_t program, uint64_t sample_time);
uint8_t audio_midi_out_clock(uint64_t sample_time);
uint8_t audio_midi_out_start(uint64_t sample_time);
uint8_t audio_midi_out_stop(uint64_t sample_time);
uint8_t audio_midi_out_all_notes_off(uint8_t channel, uint64_t sample_time);
void audio_midi_out_process(uint32_t max_events);
void audio_midi_out_diag_snapshot(audio_midi_out_diag_t *out_diag);
void audio_midi_out_diag_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MIDI_OUT_H */
