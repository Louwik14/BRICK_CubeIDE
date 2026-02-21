#include "warps/dsp/warps_fx.h"

#include <algorithm>
#include <cstdint>

void WarpsFX::Init(float sample_rate) {
  modulator_.Init(sample_rate);
  params_ = modulator_.mutable_parameters();

  params_->carrier_shape = 0;  // external carrier/modulator inputs
  params_->modulation_algorithm = 0.0f;
  params_->modulation_parameter = 0.0f;
  params_->channel_drive[0] = 0.0f;
  params_->channel_drive[1] = 0.0f;
}

void WarpsFX::Process(
    float* inL,
    float* inR,
    float* outL,
    float* outR,
    int size) {
  if (!params_ || size <= 0) {
    return;
  }

  int offset = 0;
  while (offset < size) {
    const int block_size = std::min(
        size - offset,
        static_cast<int>(warps::kMaxBlockSize));

    for (int i = 0; i < block_size; ++i) {
      in_buffer_[i].l = FloatToInt16(inL[offset + i]);
      in_buffer_[i].r = FloatToInt16(inR[offset + i]);
    }

    modulator_.Process(in_buffer_, out_buffer_, static_cast<size_t>(block_size));

    for (int i = 0; i < block_size; ++i) {
      outL[offset + i] = Int16ToFloat(out_buffer_[i].l);
      outR[offset + i] = Int16ToFloat(out_buffer_[i].r);
    }

    offset += block_size;
  }
}

void WarpsFX::SetAlgorithm(float algo) {
  if (!params_) {
    return;
  }
  params_->modulation_algorithm = Clamp01(algo);
}

void WarpsFX::SetParameter(float param) {
  if (!params_) {
    return;
  }
  params_->modulation_parameter = Clamp01(param);
}

void WarpsFX::SetDrive(float d1, float d2) {
  if (!params_) {
    return;
  }
  params_->channel_drive[0] = Clamp01(d1);
  params_->channel_drive[1] = Clamp01(d2);
}

float WarpsFX::Clamp01(float x) {
  return std::max(0.0f, std::min(1.0f, x));
}

short WarpsFX::FloatToInt16(float x) {
  const float clipped = std::max(-1.0f, std::min(1.0f, x));
  const int32_t scaled = static_cast<int32_t>(clipped * 32767.0f);
  return static_cast<short>(scaled);
}

float WarpsFX::Int16ToFloat(short x) {
  return static_cast<float>(x) / 32768.0f;
}
