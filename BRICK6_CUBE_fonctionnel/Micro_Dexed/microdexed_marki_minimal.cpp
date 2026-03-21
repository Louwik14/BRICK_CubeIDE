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


bool MicroDexedMarkIMinimal::getPatchData(uint8_t *patch_data) const
{
  if (engine_ == nullptr || patch_data == nullptr)
    return false;

  return engine_->getVoiceData(patch_data);
}

bool MicroDexedMarkIMinimal::setVoiceParameter(uint8_t offset, uint8_t value)
{
  if (engine_ == nullptr || offset >= sizeof(engine_->data))
    return false;

  engine_->data[offset] = value;
  engine_->doRefreshVoice();
  return true;
}

uint8_t MicroDexedMarkIMinimal::getVoiceParameter(uint8_t offset) const
{
  if (engine_ == nullptr || offset >= sizeof(engine_->data))
    return 0U;

  return engine_->data[offset];
}

void MicroDexedMarkIMinimal::setMonoMode(bool enabled)
{
  if (engine_ != nullptr)
    engine_->setMonoMode(enabled);
}

bool MicroDexedMarkIMinimal::isMonoMode() const
{
  return (engine_ != nullptr) ? engine_->isMonoMode() : false;
}

void MicroDexedMarkIMinimal::setPitchBendRange(uint8_t range, uint8_t step)
{
  if (engine_ != nullptr)
    engine_->setPBController(range, step);
}

void MicroDexedMarkIMinimal::setPortamento(uint8_t mode, uint8_t glissando, uint8_t time)
{
  if (engine_ != nullptr)
    engine_->setPortamentoMode(mode, glissando, time);
}

void MicroDexedMarkIMinimal::setOperatorMask(uint8_t mask)
{
  if (engine_ != nullptr)
    engine_->setOPs(mask);
}
