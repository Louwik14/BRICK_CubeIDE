#pragma once

#include <stdint.h>

#include "Param/param_registry.h"
#include "Track/track_types.h"

typedef struct { float param1, param2, amod, model; } prism_osc_control_t;
typedef struct {
    prism_osc_control_t osc[2];
    float volume, balance, tune, detune, drift, pitch_mod[2], phase1_reset;
} prism_program_control_t;

typedef struct { float level, model, tune, timbre, color; } stack_osc_control_t;
typedef struct {
    stack_osc_control_t osc[3];
    float noise_level, osc_detune, phase_reset;
} stack_program_control_t;

typedef struct { float position, start, length; } wave_osc_control_t;
typedef struct {
    wave_osc_control_t osc[2];
    float volume, balance, tune, detune;
} wave_program_control_t;

typedef struct { float gain, start, length, mode, tune, loop_start, slice_count; } ram_program_control_t;
typedef struct { float gain, source_bpm, play_mode, loop, stretch_mode, pitch, sync_length, grain; } stream_program_control_t;
typedef struct { float xfade, stretch, pitch, grain; } looper_program_control_t;
typedef struct { float gain, loop; } multi_program_control_t;
typedef struct { float program; float cc[3][4]; } midi_program_control_t;
typedef struct { float pitch, decay, pitch_sweep, sweep_decay, attack, noise, harmonics, drive; } drum_analog_program_control_t;
typedef struct { float model; float p[8]; } drum_md_program_control_t;

typedef struct {
    track_runtime_type_t tag;
    union {
        prism_program_control_t prism;
        stack_program_control_t stack;
        wave_program_control_t wave;
        ram_program_control_t ram;
        stream_program_control_t stream;
        looper_program_control_t looper;
        multi_program_control_t multi;
        midi_program_control_t midi;
        drum_analog_program_control_t drum_analog;
        drum_md_program_control_t drum_md;
    } state;
} tone_program_control_t;

void tone_program_control_init(void);
uint8_t tone_program_control_activate(uint8_t track, track_runtime_type_t type);
uint8_t tone_program_control_get(uint8_t track, param_id_t id, float *out_value);
uint8_t tone_program_control_set(uint8_t track, param_id_t id, float value);
uint8_t tone_program_control_capture(uint8_t track, tone_program_control_t *out_program);
uint8_t tone_program_control_restore(uint8_t track, const tone_program_control_t *program);
uint8_t tone_program_control_validate(const tone_program_control_t *program,
                                      track_runtime_type_t expected_type);
