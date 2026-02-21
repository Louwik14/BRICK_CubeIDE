#pragma once

#include <cstddef>

#include "warps/dsp/modulator.h"

class WarpsFX {
 public:
  void Init(float sample_rate);

  void Process(float* inL,
               float* inR,
               float* outL,
               float* outR,
               int size);

  void SetAlgorithm(float algo);     // 0..1
  void SetParameter(float param);    // 0..1
  void SetDrive(float d1, float d2); // 0..1

 private:
  static float Clamp01(float x);
  static short FloatToInt16(float x);
  static float Int16ToFloat(short x);

  warps::Modulator modulator_;
  warps::Parameters* params_ = nullptr;
  warps::ShortFrame in_buffer_[warps::kMaxBlockSize];
  warps::ShortFrame out_buffer_[warps::kMaxBlockSize];
};

/*
Example:

WarpsFX warps;

void audio_init() {
  warps.Init(48000.0f);
  warps.SetAlgorithm(0.35f);
  warps.SetParameter(0.6f);
  warps.SetDrive(0.25f, 0.15f);
}

void mixer_process(float **in, float **out, uint32_t frames) {
  warps.Process(in[2], in[3], out[2], out[3], static_cast<int>(frames));
}
*/
