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

  private:
    Dexed *engine_ = nullptr;
    alignas(Dexed) unsigned char engine_storage_[sizeof(Dexed)];
};
