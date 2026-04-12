#pragma once

#include <cstddef>
#include <cstdint>

#include "dexed.h"

class MicroDexedMarkIMinimal
{
  public:
    MicroDexedMarkIMinimal();
    ~MicroDexedMarkIMinimal();

    bool init(int sample_rate);
    bool loadPatchFromMemory(const uint8_t *patch_data);
    void loadDefaultPatch();
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void allNotesOff();
    void render(int16_t *out, int nframes);
    bool getPatchData(uint8_t *patch_data) const;
    bool setVoiceParameter(uint8_t offset, uint8_t value);
    uint8_t getVoiceParameter(uint8_t offset) const;
    void setMonoMode(bool enabled);
    bool isMonoMode() const;
    void setPitchBendRange(uint8_t range, uint8_t step);
    void setPortamento(uint8_t mode, uint8_t glissando, uint8_t time);
    void setOperatorMask(uint8_t mask);

  private:
    Dexed *engine_ = nullptr;
    alignas(Dexed) unsigned char engine_storage_[sizeof(Dexed)];
};
