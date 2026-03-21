#include "microdexed_marki_minimal.h"

#include <algorithm>
#include <cstring>
#include <new>

MicroDexedMarkIMinimal::MicroDexedMarkIMinimal() = default;

MicroDexedMarkIMinimal::~MicroDexedMarkIMinimal()
{
  if (engine_ != nullptr)
    engine_->~Dexed();
}

bool MicroDexedMarkIMinimal::init(int sample_rate)
{
  if (engine_ != nullptr)
  {
    engine_->~Dexed();
    engine_ = nullptr;
  }

  engine_ = new (engine_storage_) Dexed(sample_rate);
  engine_->setEngineType(DEXED_ENGINE_MARKI);
  engine_->setMaxNotes(4);
  engine_->setMonoMode(false);
  engine_->fx.Gain = 1.0f;
  engine_->fx.Cutoff = 1.0f;
  engine_->fx.Reso = 0.0f;

  return true;
}

bool MicroDexedMarkIMinimal::loadPatchFromMemory(const uint8_t *patch_data)
{
  if (engine_ == nullptr || patch_data == nullptr)
    return false;

  return engine_->loadVoiceParameters(const_cast<uint8_t *>(patch_data));
}

void MicroDexedMarkIMinimal::loadDefaultPatch()
{
  if (engine_ == nullptr)
    return;

  uint8_t patch_copy[sizeof(engine_->data)];
  memcpy(patch_copy, engine_->data, sizeof(patch_copy));
  engine_->loadVoiceParameters(patch_copy);
}

void MicroDexedMarkIMinimal::noteOn(uint8_t note, uint8_t velocity)
{
  if (engine_ != nullptr)
    engine_->keydown(note, velocity);
}

void MicroDexedMarkIMinimal::noteOff(uint8_t note)
{
  if (engine_ != nullptr)
    engine_->keyup(note);
}

void MicroDexedMarkIMinimal::allNotesOff()
{
  if (engine_ != nullptr)
    engine_->notesOff();
}

void MicroDexedMarkIMinimal::render(int16_t *out, int nframes)
{
  if (engine_ == nullptr || out == nullptr || nframes <= 0)
    return;

  int offset = 0;
  while (offset < nframes)
  {
    const int frames = std::min(nframes - offset, DEXED_RENDER_MAX_FRAMES);
    engine_->getSamples(static_cast<uint16_t>(frames), out + offset);
    offset += frames;
  }
}
