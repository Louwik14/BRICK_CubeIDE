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
            // This is the Griesinger topology described in the Dattorro paper
            // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
            // Modulation is applied in the loop of the first diffuser AP for additional
            // smearing; and to the two long delays for a slow shimmer/chorus effect.
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

                // Smear AP1 inside the loop.
                if (smear_depth_ > 0.0f) {
                    c.Interpolate(ap1, 10.884354f, LFO_1, smear_depth_, 1.0f);
                    c.Write(ap1, 109, 0.0f);
                }

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
                c.Interpolate(del2, 6815.2383f, LFO_2, 54.42177f, krt);
                c.Lp(lp_1, klp);
                c.Read(dap1a TAIL, -kap);
                c.WriteAllPass(dap1a, kap);
                c.Read(dap1b TAIL, kap);
                c.WriteAllPass(dap1b, -kap);
                c.Write(del1, 2.0f);
                c.Write(wet, 0.0f);

                *left += (wet - *left) * amount;

                c.Load(apout);
                c.Interpolate(del1, 4854.4219f, LFO_1, 43.53742f, krt);
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
            // This is the Griesinger topology described in the Dattorro paper
            // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
            // Modulation is applied in the loop of the first diffuser AP for additional
            // smearing; and to the two long delays for a slow shimmer/chorus effect.
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

                // Smear AP1 inside the loop.
                if (smear_depth_ > 0.0f) {
                    c.Interpolate(ap1, 10.884354f, LFO_1, smear_depth_, 1.0f);
                    c.Write(ap1, 109, 0.0f);
                }

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
                c.Interpolate(del2, 6815.2383f, LFO_2, 54.42177f, krt);
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
                c.Interpolate(del1, 4854.4219f, LFO_1, 43.53742f, krt);
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

        ITCM_TEXT_NAMED("mifx_reverb_process_digital")
        void ProcessDigital(const float* in, float *left, float *right, size_t size) {
            typedef E::Reserve<88,
                    E::Reserve<66,
                            E::Reserve<234,
                                    E::Reserve<171,
                                            E::Reserve<425,
                                                    E::Reserve<2760,
                                                            E::Reserve<1116,
                                                                    E::Reserve<2306,
                                                                            E::Reserve<571,
                                                                                    E::Reserve<2614,
                                                                                            E::Reserve<1646,
                                                                                                    E::Reserve<1961> > > > > > > > > > > > Memory;
            E::DelayLine<Memory, 0> ap1;
            E::DelayLine<Memory, 1> ap2;
            E::DelayLine<Memory, 2> ap3;
            E::DelayLine<Memory, 3> ap4;
            E::DelayLine<Memory, 4> dap1a;
            E::DelayLine<Memory, 5> del1a;
            E::DelayLine<Memory, 6> dap1b;
            E::DelayLine<Memory, 7> del1b;
            E::DelayLine<Memory, 8> dap2a;
            E::DelayLine<Memory, 9> del2a;
            E::DelayLine<Memory, 10> dap2b;
            E::DelayLine<Memory, 11> del2b;
            E::Context c;

            const float kdecay = reverb_time_;
            const float kid1 = 0.750f;
            const float kid2 = 0.625f;
            const float kdd1 = 0.70f;
            const float kdd2 = clamp_local(kdecay + 0.15f, 0.25f, 0.5f);
            const float kdamp = lp_;
            float lp_1 = lp_decay_1_;
            float lp_2 = lp_decay_2_;
            float lp_band = lp_band_;

            while (size--) {
                engine_.Start(&c);
                c.Load(*in++);
                c.Lp(lp_band, 0.9995f);
                process_allpass(c, ap1, kid1);
                process_allpass(c, ap2, kid1);
                process_allpass(c, ap3, kid2);
                process_allpass(c, ap4, kid2);
                float apout = 0.0f;
                c.Write(apout);

                c.Load(apout);
                c.Interpolate(dap1a, 416.6540f, LFO_2, 9.0f, -kdd1);
                process_delay(c, del1a);
                c.Lp(lp_1, kdamp);
                multiply(c, kdecay);
                process_allpass(c, dap1b, kdd2);
                process_delay(c, del1b);
                multiply(c, kdecay);
                c.Read(apout);
                c.Write(dap2a, kdd2);

                c.Load(apout);
                c.Interpolate(dap2a, 562.9789f, LFO_1, 9.0f, -kdd1);
                process_delay(c, del2a);
                c.Lp(lp_2, kdamp);
                multiply(c, kdecay);
                process_allpass(c, dap2b, kdd2);
                process_delay(c, del2b);
                multiply(c, kdecay);
                c.Read(apout);
                c.Write(dap1a, kdd1);

                float wet_l =
                    0.6f * engine_.ReadSample(del2a, 164) +
                    0.6f * engine_.ReadSample(del2a, 1843) -
                    0.6f * engine_.ReadSample(dap2b, 1186) +
                    0.6f * engine_.ReadSample(del2b, 1237) -
                    0.6f * engine_.ReadSample(del1a, 1233) -
                    0.6f * engine_.ReadSample(dap1b, 115) -
                    0.6f * engine_.ReadSample(del1b, 660);
                float wet_r =
                    0.6f * engine_.ReadSample(del1a, 218) +
                    0.6f * engine_.ReadSample(del1a, 2248) -
                    0.6f * engine_.ReadSample(dap1b, 761) +
                    0.6f * engine_.ReadSample(del1b, 1657) -
                    0.6f * engine_.ReadSample(del2a, 1308) -
                    0.6f * engine_.ReadSample(dap2b, 207) -
                    0.6f * engine_.ReadSample(del2b, 75);

                wet_l -= one_pole(hp_l_, wet_l, hp_coefficient_);
                wet_r -= one_pole(hp_r_, wet_r, hp_coefficient_);
                wet_l = one_pole(lp_l_, wet_l, lp_coefficient_);
                wet_r = one_pole(lp_r_, wet_r, lp_coefficient_);
                *left++ = wet_l;
                *right++ = wet_r;
            }
            lp_decay_1_ = lp_1;
            lp_decay_2_ = lp_2;
            lp_band_ = lp_band;
        }

        inline void set_amount(float amount) {
            amount_ = amount;
        }

        inline void set_input_gain(float input_gain) {
            input_gain_ = input_gain;
        }

        inline void set_time(float reverb_time) {
            reverb_time_ = reverb_time;
        }

        inline void set_diffusion(float diffusion) {
            diffusion_ = diffusion;
        }

        inline void set_lp(float lp) {
            lp_ = lp;
        }

        inline void set_output_filters(float hp, float lp) {
            hp_coefficient_ = hp;
            lp_coefficient_ = lp;
        }

        inline void set_smear_depth(float depth) {
            smear_depth_ = depth;
        }

        inline void Clear() {
            engine_.Clear();
            lp_decay_1_ = 0.0f;
            lp_decay_2_ = 0.0f;
            hp_l_ = 0.0f;
            hp_r_ = 0.0f;
            lp_l_ = 0.0f;
            lp_r_ = 0.0f;
            lp_band_ = 0.0f;
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
        float smear_depth_ = 80.0f * (48000.0f / 44100.0f);
        float lp_band_ = 0.0f;

        static inline float one_pole(float& state, float input, float coefficient) {
            state += coefficient * (input - state);
            return state;
        }

        static inline float clamp_local(float v, float lo, float hi) {
            return (v < lo) ? lo : ((v > hi) ? hi : v);
        }

        template<typename D>
        inline void process_allpass(typename E::Context& c, D& d, float scale) {
            c.Read(d TAIL, scale);
            c.WriteAllPass(d, -scale);
        }

        template<typename D>
        inline void process_delay(typename E::Context& c, D& d) {
            float head = 0.0f;
            c.Write(head);
            const float tail = engine_.ReadSample(d, D::length);
            engine_.WriteSample(d, 0, head);
            c.Load(tail);
        }

        static inline void multiply(typename E::Context& c, float scale) {
            float value = 0.0f;
            c.Write(value);
            c.Load(value * scale);
        }

        DISALLOW_COPY_AND_ASSIGN(Reverb);
    };

}  // namespace
