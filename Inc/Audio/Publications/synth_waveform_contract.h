#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_WAVEFORM_POINT_COUNT 48U
#define SYNTH_WAVEFORM_OSC_COUNT   2U

typedef enum
{
    SYNTH_WAVEFORM_ENGINE_NONE = 0,
    SYNTH_WAVEFORM_ENGINE_WAVE,
    SYNTH_WAVEFORM_ENGINE_PRISM
} synth_waveform_engine_t;

typedef struct
{
    uint32_t generation;
    uint8_t entity_id;
    uint8_t engine;
    uint8_t osc_mask;
    uint8_t voice_instance;
    int8_t points[SYNTH_WAVEFORM_OSC_COUNT][SYNTH_WAVEFORM_POINT_COUNT];
} synth_waveform_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    synth_waveform_snapshot_t snapshot;
} synth_waveform_layout_t;

extern synth_waveform_layout_t g_synth_waveform_layout;

#ifdef __cplusplus
}
#endif
