// Copyright 2014 Emilie Gillet.
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
// Reverb.

#pragma once

#include "stmlib/stmlib.h"

#include "Storage/memory_layout.h"

#include "fx_revb_engine.h"

namespace mifx {

    class Reverb {
    public:
        static constexpr float kLongDelay2ModulationSamples = 54.42177f;
        static constexpr float kLongDelay1ModulationSamples = 43.53742f;

        Reverb() {}

        ~Reverb() {}

        void Init(float *buffer) {
            engine_.Init(buffer);
            engine_.SetLFOFrequency(LFO_1, 0.5f / 48000.0f);
            engine_.SetLFOFrequency(LFO_2, 0.3f / 48000.0f);
            lp_ = 0.7f;
            diffusion_ = 0.625f;
        }

        ITCM_TEXT_NAMED("mifx_reverb_process_stereo")
        void Process(float *left, float *right, size_t size) {
            // Griesinger topology
            // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
            // Modulation is applied to the two long delays for a slow shimmer/chorus effect.
            typedef E::Reserve<163,
                    E::Reserve<233,
                            E::Reserve < 347,
                            E::Reserve < 574,
                            E::Reserve < 2375,
                            E::Reserve < 2928,
                            E::Reserve < 4899,
                            E::Reserve < 2748,
                            E::Reserve < 2391,
                            E::Reserve < 6870> > > > > > > > > > Memory;
            E::DelayLine<Memory, 0> ap1;
            E::DelayLine<Memory, 1> ap2;
            E::DelayLine<Memory, 2> ap3;
            E::DelayLine<Memory, 3> ap4;
            E::DelayLine<Memory, 4> dap1a;
            E::DelayLine<Memory, 5> dap1b;
            E::DelayLine<Memory, 6> del1;
            E::DelayLine<Memory, 7> dap2a;
            E::DelayLine<Memory, 8> dap2b;
            E::DelayLine<Memory, 9> del2;
            E::Context c;

            const float kap = diffusion_;
            const float klp = lp_;
            const float krt = reverb_time_;
            const float amount = amount_;
            const float gain = input_gain_;

            float lp_1 = lp_decay_1_;
            float lp_2 = lp_decay_2_;

            while (size--) {
                float wet;
                float apout = 0.0f;
                engine_.Start(&c);

                c.Read(*left + *right, gain);

                // Diffuse through 4 allpasses.
                c.Read(ap1 TAIL, kap);
                c.WriteAllPass(ap1, -kap);
                c.Read(ap2 TAIL, kap);
                c.WriteAllPass(ap2, -kap);
                c.Read(ap3 TAIL, kap);
                c.WriteAllPass(ap3, -kap);
                c.Read(ap4 TAIL, kap);
                c.WriteAllPass(ap4, -kap);
                c.Write(apout);

                // Main reverb loop.
                c.Load(apout);
                c.Interpolate(del2, 6815.2383f, LFO_2, kLongDelay2ModulationSamples, krt);
                c.Lp(lp_1, klp);
                c.Read(dap1a TAIL, -kap);
                c.WriteAllPass(dap1a, kap);
                c.Read(dap1b TAIL, kap);
                c.WriteAllPass(dap1b, -kap);
                c.Write(del1, 2.0f);
                c.Write(wet, 0.0f);

                *left += (wet - *left) * amount;

                c.Load(apout);
                c.Interpolate(del1, 4854.4219f, LFO_1, kLongDelay1ModulationSamples, krt);
                c.Lp(lp_2, klp);
                c.Read(dap2a TAIL, kap);
                c.WriteAllPass(dap2a, -kap);
                c.Read(dap2b TAIL, -kap);
                c.WriteAllPass(dap2b, kap);
                c.Write(del2, 2.0f);
                c.Write(wet, 0.0f);

                *right += (wet - *right) * amount;

                ++left;
                ++right;
            }

            lp_decay_1_ = lp_1;
            lp_decay_2_ = lp_2;
        }

        ITCM_TEXT_NAMED("mifx_reverb_process_mono")
        void Process(const float* in, float *left, float *right, size_t size) {
            // Griesinger topology
            // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
            // Modulation is applied to the two long delays for a slow shimmer/chorus effect.
            typedef E::Reserve<163,
                    E::Reserve<233,
                            E::Reserve < 347,
                            E::Reserve < 574,
                            E::Reserve < 2375,
                            E::Reserve < 2928,
                            E::Reserve < 4899,
                            E::Reserve < 2748,
                            E::Reserve < 2391,
                            E::Reserve < 6870> > > > > > > > > > Memory;
            E::DelayLine<Memory, 0> ap1;
            E::DelayLine<Memory, 1> ap2;
            E::DelayLine<Memory, 2> ap3;
            E::DelayLine<Memory, 3> ap4;
            E::DelayLine<Memory, 4> dap1a;
            E::DelayLine<Memory, 5> dap1b;
            E::DelayLine<Memory, 6> del1;
            E::DelayLine<Memory, 7> dap2a;
            E::DelayLine<Memory, 8> dap2b;
            E::DelayLine<Memory, 9> del2;
            E::Context c;

            const float kap = diffusion_;
            const float klp = lp_;
            const float krt = reverb_time_;
            const float gain = input_gain_;

            float lp_1 = lp_decay_1_;
            float lp_2 = lp_decay_2_;

            while (size--) {
                float wet;
                float apout = 0.0f;
                engine_.Start(&c);

                c.Read(*in, gain);
                in++;
                // Diffuse through 4 allpasses.
                c.Read(ap1 TAIL, kap);
                c.WriteAllPass(ap1, -kap);
                c.Read(ap2 TAIL, kap);
                c.WriteAllPass(ap2, -kap);
                c.Read(ap3 TAIL, kap);
                c.WriteAllPass(ap3, -kap);
                c.Read(ap4 TAIL, kap);
                c.WriteAllPass(ap4, -kap);
                c.Write(apout);

                // Main reverb loop.
                c.Load(apout);
                c.Interpolate(del2, 6815.2383f, LFO_2, kLongDelay2ModulationSamples, krt);
                c.Lp(lp_1, klp);
                c.Read(dap1a TAIL, -kap);
                c.WriteAllPass(dap1a, kap);
                c.Read(dap1b TAIL, kap);
                c.WriteAllPass(dap1b, -kap);
                c.Write(del1, 2.0f);
                c.Write(wet, 0.0f);

                wet -= one_pole(hp_l_, wet, hp_coefficient_);
                *left = one_pole(lp_l_, wet, lp_coefficient_);

                c.Load(apout);
                c.Interpolate(del1, 4854.4219f, LFO_1, kLongDelay1ModulationSamples, krt);
                c.Lp(lp_2, klp);
                c.Read(dap2a TAIL, kap);
                c.WriteAllPass(dap2a, -kap);
                c.Read(dap2b TAIL, -kap);
                c.WriteAllPass(dap2b, kap);
                c.Write(del2, 2.0f);
                c.Write(wet, 0.0f);

                wet -= one_pole(hp_r_, wet, hp_coefficient_);
                *right = one_pole(lp_r_, wet, lp_coefficient_);

                ++left;
                ++right;
            }

            lp_decay_1_ = lp_1;
            lp_decay_2_ = lp_2;
        }

        inline void set_amount(float amount) {
            amount_ = amount;
        }

        inline void set_input_gain(float input_gain) {
            input_gain_ = input_gain;
        }

        inline void set_time(float reverb_time) {
            reverb_time_ = (reverb_time < 0.0f) ? 0.0f
                    : ((reverb_time > 0.98f) ? 0.98f : reverb_time);
        }

        inline void set_diffusion(float diffusion) {
            diffusion_ = (diffusion < 0.0f) ? 0.0f : ((diffusion > 0.9f) ? 0.9f : diffusion);
        }

        inline void set_lp(float lp) {
            lp_ = lp;
        }

        inline void set_output_filters(float hp, float lp) {
            hp_coefficient_ = hp;
            lp_coefficient_ = lp;
        }

        inline void Clear() {
            engine_.Clear();
            lp_decay_1_ = 0.0f;
            lp_decay_2_ = 0.0f;
            hp_l_ = 0.0f;
            hp_r_ = 0.0f;
            lp_l_ = 0.0f;
            lp_r_ = 0.0f;
        }

        void set_lfo1_freq(float f) {
            engine_.SetLFOFrequency(LFO_1, f / 48000.0f);
        }

        void set_lfo2_freq(float f) {
            engine_.SetLFOFrequency(LFO_2, f / 48000.0f);
        }

    private:
        typedef FxEngine<32768, FORMAT_32_BIT> E;
        E engine_;

        float amount_ = 0.f;
        float input_gain_ = 0.f;
        float reverb_time_ = 0.f;
        float diffusion_ = 0.f;
        float lp_ = 0.f;

        float lp_decay_1_ = 0.f;
        float lp_decay_2_ = 0.f;
        float hp_l_ = 0.f;
        float hp_r_ = 0.f;
        float lp_l_ = 0.f;
        float lp_r_ = 0.f;
        float hp_coefficient_ = 0.f;
        float lp_coefficient_ = 1.f;

        static inline float one_pole(float& state, float input, float coefficient) {
            state += coefficient * (input - state);
            return state;
        }

        DISALLOW_COPY_AND_ASSIGN(Reverb);
    };

}  // namespace
