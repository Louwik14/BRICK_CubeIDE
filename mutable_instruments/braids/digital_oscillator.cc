// Copyright 2012 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Oscillator - digital style waveforms.

#include "braids/digital_oscillator.h"

#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"

#include "braids/parameter_interpolation.h"
#include "braids/resources.h"

namespace braids {
  
using namespace stmlib;

static const uint16_t kHighestNote = 140 * 128;
static const uint16_t kPitchTableStart = 128 * 128;
static const uint16_t kOctave = 12 * 128;

static const uint32_t kFIR4Coefficients[4] = { 10530, 14751, 16384, 14751 };
static const uint32_t kFIR4DcOffset = 28208;

uint32_t DigitalOscillator::ComputePhaseIncrement(int16_t midi_pitch) {
  if (midi_pitch >= kPitchTableStart) {
    midi_pitch = kPitchTableStart - 1;
  }
  
  int32_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  
  size_t num_shifts = 0;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  
  uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
  uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
  uint32_t phase_increment = a + \
      (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
  phase_increment >>= num_shifts;
  return phase_increment;
}

void DigitalOscillator::Render(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {

  // Quantize parameter for FM.
  if (shape_ >= OSC_SHAPE_FM &&
      shape_ <= OSC_SHAPE_CHAOTIC_FEEDBACK_FM) {
    uint16_t integral = parameter_[1] >> 8;
    uint16_t fractional = parameter_[1] & 255;
    int16_t a = lut_fm_frequency_quantizer[integral];
    int16_t b = lut_fm_frequency_quantizer[integral + 1];
    parameter_[1] = a + ((b - a) * fractional >> 8);
  }    
  
  if (shape_ != previous_shape_) {
    Init();
    previous_shape_ = shape_;
    init_ = true;
  }
  
  if (pitch_ != phase_increment_pitch_) {
    phase_increment_ = ComputePhaseIncrement(pitch_);
    phase_increment_pitch_ = pitch_;
  }
  
  if (pitch_ > kHighestNote) {
    pitch_ = kHighestNote;
  } else if (pitch_ < 0) {
    pitch_ = 0;
  }

  switch (static_cast<uint8_t>(shape_)) {
    case 0: RenderTripleRingMod(sync, buffer, size); break;
    case 1: RenderSawSwarm(sync, buffer, size); break;
    case 2: RenderComb(sync, buffer, size); break;
    case 3: RenderToy(sync, buffer, size); break;
    case 4:
    case 5:
    case 6:
    case 7: RenderDigitalFilter(sync, buffer, size); break;
    case 8: RenderVosim(sync, buffer, size); break;
    case 9: RenderVowel(sync, buffer, size); break;
    case 10: RenderVowelFof(sync, buffer, size); break;
    case 12: RenderFm(sync, buffer, size); break;
    case 13: RenderFeedbackFm(sync, buffer, size); break;
    case 14: RenderChaoticFeedbackFm(sync, buffer, size); break;
    case 24: RenderWavetables(sync, buffer, size); break;
    case 25: RenderWaveMap(sync, buffer, size); break;
    case 26: RenderWaveLine(sync, buffer, size); break;
    case 27: RenderWaveParaphonic(sync, buffer, size); break;
    case 28: RenderFilteredNoise(sync, buffer, size); break;
    case 29: RenderTwinPeaksNoise(sync, buffer, size); break;
    case 30: RenderClockedNoise(sync, buffer, size); break;
    case 31: RenderGranularCloud(sync, buffer, size); break;
    case 32: RenderParticleNoise(sync, buffer, size); break;
    case 33: RenderDigitalModulation(sync, buffer, size); break;
    case 34: RenderQuestionMark(sync, buffer, size); break;
    default: break;
  }
}

void DigitalOscillator::RenderTripleRingMod(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  uint32_t phase = phase_ + (1L << 30);
  uint32_t increment = phase_increment_;
  uint32_t modulator_phase = state_.vow.formant_phase[0];
  uint32_t modulator_phase_2 = state_.vow.formant_phase[1];
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
    pitch_ + ((parameter_[0] - 16384) >> 2)
  );
  uint32_t modulator_phase_increment_2 = ComputePhaseIncrement(
    pitch_ + ((parameter_[1] - 16384) >> 2)
  );
  
  while (size--) {
    phase += increment;
    if (sync != NULL && *sync++) {
      phase = 0;
      modulator_phase = 0;
      modulator_phase_2 = 0;
    }
    modulator_phase += modulator_phase_increment;
    modulator_phase_2 += modulator_phase_increment_2;
    int16_t result = Interpolate824(wav_sine, phase);
    result = result * Interpolate824(wav_sine, modulator_phase) >> 16;
    result = result * Interpolate824(wav_sine, modulator_phase_2) >> 16;
    result = Interpolate88(ws_moderate_overdrive, result + 32768);
    *buffer++ = result;
  }
  phase_ = phase - (1L << 30);
  state_.vow.formant_phase[0] = modulator_phase;
  state_.vow.formant_phase[1] = modulator_phase_2;
}

void DigitalOscillator::RenderSawSwarm(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int32_t detune = parameter_[0] + 1024;
  detune = (detune * detune) >> 9;
  uint32_t increments[7];
  for (int16_t i = 0; i < 7; ++i) {
    int32_t saw_detune = detune * (i - 3);
    int32_t detune_integral = saw_detune >> 16;
    int32_t detune_fractional = saw_detune & 0xffff;
    int32_t increment_a = ComputePhaseIncrement(pitch_ + detune_integral);
    int32_t increment_b = ComputePhaseIncrement(pitch_ + detune_integral + 1);
    increments[i] = increment_a + \
        (((increment_b - increment_a) * detune_fractional) >> 16);
  }
  if (strike_) {
    for (size_t i = 0; i < 6; ++i) {
      state_.saw.phase[i] = Random::GetWord();
    }
    strike_ = false;
  }
  int32_t hp_cutoff = pitch_;
  if (parameter_[1] < 10922) {
    hp_cutoff += ((parameter_[1] - 10922) * 24) >> 5;
  } else {
    hp_cutoff += ((parameter_[1] - 10922) * 12) >> 5;
  }
  if (hp_cutoff < 0) {
    hp_cutoff = 0;
  } else if (hp_cutoff > 32767) {
    hp_cutoff = 32767;
  }
  
  int32_t f = Interpolate824(lut_svf_cutoff, hp_cutoff << 17);
  int32_t damp = lut_svf_damp[0];
  int32_t bp = state_.saw.bp;
  int32_t lp = state_.saw.lp;

  while (size--) {
    if (sync != NULL && *sync++) {
      for (size_t i = 0; i < 6; ++i) {
        state_.saw.phase[i] = 0;
      }
    }
    int32_t notch, hp, sample;
    
    phase_ += increments[0];
    state_.saw.phase[0] += increments[1];
    state_.saw.phase[1] += increments[2];
    state_.saw.phase[2] += increments[3];
    state_.saw.phase[3] += increments[4];
    state_.saw.phase[4] += increments[5];
    state_.saw.phase[5] += increments[6];
    
    // Compute a sample.
    sample = -28672;
    sample += phase_ >> 19;
    sample += state_.saw.phase[0] >> 19;
    sample += state_.saw.phase[1] >> 19;
    sample += state_.saw.phase[2] >> 19;
    sample += state_.saw.phase[3] >> 19;
    sample += state_.saw.phase[4] >> 19;
    sample += state_.saw.phase[5] >> 19;
    sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    
    notch = sample - (bp * damp >> 15);
    lp += f * bp >> 15;
    CLIP(lp)
    hp = notch - lp;
    bp += f * hp >> 15;
    
    int32_t result = hp;
    CLIP(result)
    *buffer++ = result;
  }
  state_.saw.lp = lp;
  state_.saw.bp = bp;
}

void DigitalOscillator::RenderComb(
    const uint8_t* sync,
     int16_t* buffer,
     size_t size) {
  (void)sync;
  // Delay-line free mode: keep the dry pre-comb source already in buffer.
  while (size--) {
    ++buffer;
  }
}

void DigitalOscillator::RenderToy(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  // 4 times oversampling.
  phase_increment_ >>= 2;
  
  uint32_t phase_increment = phase_increment_;
  uint32_t phase = phase_;
  
  uint16_t decimation_counter = state_.toy.decimation_counter;
  uint16_t decimation_count = 512 - (parameter_[0] >> 6);

  uint8_t held_sample = state_.toy.held_sample;
  while (size--) {
    int32_t filtered_sample = 0;
    if (sync != NULL && *sync++) {
      phase = 0;
    } 
    for (size_t tap = 0; tap < 4; ++tap) {
      phase += phase_increment;
      if (decimation_counter >= decimation_count) {
        uint8_t x = parameter_[1] >> 8;
        held_sample = (((phase >> 24) ^ (x << 1)) & (~x)) + (x >> 1);
        decimation_counter = 0;
      }
      filtered_sample += kFIR4Coefficients[tap] * held_sample;
      ++decimation_counter;
    }
    *buffer++ = (filtered_sample >> 8) - kFIR4DcOffset;
  }
  state_.toy.held_sample = held_sample;
  state_.toy.decimation_counter = decimation_counter;
  phase_ = phase;
}
 
const uint32_t kPhaseReset[] = {
  0,
  0x80000000,
  0x40000000,
  0x80000000
};

void DigitalOscillator::RenderDigitalFilter(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int16_t shifted_pitch = pitch_ + ((parameter_[0] - 2048) >> 1);
  if (shifted_pitch > 16383) {
    shifted_pitch = 16383;
  }
  uint32_t modulator_phase = state_.res.modulator_phase;
  uint32_t square_modulator_phase = state_.res.square_modulator_phase;
  int32_t square_integrator = state_.res.integrator;
  
  uint8_t filter_type = shape_ - OSC_SHAPE_DIGITAL_FILTER_LP;
  
  uint32_t modulator_phase_increment = state_.res.modulator_phase_increment;
  uint32_t target_increment = ComputePhaseIncrement(shifted_pitch);
  uint32_t modulator_phase_increment_increment = 
    modulator_phase_increment < target_increment
    ? (target_increment - modulator_phase_increment) / size
    : ~((modulator_phase_increment - target_increment) / size);
    
  while (size--) {
    phase_ += phase_increment_;
    modulator_phase_increment += modulator_phase_increment_increment;
    modulator_phase += modulator_phase_increment;
    uint16_t integrator_gain = (modulator_phase_increment >> 14);
    
    if (sync != NULL && *sync++) {
      state_.res.polarity = 1;
      phase_ = 0;
      modulator_phase = 0;
      square_modulator_phase = 0;
      square_integrator = 0;
    }
    
    square_modulator_phase += modulator_phase_increment;
    if (phase_ < phase_increment_) {
      modulator_phase = kPhaseReset[filter_type];
    }
    if ((phase_ << 1) < (phase_increment_ << 1)) {
      state_.res.polarity = !state_.res.polarity;
      square_modulator_phase = kPhaseReset[(filter_type & 1) + 2];
    }
    
    int32_t carrier = Interpolate824(wav_sine, modulator_phase);
    int32_t square_carrier = Interpolate824(wav_sine, square_modulator_phase);
    
    uint16_t saw = ~(phase_ >> 16);
    uint16_t double_saw = ~(phase_ >> 15);
    uint16_t triangle = (phase_ >> 15) ^ (phase_ & 0x80000000 ? 0xffff : 0x0000);
    uint16_t window = parameter_[1] < 16384 ? saw : triangle;

    int32_t pulse = (square_carrier * double_saw) >> 16;
    if (state_.res.polarity) {
      pulse = -pulse;
    }
    square_integrator += (pulse * integrator_gain) >> 16;
    CLIP(square_integrator)
    
    int16_t saw_tri_signal;
    int16_t square_signal;
    
    if (filter_type & 2) {
      saw_tri_signal = (carrier * window) >> 16;
      square_signal = pulse;
    } else {
      saw_tri_signal = (window * (carrier + 32768) >> 16) - 32768;
      square_signal = square_integrator;
      if (filter_type == 1) {
        square_signal = (pulse + square_integrator) >> 1;
      }
    }
    uint16_t balance = (parameter_[1] < 16384 ? 
                        parameter_[1] : ~parameter_[1]) << 2;
    *buffer++ = Mix(saw_tri_signal, square_signal, balance);
  }
  state_.res.modulator_phase = modulator_phase;
  state_.res.square_modulator_phase = square_modulator_phase;
  state_.res.integrator = square_integrator;
  state_.res.modulator_phase_increment = modulator_phase_increment;
}

void DigitalOscillator::RenderVosim(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  for (size_t i = 0; i < 2; ++i) {
    state_.vow.formant_increment[i] = ComputePhaseIncrement(parameter_[i] >> 1);
  }
  while (size--) {
    phase_ += phase_increment_;
    if (sync != NULL && *sync++) {
      phase_ = 0;
    }
    int32_t sample = 16384 + 8192;
    state_.vow.formant_phase[0] += state_.vow.formant_increment[0];
    sample += Interpolate824(wav_sine, state_.vow.formant_phase[0]) >> 1;
    
    state_.vow.formant_phase[1] += state_.vow.formant_increment[1];
    sample += Interpolate824(wav_sine, state_.vow.formant_phase[1]) >> 2;
    
    sample = sample * (Interpolate824(lut_bell, phase_) >> 1) >> 15;
    if (phase_ < phase_increment_) {
      state_.vow.formant_phase[0] = 0;
      state_.vow.formant_phase[1] = 0;
      sample = 0;
    }
    sample -= 16384 + 8192;
    *buffer++ = sample;
  }
}

struct PhonemeDefinition {
  uint8_t formant_frequency[3];
  uint8_t formant_amplitude[3];
};

static const PhonemeDefinition vowels_data[9] = {
    { { 27,  40,  89 }, { 15,  13,  1 } },
    { { 18,  51,  62 }, { 13,  12,  6 } },
    { { 15,  69,  93 }, { 14,  12,  7 } },
    { { 10,  84, 110 }, { 13,  10,  8 } },
    { { 23,  44,  87 }, { 15,  12,  1 } },
    { { 13,  29,  80 }, { 13,   8,  0 } },
    { {  6,  46,  81 }, { 12,   3,  0 } },
    { {  9,  51,  95 }, { 15,   3,  0 } },
    { {  6,  73,  99 }, {  7,   3,  14 } }
};

static const PhonemeDefinition consonant_data[8] = {
    { { 6, 54, 121 }, { 9,  9,  0 } },
    { { 18, 50, 51 }, { 12,  10,  5 } },
    { { 11, 24, 70 }, { 13,  8,  0 } },
    { { 15, 69, 74 }, { 14,  12,  7 } },
    { { 16, 37, 111 }, { 14,  8,  1 } },
    { { 18, 51, 62 }, { 14,  12,  6 } },
    { { 6, 26, 81 }, { 5,  5,  5 } },
    { { 6, 73, 99 }, { 7,  10,  14 } },
};


void DigitalOscillator::RenderVowel(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  size_t vowel_index = parameter_[0] >> 12;
  uint16_t balance = parameter_[0] & 0x0fff;
  uint16_t formant_shift = (200 + (parameter_[1] >> 6));
  if (strike_) {
    strike_ = false;
    state_.vow.consonant_frames = 160;
    uint16_t index = (Random::GetSample() + 1) & 7;
    for (size_t i = 0; i < 3; ++i) {
      state_.vow.formant_increment[i] = \
          static_cast<uint32_t>(consonant_data[index].formant_frequency[i]) * \
          0x1000 * formant_shift;
      state_.vow.formant_amplitude[i] = consonant_data[index].formant_amplitude[i];
    }
    state_.vow.noise = index >= 6 ? 4095 : 0;
  }
  
  if (state_.vow.consonant_frames) {
    --state_.vow.consonant_frames;
  } else {
    for (size_t i = 0; i < 3; ++i) {
      state_.vow.formant_increment[i] = 
          (vowels_data[vowel_index].formant_frequency[i] * (0x1000 - balance) + \
           vowels_data[vowel_index + 1].formant_frequency[i] * balance) * \
           formant_shift;
      state_.vow.formant_amplitude[i] =
          (vowels_data[vowel_index].formant_amplitude[i] * (0x1000 - balance) + \
           vowels_data[vowel_index + 1].formant_amplitude[i] * balance) >> 12;
    }
    state_.vow.noise = 0;
  }
  int32_t noise = state_.vow.noise;
  
  while (size--) {
    phase_ += phase_increment_;
    size_t phaselet;
    int16_t sample = 0;
    state_.vow.formant_phase[0] += state_.vow.formant_increment[0];
    phaselet = (state_.vow.formant_phase[0] >> 24) & 0xf0;
    sample += wav_formant_sine[phaselet | state_.vow.formant_amplitude[0]];

    state_.vow.formant_phase[1] += state_.vow.formant_increment[1];
    phaselet = (state_.vow.formant_phase[1] >> 24) & 0xf0;
    sample += wav_formant_sine[phaselet | state_.vow.formant_amplitude[1]];
    
    state_.vow.formant_phase[2] += state_.vow.formant_increment[2];
    phaselet = (state_.vow.formant_phase[2] >> 24) & 0xf0;
    sample += wav_formant_square[phaselet | state_.vow.formant_amplitude[2]];
    
    sample *= 255 - (phase_ >> 24);
    int32_t phase_noise = Random::GetSample() * noise;
    if ((phase_ + phase_noise) < phase_increment_) {
      state_.vow.formant_phase[0] = 0;
      state_.vow.formant_phase[1] = 0;
      state_.vow.formant_phase[2] = 0;
      sample = 0;
    }
    sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    *buffer++ = sample;
  }
}

static const int16_t formant_f_data[kNumFormants][kNumFormants][kNumFormants] = {
  // bass
  {
    { 9519, 10738, 12448, 12636, 12892 }, // a
    { 8620, 11720, 12591, 12932, 13158 }, // e
    { 7579, 11891, 12768, 13122, 13323 }, // i
    { 8620, 10013, 12591, 12768, 13010 }, // o
    { 8324, 9519, 12591, 12831, 13048 } // u
  },
  // tenor
  {
    { 9696, 10821, 12810, 13010, 13263 }, // a
    { 8620, 11827, 12768, 13228, 13477 }, // e
    { 7908, 12038, 12932, 13263, 13452 }, // i
    { 8620, 10156, 12768, 12932, 13085 }, // o
    { 8324, 9519, 12852, 13010, 13296 } // u
  },
  // countertenor
  {
    { 9730, 10902, 12892, 13085, 13330 }, // a
    { 8832, 11953, 12852, 13085, 13296 }, // e
    { 7749, 12014, 13010, 13330, 13483 }, // i
    { 8781, 10211, 12852, 13085, 13296 }, // o
    { 8448, 9627, 12892, 13085, 13363 } // u
  },
  // alto
  {
    { 10156, 10960, 12932, 13427, 14195 }, // a
    { 8620, 11692, 12852, 13296, 14195 }, // e
    { 8324, 11827, 12852, 13550, 14195 }, // i
    { 8881, 10156, 12956, 13427, 14195 }, // o
    { 8160, 9860, 12708, 13427, 14195 } // u
  },
  // soprano
  {
    { 10156, 10960, 13010, 13667, 14195 }, // a
    { 8324, 12187, 12932, 13489, 14195 }, // e
    { 7749, 12337, 13048, 13667, 14195 }, // i
    { 8881, 10156, 12956, 13609, 14195 }, // o
    { 8160, 9860, 12852, 13609, 14195 } // u
  }
};

static const int16_t formant_a_data[kNumFormants][kNumFormants][kNumFormants] = {
  // bass
  {
    { 16384, 7318, 5813, 5813, 1638 }, // a
    { 16384, 4115, 5813, 4115, 2062 }, // e
    { 16384, 518, 2596, 1301, 652 }, // i
    { 16384, 4617, 1460, 1638, 163 }, // o
    { 16384, 1638, 411, 652, 259 } // u
  },
  // tenor
  {
    { 16384, 8211, 7318, 6522, 1301 }, // a
    { 16384, 3269, 4115, 3269, 1638 }, // e
    { 16384, 2913, 2062, 1638, 518 }, // i
    { 16384, 5181, 4115, 4115, 821 }, // o
    { 16384, 1638, 2314, 3269, 821 } // u
  },
  // countertenor
  {
    { 16384, 8211, 1159, 1033, 206 }, // a
    { 16384, 3269, 2062, 1638, 1638 }, // e
    { 16384, 1033, 1033, 259, 259 }, // i
    { 16384, 5181, 821, 1301, 326 }, // o
    { 16384, 1638, 1159, 518, 326 } // u
  },
  // alto
  {
    { 16384, 10337, 1638, 259, 16 }, // a
    { 16384, 1033, 518, 291, 16 }, // e
    { 16384, 1638, 518, 259, 16 }, // i
    { 16384, 5813, 2596, 652, 29 }, // o
    { 16384, 4115, 518, 163, 10 } // u
  },
  // soprano
  {
    { 16384, 8211, 411, 1638, 51 }, // a
    { 16384, 1638, 2913, 163, 25 }, // e
    { 16384, 4115, 821, 821, 103 }, // i
    { 16384, 4617, 1301, 1301, 51 }, // o
    { 16384, 2596, 291, 163, 16 } // u
  }
};

int16_t DigitalOscillator::InterpolateFormantParameter(
    const int16_t table[][kNumFormants][kNumFormants],
    int16_t x,
    int16_t y,
    uint8_t formant) {
  uint16_t x_index = x >> 13;
  uint16_t x_mix = x << 3;
  uint16_t y_index = y >> 13;
  uint16_t y_mix = y << 3;
  int16_t a = table[x_index][y_index][formant];
  int16_t b = table[x_index + 1][y_index][formant];
  int16_t c = table[x_index][y_index + 1][formant];
  int16_t d = table[x_index + 1][y_index + 1][formant];
  a = a + ((b - a) * x_mix >> 16);
  c = c + ((d - c) * x_mix >> 16);
  return a + ((c - a) * y_mix >> 16);
}

void DigitalOscillator::RenderVowelFof(
  const uint8_t* sync,
  int16_t* buffer,
  size_t size) {

  // The original implementation used FOF but we live in the future and it's
  // less computationally expensive to render a proper bank of 5 SVF.

  const int16_t amplitude = InterpolateFormantParameter(
      formant_a_data,
      parameter_[1],
      parameter_[0],
      0);
  int32_t svf_lp[kNumFormants];
  int32_t svf_bp[kNumFormants];
  int16_t svf_f[kNumFormants];
  
  for (size_t i = 0; i < kNumFormants; ++i) {
    int32_t frequency = InterpolateFormantParameter(
        formant_f_data,
        parameter_[1],
        parameter_[0],
        i) + (12 << 7);
    svf_f[i] = Interpolate824(lut_svf_cutoff, frequency << 17);
    if (init_) {
      svf_lp[i] = 0;
      svf_bp[i] = 0;
    } else {
      svf_lp[i] = state_.fof.svf_lp[i];
      svf_bp[i] = state_.fof.svf_bp[i];
    }
  }
  
  if (init_) {
    init_ = false;
  }
  
  uint32_t phase = phase_;
  int32_t previous_sample = state_.fof.previous_sample;
  int32_t next_saw_sample = state_.fof.next_saw_sample;
  uint32_t increment = phase_increment_ << 1;
  while (size) {
    int32_t this_saw_sample = next_saw_sample;
    next_saw_sample = 0;
    phase += increment;
    if (phase < increment) {
      uint32_t t = phase / (increment >> 16);
      if (t > 65535) {
        t = 65535;
      }
      this_saw_sample -= static_cast<int32_t>(t * t >> 18);
      t = 65535 - t;
      next_saw_sample -= -static_cast<int32_t>(t * t >> 18);
    }
    next_saw_sample += phase >> 17;
    int32_t in = this_saw_sample;
    int32_t out = 0;
    for (int32_t i = 0; i < 5; ++i) {
      int32_t notch = in - (svf_bp[i] >> 6);
      svf_lp[i] += svf_f[i] * svf_bp[i] >> 15;
      CLIP(svf_lp[i])
      int32_t hp = notch - svf_lp[i];
      svf_bp[i] += svf_f[i] * hp >> 15;
      CLIP(svf_bp[i])
      out += svf_bp[i] * amplitude >> 17;
    }
    CLIP(out);
    *buffer++ = (out + previous_sample) >> 1;
    *buffer++ = out;
    previous_sample = out;
    size -= 2;
  }
  phase_ = phase;
  state_.fof.next_saw_sample = next_saw_sample;
  state_.fof.previous_sample = previous_sample;
  for (size_t i = 0; i < kNumFormants; ++i) {
    state_.fof.svf_lp[i] = svf_lp[i];
    state_.fof.svf_bp[i] = svf_bp[i];
  }
}

void DigitalOscillator::RenderFm(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  uint32_t modulator_phase = state_.modulator_phase;
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1)) >> 1;
  
  BEGIN_INTERPOLATE_PARAMETER_0    
  
  while (size--) {
    INTERPOLATE_PARAMETER_0
    
    phase_ += phase_increment_;
    if (sync != NULL && *sync++) {
      phase_ = modulator_phase = 0;
    }
    modulator_phase += modulator_phase_increment;

    uint32_t pm = (
        Interpolate824(wav_sine, modulator_phase) * parameter_0) << 2;
    *buffer++ = Interpolate824(wav_sine, phase_ + pm);
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  state_.modulator_phase = modulator_phase;
}

void DigitalOscillator::RenderFeedbackFm(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int16_t previous_sample = state_.ffm.previous_sample;
  uint32_t modulator_phase = state_.ffm.modulator_phase;

  int32_t attenuation = pitch_ - (72 << 7) + ((parameter_[1] - 16384) >> 1);
  attenuation = 32767 - attenuation * 4;
  if (attenuation < 0) attenuation = 0;
  if (attenuation > 32767) attenuation = 32767;
  
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1)) >> 1;
  
  BEGIN_INTERPOLATE_PARAMETER_0    
  
  while (size--) {
    INTERPOLATE_PARAMETER_0
    
    phase_ += phase_increment_;
    if (sync != NULL && *sync++) {
      phase_ = modulator_phase = 0;
    }
    
    modulator_phase += modulator_phase_increment;

    int32_t pm;
    int32_t p = parameter_0 * attenuation >> 15;
    pm = previous_sample << 14;
    pm = (
        Interpolate824(wav_sine, modulator_phase + pm) * p) << 1;
    previous_sample = Interpolate824(wav_sine, phase_ + pm);
    *buffer++ = previous_sample;
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  state_.ffm.previous_sample = previous_sample;
  state_.ffm.modulator_phase = modulator_phase;
}

void DigitalOscillator::RenderChaoticFeedbackFm(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  uint32_t modulator_phase_increment = ComputePhaseIncrement(
      (12 << 7) + pitch_ + ((parameter_[1] - 16384) >> 1)) >> 1;
  int16_t previous_sample = state_.ffm.previous_sample;
  uint32_t modulator_phase = state_.ffm.modulator_phase;
  
  BEGIN_INTERPOLATE_PARAMETER_0
  
  while (size--) {
    INTERPOLATE_PARAMETER_0
    
    phase_ += phase_increment_;
    if (sync != NULL && *sync++) {
      phase_ = modulator_phase = 0;
    }
    
    int32_t pm;
    pm = (Interpolate824(wav_sine, modulator_phase) * parameter_0) << 1;
    previous_sample = Interpolate824(wav_sine, phase_ + pm);
    *buffer++ = previous_sample;
    modulator_phase += (modulator_phase_increment >> 8) * \
        (129 + (previous_sample >> 9));
  }
  
  END_INTERPOLATE_PARAMETER_0
  
  state_.ffm.previous_sample = previous_sample;
  state_.ffm.modulator_phase = modulator_phase;
}


struct WavetableDefinition {
  uint8_t num_steps;
  uint8_t wave_index[17];
};

static const WavetableDefinition wavetable_definitions[] = {
// 01 male
{ 16 , { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15 } },
// 02 female
{ 16 , { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 31 } },
// 03 choir
{ 16 , { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 47 } },
// 04 space_voice
{ 16 , { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 63 } },
// 05 tampura
{ 16 , { 64, 65, 66, 67, 68, 68, 69, 70, 71, 72, 73, 73, 74, 75, 75, 76, 76 } },
// 06 shamus
{ 16 , { 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 92 } },
// 07 swept_string
{ 16 , { 93, 94, 95, 96, 97, 98, 99, 100, 101,
         102, 103, 104, 105, 106, 107, 108, 108 } },
// 08 bowed
{ 16 , { 109, 110, 111, 112, 113, 114, 115, 116,
         117, 118, 119, 120, 121, 122, 123, 124, 124 } },
// 09 cello
{ 16 , { 125, 126, 127, 128, 129, 130, 131, 132,
         132, 132, 132, 132, 132, 132, 132, 132, 132 } },
// 10 vibes
{ 16 , { 133, 134, 135, 136, 137, 138, 139, 140,
         141, 142, 143, 144, 144, 144, 145, 145, 145 } },
// 11 slap
{ 16 , { 146, 147, 148, 149, 150, 151, 151, 151,
         152, 152, 152, 152, 153, 153, 153, 153, 153 } },
// 12 piano
{ 8 , { 154, 154, 154, 154, 154, 154, 155, 156, 156 } },
// 13 organ!
{ 16 , { 176, 157, 158, 159, 160, 161, 162, 163,
         164, 165, 166, 167, 168, 169, 170, 171, 171 } },
// 14 waves!
{ 16 , { 172, 173, 174, 175, 176, 177, 178, 179,
         180, 181, 182, 183, 184, 185, 186, 187, 187 } },
// 15 digital
{ 16 , { 176, 188, 189, 190, 191, 192, 193, 194,
         195, 196, 197, 198, 199, 200, 201, 202, 202 } },
// 16 drone 1
{ 16 , { 203, 205, 204, 205, 212, 206, 207, 208,
         208, 209, 210, 210, 211, 211, 212, 212, 212 } },
// 17 drone 2
{ 8 , { 213, 213, 213, 214, 215, 216, 217, 218, 219 } },
// 18 metallic
{ 16 , { 220, 221, 222, 223, 224, 225, 226, 227,
         228, 229, 230, 231, 232, 233, 234, 235, 235 } },
// 19 fantasy
{ 16 , { 236, 237, 238, 239, 240, 241, 242, 243,
         244, 245, 246, 247, 248, 249, 250, 251, 251 } },
// 20 bell
{ 4 , { 252, 253, 254, 255, 254 } },
};

void DigitalOscillator::RenderWavetables(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  // Add some hysteresis to the second parameter to prevent a single DAC bit
  // error to cause a sharp and glitchy wavetable transition.
  if ((parameter_[1] > previous_parameter_[1] + 64) ||
      (parameter_[1] < previous_parameter_[1] - 64)) {
    previous_parameter_[1] = parameter_[1];
  }
      
  uint32_t wavetable_index = static_cast<uint32_t>(previous_parameter_[1]) * 20;
  wavetable_index >>= 15;
  
  uint32_t wave_pointer;
  const uint8_t* wave[2];
  const WavetableDefinition& wt = wavetable_definitions[wavetable_index];
  
  wave_pointer = (parameter_[0] << 1) * wt.num_steps;
  for (size_t i = 0; i < 2; ++i) {
    size_t wave_index = wt.wave_index[(wave_pointer >> 16) + i];
    wave[i] = wt_waves + wave_index * 129;
  }

  uint32_t phase_increment = phase_increment_ >> 1;
  while (size--) {
    int16_t sample;
    // 2x naive oversampling.
    phase_ += phase_increment;
    if (sync != NULL && *sync++) {
      phase_ = 0;
    }
    
    sample = Crossfade(wave[0], wave[1], phase_ >> 1, wave_pointer) >> 1;
    phase_ += phase_increment;
    sample += Crossfade(wave[0], wave[1], phase_ >> 1, wave_pointer) >> 1;
    *buffer++ = sample;
  }
}

void DigitalOscillator::RenderWaveMap(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  
  // The grid is 16x16; so there are 15 interpolation squares.
  uint16_t p[2];
  uint16_t wave_xfade[2];
  uint16_t wave_coordinate[2];

  p[0] = parameter_[0] * 15 >> 4;
  p[1] = parameter_[1] * 15 >> 4;
  wave_xfade[0] = p[0] << 5;
  wave_xfade[1] = p[1] << 5;
  wave_coordinate[0] = p[0] >> 11;
  wave_coordinate[1] = p[1] >> 11;

  const uint8_t* wave[2][2];
  
  for (size_t i = 0; i < 2; ++i) {
    for (size_t j = 0; j < 2; ++j) {
      uint16_t wave_index = \
          (wave_coordinate[0] + i) * 16 + (wave_coordinate[1] + j);
      wave[i][j] = wt_waves + wt_map[wave_index] * 129;
    }
  }

  uint32_t phase_increment = phase_increment_ >> 1;
  while (size--) {
    int16_t sample;
    // 2x naive oversampling.
    phase_ += phase_increment;
    if (sync != NULL && *sync++) {
      phase_ = 0;
    }
    
    sample = Mix(
        Crossfade(wave[0][0], wave[0][1], phase_ >> 1, wave_xfade[1]),
        Crossfade(wave[1][0], wave[1][1], phase_ >> 1, wave_xfade[1]),
        wave_xfade[0]) >> 1;
    phase_ += phase_increment;
    sample += Mix(
        Crossfade(wave[0][0], wave[0][1], phase_ >> 1, wave_xfade[1]),
        Crossfade(wave[1][0], wave[1][1], phase_ >> 1, wave_xfade[1]),
        wave_xfade[0]) >> 1;
    *buffer++ = sample;
  }
}

static const uint8_t wave_line[] = {
  187, 179, 154, 155, 135, 134, 137, 19, 24, 3, 8, 66, 79, 25, 180, 174, 64,
  127, 198, 15, 10, 7, 11, 0, 191, 192, 115, 238, 237, 236, 241, 47, 70, 76,
  235, 26, 133, 208, 34, 175, 183, 146, 147, 148, 150, 151, 152, 153, 117,
  138, 32, 33, 35, 125, 199, 201, 30, 31, 193, 27, 29, 21, 18, 182
};


static const uint8_t mini_wave_line[] = {
  157, 161, 171, 188, 189, 191, 192, 193, 196, 198, 201, 234, 232,
  229, 226, 224, 1, 2, 3, 4, 5, 8, 12, 32, 36, 42, 47, 252, 254, 141, 139,
  135, 174
};

void DigitalOscillator::RenderWaveLine(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  smoothed_parameter_ = (3 * smoothed_parameter_ + (parameter_[0] << 1)) >> 2;

  uint16_t scan = smoothed_parameter_;
  const uint8_t* wave_0 = wt_waves + wave_line[previous_parameter_[0] >> 9] * 129;
  const uint8_t* wave_1 = wt_waves + wave_line[scan >> 10] * 129;
  const uint8_t* wave_2 = wt_waves + wave_line[(scan >> 10) + 1] * 129;

  uint16_t smooth_xfade = scan << 6;
  uint16_t rough_xfade = 0;
  uint16_t rough_xfade_increment = 32768 / size;
  uint32_t balance = parameter_[1] << 3;

  uint32_t phase = phase_;
  uint32_t phase_increment = phase_increment_ >> 1;
  
  int16_t rough, smooth;
  
  if (parameter_[1] < 8192) {
    while (size--) {
      if (sync != NULL && *sync++) {
        phase = 0;
      }
      int32_t sample = 0;
      
      rough = Crossfade(wave_0, wave_1, (phase >> 1) & 0xfe000000, rough_xfade);
      smooth = Crossfade(wave_0, wave_1, phase >> 1, rough_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      rough = Crossfade(wave_0, wave_1, (phase >> 1) & 0xfe000000, rough_xfade);
      smooth = Crossfade(wave_0, wave_1, phase >> 1, rough_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      *buffer++ = sample >> 1;
    }
  } else if (parameter_[1] < 16384) {
    while (size--) {
      if (sync != NULL && *sync++) {
        phase = 0;
      }
      int32_t sample = 0;
      
      rough = Crossfade(wave_0, wave_1, phase >> 1, rough_xfade);
      smooth = Crossfade(wave_1, wave_2, phase >> 1, smooth_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;
      
      rough = Crossfade(wave_0, wave_1, phase >> 1, rough_xfade);
      smooth = Crossfade(wave_1, wave_2, phase >> 1, smooth_xfade);
      sample += Mix(rough, smooth, balance);
      phase += phase_increment;
      rough_xfade += rough_xfade_increment;

      *buffer++ = sample >> 1;
    }
  } else if (parameter_[1] < 24576) {
    while (size--) {
      if (sync != NULL && *sync++) {
        phase = 0;
      }
      int32_t sample = 0;
      
      smooth = Crossfade(wave_1, wave_2, phase >> 1, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (phase >> 1) & 0xfe000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      smooth = Crossfade(wave_1, wave_2, phase >> 1, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (phase >> 1) & 0xfe000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      *buffer++ = sample >> 1;
    }
  } else {
    while (size--) {
      if (sync != NULL && *sync++) {
        phase = 0;
      }
      int32_t sample = 0;
      smooth = Crossfade(wave_1, wave_2, (phase >> 1) & 0xfe000000, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (phase >> 1) & 0xf8000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      smooth = Crossfade(wave_1, wave_2, (phase >> 1) & 0xfe000000, smooth_xfade);
      rough = Crossfade(wave_1, wave_2, (phase >> 1) & 0xf8000000, smooth_xfade);
      sample += Mix(smooth, rough, balance);
      phase += phase_increment;

      *buffer++ = sample >> 1;
    }
  }
  phase_ = phase;
  previous_parameter_[0] = smoothed_parameter_ >> 1;
}

#define SEMI * 128

static const uint16_t chords[17][3] = {
  { 2, 4, 6 },
  { 16, 32, 48 },
  { 2 SEMI, 7 SEMI, 12 SEMI },
  { 3 SEMI, 7 SEMI, 10 SEMI },
  { 3 SEMI, 7 SEMI, 12 SEMI },
  { 3 SEMI, 7 SEMI, 14 SEMI },
  { 3 SEMI, 7 SEMI, 17 SEMI },
  { 7 SEMI, 12 SEMI, 19 SEMI },
  { 7 SEMI, 3 + 12 SEMI, 5 + 19 SEMI },
  { 4 SEMI, 7 SEMI, 17 SEMI },
  { 4 SEMI, 7 SEMI, 14 SEMI },
  { 4 SEMI, 7 SEMI, 12 SEMI },
  { 4 SEMI, 7 SEMI, 11 SEMI },
  { 5 SEMI, 7 SEMI, 12 SEMI },
  { 4, 7 SEMI, 12 SEMI },
  { 4, 4 + 12 SEMI, 12 SEMI },
  { 4, 4 + 12 SEMI, 12 SEMI },
};

void DigitalOscillator::RenderWaveParaphonic(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  if (strike_) {
    for (size_t i = 0; i < 4; ++i) {
      state_.saw.phase[i] = Random::GetWord();
    }
    strike_ = false;
  }
  
  // Do not use an array here to allow these to be kept in arbitrary registers.
  uint32_t phase_0, phase_1, phase_2, phase_3;
  uint32_t phase_increment[3];
  uint32_t phase_increment_0;

  phase_increment_0 = phase_increment_;
  phase_0 = state_.saw.phase[0];
  phase_1 = state_.saw.phase[1];
  phase_2 = state_.saw.phase[2];
  phase_3 = state_.saw.phase[3];
  
  uint16_t chord_integral = parameter_[1] >> 11;
  uint16_t chord_fractional = parameter_[1] << 5;
  if (chord_fractional < 30720) {
    chord_fractional = 0;
  } else if (chord_fractional >= 34816) {
    chord_fractional = 65535;
  } else {
    chord_fractional = (chord_fractional - 30720) * 16;
  }
  
  for (size_t i = 0; i < 3; ++i) {
    uint16_t detune_1 = chords[chord_integral][i];
    uint16_t detune_2 = chords[chord_integral + 1][i];
    uint16_t detune = detune_1 + ((detune_2 - detune_1) * chord_fractional >> 16);
    phase_increment[i] = ComputePhaseIncrement(pitch_ + detune);
  }

  const uint8_t* wave_1 = wt_waves + mini_wave_line[parameter_[0] >> 10] * 129;
  const uint8_t* wave_2 = wt_waves + mini_wave_line[(parameter_[0] >> 10) + 1] * 129;
  uint16_t wave_xfade = parameter_[0] << 6;
  
  while (size) {
    int32_t sample = 0;
    
    phase_0 += phase_increment_0;
    phase_1 += phase_increment[0];
    phase_2 += phase_increment[1];
    phase_3 += phase_increment[2];

    sample += Crossfade(wave_1, wave_2, phase_0 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_1 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_2 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_3 >> 1, wave_xfade);
    *buffer++ = sample >> 2;
    
    phase_0 += phase_increment_0;
    phase_1 += phase_increment[0];
    phase_2 += phase_increment[1];
    phase_3 += phase_increment[2];
    
    sample = 0;
    sample += Crossfade(wave_1, wave_2, phase_0 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_1 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_2 >> 1, wave_xfade);
    sample += Crossfade(wave_1, wave_2, phase_3 >> 1, wave_xfade);
    *buffer++ = sample >> 2;
    size -= 2;
  }
  
  state_.saw.phase[0] = phase_0;
  state_.saw.phase[1] = phase_1;
  state_.saw.phase[2] = phase_2;
  state_.saw.phase[3] = phase_3;

}

void DigitalOscillator::RenderFilteredNoise(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int32_t f = Interpolate824(lut_svf_cutoff, pitch_ << 17);
  int32_t damp = Interpolate824(lut_svf_damp, parameter_[0] << 17);
  int32_t scale = Interpolate824(lut_svf_scale, parameter_[0] << 17);
  int32_t bp = state_.svf.bp;
  int32_t lp = state_.svf.lp;
  int32_t bp_gain, lp_gain, hp_gain;
  
  // Morph between LP, BP, HP.
  if (parameter_[1] < 16384) {
    bp_gain = parameter_[1];
    lp_gain = 16384 - bp_gain;
    hp_gain = 0;
  } else {
    bp_gain = 32767 - parameter_[1];
    hp_gain = parameter_[1] - 16384;
    lp_gain = 0;
  }
  
  int32_t gain_correction = f > scale ? scale * 32767 / f : 32767;
  while (size--) {
    int32_t notch, hp, in;
    
    in = Random::GetSample() >> 1;
    notch = in - (bp * damp >> 15);
    lp += f * bp >> 15;
    CLIP(lp)
    hp = notch - lp;
    bp += f * hp >> 15;
    
    int32_t result = 0;
    result += (lp_gain * lp) >> 14;
    result += (bp_gain * bp) >> 14;
    result += (hp_gain * hp) >> 14;
    CLIP(result)
    result = result * gain_correction >> 15;
    *buffer++ = Interpolate88(ws_moderate_overdrive, result + 32768);
  }
  state_.svf.lp = lp;
  state_.svf.bp = bp;
}

void DigitalOscillator::RenderTwinPeaksNoise(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  int32_t sample;
  int32_t y10, y20;
  int32_t y11 = state_.pno.filter_state[0][0];
  int32_t y12 = state_.pno.filter_state[0][1];
  int32_t y21 = state_.pno.filter_state[1][0];
  int32_t y22 = state_.pno.filter_state[1][1];
  uint32_t q = 65240 + (parameter_[0] >> 7);
  int32_t q_squared = q * q >> 17;
  int16_t p1 = pitch_;

  CONSTRAIN(p1, 0, 16383)
  int32_t c1 = Interpolate824(lut_resonator_coefficient, p1 << 17);
  int32_t s1 = Interpolate824(lut_resonator_scale, p1 << 17);
  
  int16_t p2 = pitch_ + ((parameter_[1] - 16384) >> 1);
  CONSTRAIN(p2, 0, 16383)
  int32_t c2 = Interpolate824(lut_resonator_coefficient, p2 << 17);
  int32_t s2 = Interpolate824(lut_resonator_scale, p2 << 17);

  c1 = c1 * q >> 16;
  c2 = c2 * q >> 16;

  int32_t makeup_gain = 8191 - (parameter_[0] >> 2);
  
  while (size) {    
    sample = Random::GetSample() >> 1;
    
    if (sample > 0) {
      y10 = sample * s1 >> 16;
      y20 = sample * s2 >> 16;
    } else {
      y10 = -((-sample) * s1 >> 16);
      y20 = -((-sample) * s2 >> 16);
    }
    
    y10 += y11 * c1 >> 15;
    y10 -= y12 * q_squared >> 15;
    CLIP(y10)
    y12 = y11;
    y11 = y10;
    
    y20 += y21 * c2 >> 15;
    y20 -= y22 * q_squared >> 15;
    CLIP(y20)
    y22 = y21;
    y21 = y20;
    
    y10 += y20;
    y10 += (y10 * makeup_gain >> 13);
    CLIP(y10)
    sample = y10;
    sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
    
    *buffer++ = sample;
    *buffer++ = sample;
    size -= 2;
  }
  
  state_.pno.filter_state[0][0] = y11;
  state_.pno.filter_state[0][1] = y12;
  state_.pno.filter_state[1][0] = y21;
  state_.pno.filter_state[1][1] = y22;
}

void DigitalOscillator::RenderClockedNoise(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  ClockedNoiseState* state = &state_.clk;
  
  if ((parameter_[1] > previous_parameter_[1] + 64) ||
      (parameter_[1] < previous_parameter_[1] - 64)) {
    previous_parameter_[1] = parameter_[1];
  }
  if ((parameter_[0] > previous_parameter_[0] + 16) ||
      (parameter_[0] < previous_parameter_[0] - 16)) {
    previous_parameter_[0] = parameter_[0];
  }
  
  
  if (strike_) {
    state->seed = Random::GetWord();
    strike_ = false;
  }
  
  // Shift the range of the Coarse knob to reach higher clock rates, close
  // to the sample rate.
  uint32_t phase = phase_;
  uint32_t phase_increment = phase_increment_;
  for (size_t i = 0; i < 3; ++i) {
    if (phase_increment < (1UL << 31)) {
      phase_increment <<= 1;
    }
  }
  
  // Compute the period of the random generator.
  state->cycle_phase_increment = ComputePhaseIncrement(
      previous_parameter_[0] - 16384) << 1;
  
  // Compute the number of quantization steps
  uint32_t num_steps = 1 + (previous_parameter_[1] >> 10);
  if (num_steps == 1) {
    num_steps = 2;
  }
  uint32_t quantizer_divider = 65536 / num_steps;
  while (size--) {
    phase += phase_increment;
    if (sync != NULL && *sync++) {
      phase = 0;
    }
    
    // Clock.
    if (phase < phase_increment) {
      state->rng_state = state->rng_state * 1664525L + 1013904223L;
      state->cycle_phase += state->cycle_phase_increment;
      // Enforce period
      if (state->cycle_phase < state->cycle_phase_increment) {
        state->rng_state = state->seed;
        // Make the period an integer.
        state->cycle_phase = state->cycle_phase_increment;
      }
      uint16_t sample = state->rng_state;
      sample -= sample % quantizer_divider;
      sample += quantizer_divider >> 1;
      state->sample = sample;
      // Make the clock rate an exact divisor of the sample rate.
      phase = phase_increment;
    }
    *buffer++ = state->sample;
  }
  phase_ = phase;
}

void DigitalOscillator::RenderGranularCloud(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  
  for (size_t i = 0; i < 4; ++i) {
    Grain* g = &state_.grain[i];
    // If a grain has reached the end of its envelope, reset it.
    if (g->envelope_phase > (1 << 24) ||
        g->envelope_phase_increment == 0) {
      g->envelope_phase_increment = 0;
      if ((Random::GetWord() & 0xffff) < 0x4000) {
        g->envelope_phase_increment = \
            lut_granular_envelope_rate[parameter_[0] >> 7] << 3;
        g->envelope_phase = 0;
        g->phase_increment = phase_increment_;
        int32_t pitch_mod = Random::GetSample() * parameter_[1] >> 16;
        int32_t phi = phase_increment_ >> 8;
        if (pitch_mod < 0) {
          g->phase_increment += phi * (pitch_mod >> 8);
        } else {
          g->phase_increment += phi * (pitch_mod >> 7);
        }
      }
    }
  }
  
  // TODO(pichenettes): Check if it's possible to interpolate envelope
  // increment too!
  while (size--) {
    int32_t sample = 0;
    state_.grain[0].phase += state_.grain[0].phase_increment;
    state_.grain[0].envelope_phase += state_.grain[0].envelope_phase_increment;
    sample += Interpolate824(wav_sine, state_.grain[0].phase) * \
        lut_granular_envelope[state_.grain[0].envelope_phase >> 16] >> 17;

    state_.grain[1].phase += state_.grain[1].phase_increment;
    state_.grain[1].envelope_phase += state_.grain[1].envelope_phase_increment;
    sample += Interpolate824(wav_sine, state_.grain[1].phase) * \
        lut_granular_envelope[state_.grain[1].envelope_phase >> 16] >> 17;

    state_.grain[2].phase += state_.grain[2].phase_increment;
    state_.grain[2].envelope_phase += state_.grain[2].envelope_phase_increment;
    sample += Interpolate824(wav_sine, state_.grain[2].phase) * \
        lut_granular_envelope[state_.grain[2].envelope_phase >> 16] >> 17;

    state_.grain[3].phase += state_.grain[3].phase_increment;
    state_.grain[3].envelope_phase += state_.grain[3].envelope_phase_increment;
    sample += Interpolate824(wav_sine, state_.grain[3].phase) * \
        lut_granular_envelope[state_.grain[3].envelope_phase >> 16] >> 17;
    
    if (sample < -32768) {
      sample = -32768;
    }
    if (sample > 32767) {
      sample = 32767;
    }
    *buffer++ = sample;
  } 
}

static const uint16_t kParticleNoiseDecay = 64763;
static const int32_t kResonanceSquared = 32768 * 0.996 * 0.996;
static const int32_t kResonanceFactor = 32768 * 0.996;

void DigitalOscillator::RenderParticleNoise(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  uint16_t amplitude = state_.pno.amplitude;
  uint32_t density = 1024 + parameter_[0];
  int32_t sample;
  
  int32_t y10, y20, y30;
  int32_t y11 = state_.pno.filter_state[0][0];
  int32_t y12 = state_.pno.filter_state[0][1];
  int32_t s1 = state_.pno.filter_scale[0];
  int32_t c1 = state_.pno.filter_coefficient[0];
  int32_t y21 = state_.pno.filter_state[1][0];
  int32_t y22 = state_.pno.filter_state[1][1];
  int32_t s2 = state_.pno.filter_scale[1];
  int32_t c2 = state_.pno.filter_coefficient[1];
  int32_t y31 = state_.pno.filter_state[2][0];
  int32_t y32 = state_.pno.filter_state[2][1];
  int32_t s3 = state_.pno.filter_scale[2];
  int32_t c3 = state_.pno.filter_coefficient[2];

  while (size) {
    uint32_t noise = Random::GetWord();
    if ((noise & 0x7fffff) < density) {
      amplitude = 65535;
      int16_t noise_a = (noise & 0x0fff) - 0x800;
      int16_t noise_b = ((noise >> 15) & 0x1fff) - 0x1000;
      int16_t p1 = pitch_ + (3 * noise_a * parameter_[1] >> 17) + 0x600;

      CONSTRAIN(p1, 0, 16383)
      c1 = Interpolate824(lut_resonator_coefficient, p1 << 17);
      s1 = Interpolate824(lut_resonator_scale, p1 << 17);

      int16_t p2 = pitch_ + (noise_a * parameter_[1] >> 15) + 0x980;
      CONSTRAIN(p2, 0, 16383)
      c2 = Interpolate824(lut_resonator_coefficient, p2 << 17);
      s2 = Interpolate824(lut_resonator_scale, p2 << 17);

      int16_t p3 = pitch_ + (noise_b * parameter_[1] >> 16) + 0x790;
      CONSTRAIN(p3, 0, 16383)
      c3 = Interpolate824(lut_resonator_coefficient, p3 << 17);
      s3 = Interpolate824(lut_resonator_scale, p3 << 17);
      
      c1 = c1 * kResonanceFactor >> 15;
      c2 = c2 * kResonanceFactor >> 15;
      c3 = c3 * kResonanceFactor >> 15;
    }
    sample = (static_cast<int16_t>(noise) * amplitude) >> 16;
    amplitude = (amplitude * kParticleNoiseDecay) >> 16;
    
    if (sample > 0) {
      y10 = sample * s1 >> 16;
      y20 = sample * s2 >> 16;
      y30 = sample * s3 >> 16;
    } else {
      y10 = -((-sample) * s1 >> 16);
      y20 = -((-sample) * s2 >> 16);
      y30 = -((-sample) * s3 >> 16);
    }
    
    y10 += y11 * c1 >> 15;
    y10 -= y12 * kResonanceSquared >> 15;
    CLIP(y10);
    y12 = y11;
    y11 = y10;
    
    y20 += y21 * c2 >> 15;
    y20 -= y22 * kResonanceSquared >> 15;
    CLIP(y20);
    y22 = y21;
    y21 = y20;
    
    y30 += y31 * c3 >> 15;
    y30 -= y32 * kResonanceSquared >> 15;
    CLIP(y30);
    y32 = y31;
    y31 = y30;
    
    y10 += y20 + y30;
    CLIP(y10)
    *buffer++ = y10;
    *buffer++ = y10;
    size -= 2;
  }
  
  state_.pno.amplitude = amplitude;
  state_.pno.filter_state[0][0] = y11;
  state_.pno.filter_state[0][1] = y12;
  state_.pno.filter_scale[0] = s1;
  state_.pno.filter_coefficient[0] = c1;
  state_.pno.filter_state[1][0] = y21;
  state_.pno.filter_state[1][1] = y22;
  state_.pno.filter_scale[1] = s2;
  state_.pno.filter_coefficient[1] = c2;
  state_.pno.filter_state[2][0] = y31;
  state_.pno.filter_state[2][1] = y32;
  state_.pno.filter_scale[2] = s3;
  state_.pno.filter_coefficient[2] = c3;
}

static const int32_t kConstellationQ[] = { 23100, -23100, -23100, 23100 };
static const int32_t kConstellationI[] = { 23100, 23100, -23100, -23100 };

void DigitalOscillator::RenderDigitalModulation(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  uint32_t phase = phase_;
  uint32_t increment = phase_increment_;
  
  uint32_t symbol_stream_phase = state_.dmd.symbol_phase;
  uint32_t symbol_stream_phase_increment = ComputePhaseIncrement(
      pitch_ - 1536 + ((parameter_[0] - 32767) >> 3));
  uint8_t data_byte = state_.dmd.data_byte;
  
  if (strike_) {
    state_.dmd.symbol_count = 0;
    strike_ = false;
  }
  
  while (size--) {
    phase += increment;
    symbol_stream_phase += symbol_stream_phase_increment;
    if (symbol_stream_phase < symbol_stream_phase_increment) {
      ++state_.dmd.symbol_count;
      if (!(state_.dmd.symbol_count & 3)) {
        if (state_.dmd.symbol_count >= (64 + 4 * 256)) {
          state_.dmd.symbol_count = 0;
        }
        if (state_.dmd.symbol_count < 32) {
          data_byte = 0x00;
        } else if (state_.dmd.symbol_count < 48) {
          data_byte = 0x99;
        } else if (state_.dmd.symbol_count < 64) {
          data_byte = 0xcc;
        } else {
          state_.dmd.filter_state = (state_.dmd.filter_state * 3 + \
              static_cast<int32_t>(parameter_[1])) >> 2;
          data_byte = state_.dmd.filter_state >> 7;
        }
      } else {
        data_byte >>= 2;
      }
    }
    int16_t i = Interpolate824(wav_sine, phase);
    int16_t q = Interpolate824(wav_sine, phase + (1 << 30));
    *buffer++ = (kConstellationQ[data_byte & 3] * q >> 15) + \
        (kConstellationI[data_byte & 3] * i >> 15);
  }
  phase_ = phase;
  state_.dmd.symbol_phase = symbol_stream_phase;
  state_.dmd.data_byte = data_byte;
}

void DigitalOscillator::RenderQuestionMark(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  ClockedNoiseState* state = &state_.clk;
  
  if (strike_) {
    state->rng_state = 0;
    state->cycle_phase = 0;
    state->sample = 10;
    state->cycle_phase_increment = -1;
    state->seed = 32767;
    strike_ = false;
  }
  
  uint32_t phase = phase_;
  uint32_t increment = phase_increment_;
  uint32_t dit_duration = 3600 + ((32767 - parameter_[0]) >> 2);
  int32_t noise_threshold = 1024 + (parameter_[1] >> 3);
  while (size--) {
    phase += increment;
    int32_t sample;
    if (state->rng_state) {
      sample = (Interpolate824(wav_sine, phase) * 3) >> 2;
    } else {
      sample = 0;
    }
    if (++state->cycle_phase > dit_duration) {
      --state->sample;
      if (state->sample == 0) {
        ++state->cycle_phase_increment;
        state->rng_state = !state->rng_state;

        size_t address = state->cycle_phase_increment >> 2;
        size_t shift = (state->cycle_phase_increment & 0x3) << 1;
        state->sample = (2 << ((wt_code[address] >> shift) & 3)) - 1;
        if (state->sample == 15) {
          state->sample = 100;
          state->rng_state = 0;
          state->cycle_phase_increment = - 1;
        }
        phase = 1L << 30;
      }
      state->cycle_phase = 0;
    }
    state->seed += Random::GetSample() >> 2;
    int32_t noise_intensity = state->seed >> 8;
    if (noise_intensity < 0) {
      noise_intensity = -noise_intensity;
    }
    if (noise_intensity < noise_threshold) {
      noise_intensity = noise_threshold;
    }
    if (noise_intensity > 16000) {
      noise_intensity = 16000;
    }
    int32_t noise = (Random::GetSample() * noise_intensity >> 15);
    noise = noise * wav_sine[(phase >> 22) & 0xff] >> 15;
    sample += noise;
    CLIP(sample);
    int32_t distorted = sample * sample >> 14;
    sample += distorted * parameter_[1] >> 15;
    CLIP(sample);
    *buffer++ = sample;
  }
  phase_ = phase;
}

/*
void DigitalOscillator::RenderYourAlgo(
    const uint8_t* sync,
    int16_t* buffer,
    size_t size) {
  while (size--) {
    *buffer++ = 0;
  }
}
*/

}  // namespace braids
