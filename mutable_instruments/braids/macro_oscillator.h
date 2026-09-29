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
// Macro-oscillator entry point.

#ifndef BRAIDS_MACRO_OSCILLATOR_H_
#define BRAIDS_MACRO_OSCILLATOR_H_

#include "stmlib/stmlib.h"

#include <cstring>

#include "braids/analog_oscillator.h"
#include "braids/digital_oscillator.h"
#include "braids/resources.h"
#include "braids/macro_oscillator_shape.h"

namespace braids {

struct MacroOscillatorScratch {
  uint8_t sync_buffer[24];
  int16_t temp_buffer[24];
};
  
class MacroOscillator {
 public:
  typedef void (MacroOscillator::*RenderFn)(const uint8_t*, int16_t*, size_t);

  MacroOscillator() { }
  ~MacroOscillator() { }
  
  inline void Init() {
    analog_oscillator_[0].Init();
    analog_oscillator_[1].Init();
    analog_oscillator_[2].Init();
    digital_oscillator_.Init();
    lp_state_ = 0;
    previous_parameter_[0] = 0;
    previous_parameter_[1] = 0;
  }
  
  inline void set_shape(MacroOscillatorShape shape) {
    if (shape != shape_) {
      Strike();
    }
    shape_ = shape;
  }

  inline void set_pitch(int16_t pitch) {
    pitch_ = pitch;
  }

  inline int16_t pitch() const { return pitch_; }

  inline uint32_t carrier_phase() const {
    if ((shape_ == MACRO_OSC_SHAPE_SQUARE_SUB)
        || (shape_ == MACRO_OSC_SHAPE_SAW_SUB)) {
      return analog_oscillator_[1].phase();
    }
    return (shape_ <= MACRO_OSC_SHAPE_TRIPLE_SINE)
        ? analog_oscillator_[0].phase()
        : digital_oscillator_.phase();
  }

  inline uint32_t carrier_phase_increment() const {
    if ((shape_ == MACRO_OSC_SHAPE_SQUARE_SUB)
        || (shape_ == MACRO_OSC_SHAPE_SAW_SUB)) {
      return analog_oscillator_[1].phase_increment();
    }
    return (shape_ <= MACRO_OSC_SHAPE_TRIPLE_SINE)
        ? analog_oscillator_[0].phase_increment()
        : digital_oscillator_.phase_increment();
  }

  inline void AdvancePhase(uint64_t samples) {
    if (shape_ <= MACRO_OSC_SHAPE_TRIPLE_SINE) {
      if (shape_ == MACRO_OSC_SHAPE_SQUARE_SYNC
          || shape_ == MACRO_OSC_SHAPE_SAW_SYNC) {
        AnalogOscillator& master = analog_oscillator_[0];
        AnalogOscillator& slave = analog_oscillator_[1];
        const uint32_t increment = master.phase_increment();
        const uint32_t prior = master.phase();
        master.AdvancePhase(samples);
        if (increment != 0 && samples > (0xffffffffUL - prior) / increment) {
          const uint32_t slave_increment = slave.phase_increment();
          const uint32_t after_wrap = master.phase() / increment;
          const uint32_t wrap_phase = master.phase() - after_wrap * increment;
          const uint32_t sync_divisor = increment >> 7;
          const uint32_t reset_time = sync_divisor != 0
              ? (wrap_phase / sync_divisor) << 9 : 0;
          slave.set_phase(reset_time * (slave_increment >> 16)
              + after_wrap * slave_increment);
        } else {
          slave.AdvancePhase(samples);
        }
        return;
      }
      for (size_t i = 0; i < 3; ++i) {
        analog_oscillator_[i].AdvancePhase(samples);
      }
    } else {
      digital_oscillator_.AdvancePhase(samples);
    }
  }

  inline void ShiftPitch(int16_t pitch) {
    const int16_t delta = pitch - pitch_;
    pitch_ = pitch;
    if (shape_ <= MACRO_OSC_SHAPE_TRIPLE_SINE) {
      for (size_t i = 0; i < 3; ++i) {
        analog_oscillator_[i].ShiftPitch(delta);
      }
    } else {
      digital_oscillator_.ShiftPitch(delta);
    }
  }

  inline void set_parameters(
      int16_t parameter_1,
      int16_t parameter_2) {
    parameter_[0] = parameter_1;
    parameter_[1] = parameter_2;
  }
  
  inline void Strike() {
    digital_oscillator_.Strike();
  }
  
  void Render24(
      const uint8_t* sync_buffer,
      MacroOscillatorScratch* scratch);
  inline int16_t* output_buffer() { return output_buffer_; }
  inline const int16_t* output_buffer() const { return output_buffer_; }
  
 private:
  void RenderCSaw(const uint8_t*, int16_t*, size_t);
  void RenderMorph(const uint8_t*, int16_t*, size_t);
  void RenderSawSquare(const uint8_t*, int16_t*, size_t);
  void RenderSub(const uint8_t*, int16_t*, size_t);
  void RenderDualSync(const uint8_t*, int16_t*, size_t);
  void RenderSineTriangle(const uint8_t*, int16_t*, size_t);
  void RenderBuzz(const uint8_t*, int16_t*, size_t);
  void RenderDigital(const uint8_t*, int16_t*, size_t);
  void RenderSawComb(const uint8_t*, int16_t*, size_t);
  void RenderTriple(const uint8_t*, int16_t*, size_t);
  void ConfigureTriple(AnalogOscillatorShape shape);

  int16_t parameter_[2];
  int16_t previous_parameter_[2];
  int16_t pitch_;
  int16_t output_buffer_[24];
  MacroOscillatorScratch* scratch_;
  int32_t lp_state_;
  
  AnalogOscillator analog_oscillator_[3];
  DigitalOscillator digital_oscillator_;
  
  MacroOscillatorShape shape_;
  static RenderFn fn_table_[];
  
  DISALLOW_COPY_AND_ASSIGN(MacroOscillator);
};

}  // namespace braids

#endif // BRAIDS_MACRO_OSCILLATOR_H_
