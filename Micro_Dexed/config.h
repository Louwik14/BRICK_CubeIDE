#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include "microdexed_compat.h"
#include "midinotes.h"

#define VERSION "minimal"

#define DEXED_ENGINE DEXED_ENGINE_MARKI

#define NUM_DEXED 1
#define MAX_DEXED 2
#define MAX_NOTES 4

#define SAMPLE_RATE 48000
#define DEXED_RENDER_MAX_FRAMES 128

#define CONTROL_RATE_MS 50
#define TRANSPOSE_FIX 24
#define VOICE_SILENCE_LEVEL 4000

#define DEFAULT_MIDI_CHANNEL MIDI_CHANNEL_OMNI

#define MIDI_CONTROLLER_MODE_MAX 2

#define MONO_MIN 0
#define MONO_MAX 3
#define MONO_DEFAULT 0

#define VOLUME_MIN 0
#define VOLUME_MAX 100
#define VOLUME_DEFAULT 80

#define PANORAMA_MIN 0
#define PANORAMA_MAX 40
#define PANORAMA_DEFAULT 20

#define MIDI_CHANNEL_MIN MIDI_CHANNEL_OMNI
#define MIDI_CHANNEL_MAX 16
#define MIDI_CHANNEL_DEFAULT MIDI_CHANNEL_OMNI

#define INSTANCE_LOWEST_NOTE_MIN 21
#define INSTANCE_LOWEST_NOTE_MAX 108
#define INSTANCE_LOWEST_NOTE_DEFAULT 21

#define INSTANCE_HIGHEST_NOTE_MIN 21
#define INSTANCE_HIGHEST_NOTE_MAX 108
#define INSTANCE_HIGHEST_NOTE_DEFAULT 108

#define FILTER_CUTOFF_MIN 0
#define FILTER_CUTOFF_MAX 100
#define FILTER_CUTOFF_DEFAULT 0

#define FILTER_RESONANCE_MIN 0
#define FILTER_RESONANCE_MAX 100
#define FILTER_RESONANCE_DEFAULT 0

#define TRANSPOSE_MIN 0
#define TRANSPOSE_MAX 48
#define TRANSPOSE_DEFAULT 12

#define TUNE_MIN 1
#define TUNE_MAX 199
#define TUNE_DEFAULT 100

#define SOUND_INTENSITY_MIN 0
#define SOUND_INTENSITY_MAX 127
#define SOUND_INTENSITY_DEFAULT 100
#define SOUND_INTENSITY_AMP_MAX 1.27

#define POLYPHONY_MIN 0
#define POLYPHONY_MAX MAX_NOTES
#define POLYPHONY_DEFAULT MAX_NOTES

#define ENGINE_MIN 0
#define ENGINE_MAX 2
#define ENGINE_DEFAULT 0

#define MONOPOLY_MIN 0
#define MONOPOLY_MAX 1
#define MONOPOLY_DEFAULT 0

#define NOTE_REFRESH_MIN 0
#define NOTE_REFRESH_MAX 1
#define NOTE_REFRESH_DEFAULT 0

#define PB_RANGE_MIN 0
#define PB_RANGE_MAX 12
#define PB_RANGE_DEFAULT 1

#define PB_STEP_MIN 0
#define PB_STEP_MAX 12
#define PB_STEP_DEFAULT 0

#define MW_RANGE_MIN 0
#define MW_RANGE_MAX 99
#define MW_RANGE_DEFAULT 50

#define MW_ASSIGN_MIN 0
#define MW_ASSIGN_MAX 7
#define MW_ASSIGN_DEFAULT 0

#define MW_MODE_MIN 0
#define MW_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define MW_MODE_DEFAULT 0

#define FC_RANGE_MIN 0
#define FC_RANGE_MAX 99
#define FC_RANGE_DEFAULT 50

#define FC_ASSIGN_MIN 0
#define FC_ASSIGN_MAX 7
#define FC_ASSIGN_DEFAULT 0

#define FC_MODE_MIN 0
#define FC_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define FC_MODE_DEFAULT 0

#define BC_RANGE_MIN 0
#define BC_RANGE_MAX 99
#define BC_RANGE_DEFAULT 50

#define BC_ASSIGN_MIN 0
#define BC_ASSIGN_MAX 7
#define BC_ASSIGN_DEFAULT 0

#define BC_MODE_MIN 0
#define BC_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define BC_MODE_DEFAULT 0

#define AT_RANGE_MIN 0
#define AT_RANGE_MAX 99
#define AT_RANGE_DEFAULT 50

#define AT_ASSIGN_MIN 0
#define AT_ASSIGN_MAX 7
#define AT_ASSIGN_DEFAULT 0

#define AT_MODE_MIN 0
#define AT_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define AT_MODE_DEFAULT 0

#define OP_ENABLED_MIN 0
#define OP_ENABLED_MAX 0x3f
#define OP_ENABLED_DEFAULT OP_ENABLED_MAX

#define PORTAMENTO_MODE_MIN 0
#define PORTAMENTO_MODE_MAX 1
#define PORTAMENTO_MODE_DEFAULT 0

#define PORTAMENTO_GLISSANDO_MIN 0
#define PORTAMENTO_GLISSANDO_MAX 1
#define PORTAMENTO_GLISSANDO_DEFAULT 0

#define PORTAMENTO_TIME_MIN 0
#define PORTAMENTO_TIME_MAX 99
#define PORTAMENTO_TIME_DEFAULT 0

#define INSTANCES_MIN 1
#define INSTANCES_MAX NUM_DEXED
#define INSTANCES_DEFAULT 1

#define INSTANCE_NOTE_START_MIN MIDI_AIS0
#define INSTANCE_NOTE_START_MAX MIDI_B7
#define INSTANCE_NOTE_START_DEFAULT INSTANCE_NOTE_START_MIN

#define INSTANCE_NOTE_END_MIN MIDI_AIS0
#define INSTANCE_NOTE_END_MAX MIDI_B7
#define INSTANCE_NOTE_END_DEFAULT INSTANCE_NOTE_END_MAX

#define VELOCITY_LEVEL_MIN 100
#define VELOCITY_LEVEL_MAX 127
#define VELOCITY_LEVEL_DEFAULT 100

typedef struct dexed_s {
  uint8_t lowest_note;
  uint8_t highest_note;
  uint8_t transpose;
  uint8_t tune;
  uint8_t sound_intensity;
  uint8_t pan;
  uint8_t polyphony;
  uint8_t velocity_level;
  uint8_t engine;
  uint8_t monopoly;
  uint8_t note_refresh;
  uint8_t pb_range;
  uint8_t pb_step;
  uint8_t mw_range;
  uint8_t mw_assign;
  uint8_t mw_mode;
  uint8_t fc_range;
  uint8_t fc_assign;
  uint8_t fc_mode;
  uint8_t bc_range;
  uint8_t bc_assign;
  uint8_t bc_mode;
  uint8_t at_range;
  uint8_t at_assign;
  uint8_t at_mode;
  uint8_t portamento_mode;
  uint8_t portamento_glissando;
  uint8_t portamento_time;
  uint8_t op_enabled;
  uint8_t midi_channel;
} dexed_t;

typedef struct fx_s {
  uint8_t filter_cutoff[MAX_DEXED];
  uint8_t filter_resonance[MAX_DEXED];
} fx_t;

typedef struct performance_s {
  uint8_t bank[MAX_DEXED];
  uint8_t voice[MAX_DEXED];
  int8_t voiceconfig_number[MAX_DEXED];
  int8_t fx_number;
} performance_t;

typedef struct sys_s {
  uint8_t instances;
  uint8_t vol;
  uint8_t mono;
  uint8_t soft_midi_thru;
  int8_t performance_number;
} sys_t;

typedef struct configuration_s {
  sys_t sys;
  fx_t fx;
  performance_t performance;
  dexed_t dexed[MAX_DEXED];
  uint16_t _marker_;
} config_t;

#if !defined(_MAPFLOAT)
#define _MAPFLOAT
inline float mapfloat(float val, float in_min, float in_max, float out_min, float out_max)
{
  return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#endif

#endif
