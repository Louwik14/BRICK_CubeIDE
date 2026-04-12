#ifndef DEXED_H_INCLUDED
#define DEXED_H_INCLUDED

#include "microdexed_compat.h"
#include "controllers.h"
#include "dx7note.h"
#include "lfo.h"
#include "synth.h"
#include "fm_core.h"
#include "EngineMkI.h"
#include "config.h"

extern float vol;
extern float vol_right;
extern float vol_left;

struct ProcessorVoice {
  int16_t midi_note;
  uint8_t velocity;
  int16_t porta;
  bool keydown;
  bool sustained;
  bool live;
  uint32_t key_pressed_timer;
  Dx7Note *dx7_note;
};

enum DexedEngineResolution {
  DEXED_ENGINE_MODERN,
  DEXED_ENGINE_MARKI,
  DEXED_ENGINE_OPL
};

enum DexedVoiceOPParameters {
  DEXED_OP_EG_R1,
  DEXED_OP_EG_R2,
  DEXED_OP_EG_R3,
  DEXED_OP_EG_R4,
  DEXED_OP_EG_L1,
  DEXED_OP_EG_L2,
  DEXED_OP_EG_L3,
  DEXED_OP_EG_L4,
  DEXED_OP_LEV_SCL_BRK_PT,
  DEXED_OP_SCL_LEFT_DEPTH,
  DEXED_OP_SCL_RGHT_DEPTH,
  DEXED_OP_SCL_LEFT_CURVE,
  DEXED_OP_SCL_RGHT_CURVE,
  DEXED_OP_OSC_RATE_SCALE,
  DEXED_OP_AMP_MOD_SENS,
  DEXED_OP_KEY_VEL_SENS,
  DEXED_OP_OUTPUT_LEV,
  DEXED_OP_OSC_MODE,
  DEXED_OP_FREQ_COARSE,
  DEXED_OP_FREQ_FINE,
  DEXED_OP_OSC_DETUNE
};

#define DEXED_VOICE_OFFSET 126
enum DexedVoiceParameters {
  DEXED_PITCH_EG_R1,
  DEXED_PITCH_EG_R2,
  DEXED_PITCH_EG_R3,
  DEXED_PITCH_EG_R4,
  DEXED_PITCH_EG_L1,
  DEXED_PITCH_EG_L2,
  DEXED_PITCH_EG_L3,
  DEXED_PITCH_EG_L4,
  DEXED_ALGORITHM,
  DEXED_FEEDBACK,
  DEXED_OSC_KEY_SYNC,
  DEXED_LFO_SPEED,
  DEXED_LFO_DELAY,
  DEXED_LFO_PITCH_MOD_DEP,
  DEXED_LFO_AMP_MOD_DEP,
  DEXED_LFO_SYNC,
  DEXED_LFO_WAVE,
  DEXED_LFO_PITCH_MOD_SENS,
  DEXED_TRANSPOSE,
  DEXED_NAME
};

class Dexed
{
  public:
    Dexed(int rate);
    ~Dexed();
    void activate(void);
    void deactivate(void);
    uint8_t getEngineType();
    void setEngineType(uint8_t tp);
    bool isMonoMode(void);
    void setMonoMode(bool mode);
    void setRefreshMode(bool mode);
    void getSamples(uint16_t n_samples, int16_t* buffer);
    void panic(void);
    void notesOff(void);
    void resetControllers(void);
    void setMaxNotes(uint8_t n);
    uint8_t getMaxNotes(void);
    void doRefreshVoice(void);
    void setOPs(uint8_t ops);
    bool decodeVoice(uint8_t* encoded_data, uint8_t* data);
    bool encodeVoice(uint8_t* encoded_data);
    bool getVoiceData(uint8_t* data_copy);
    bool loadVoiceParameters(uint8_t* data);
    bool loadGlobalParameters(uint8_t* data);
    bool initGlobalParameters(void);
    void keyup(int16_t pitch);
    void keydown(int16_t pitch, uint8_t velo);
    void setSustain(bool sustain);
    bool getSustain(void);
    uint8_t getNumNotesPlaying(void);
    void setPBController(uint8_t pb_range, uint8_t pb_step);
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode);
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode);
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode);
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode);
    void setPortamentoMode(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time);

    ProcessorVoice voices[MAX_NOTES];
    Controllers controllers;

    uint8_t data[156] = {
      95, 29, 20, 50, 99, 95, 00, 00, 41, 00, 19, 00, 00, 03, 00, 06, 79, 00, 01, 00, 14,
      95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 00, 99, 00, 01, 00, 00,
      95, 29, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 06, 89, 00, 01, 00, 07,
      95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 07,
      95, 50, 35, 78, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 07, 58, 00, 14, 00, 07,
      96, 25, 25, 67, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 10,
      94, 67, 95, 60, 50, 50, 50, 50,
      04, 06, 00,
      34, 33, 00, 00, 00, 04,
      03, 24,
      69, 68, 80, 56, 85, 76, 84, 00, 00, 00
    };

    int lastKeyDown;

  protected:
    static const uint8_t MAX_ACTIVE_NOTES = MAX_NOTES;
    uint8_t max_notes = MAX_ACTIVE_NOTES;
    uint32_t key_event_id = 0;
    int16_t currentNote;
    bool sustain;
    float vuSignal;
    bool monoMode;
    bool refreshMode;
    bool refreshVoice;
    uint8_t engineType;
    VoiceStatus voiceStatus;
    Lfo lfo;
    EngineMkI engineMkIStorage_;
    Dx7Note voiceNoteStorage_[MAX_NOTES];
    EngineMkI* engineMkI;
};

#endif
