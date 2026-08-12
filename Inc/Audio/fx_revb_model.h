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
        typedef FxEngine<32768, FORMAT_32_BIT> E;
        typedef E::Reserve<150,
                E::Reserve<214,
                        E::Reserve < 319,
                        E::Reserve < 527,
                        E::Reserve < 2182,
                        E::Reserve < 2690,
                        E::Reserve < 4501,
                        E::Reserve < 2525,
                        E::Reserve < 2197,
                        E::Reserve < 6312> > > > > > > > > > DelugeMemory;
        typedef E::Reserve<163,
                E::Reserve<233,
                        E::Reserve < 347,
                        E::Reserve < 574,
                        E::Reserve < 2375,
                        E::Reserve < 2928,
                        E::Reserve < 4899,
                        E::Reserve < 2748,
                        E::Reserve < 2391,
                        E::Reserve < 6870> > > > > > > > > > TbdMemory;

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

        void Process(float *left, float *right, size_t size) {
            // Griesinger topology
            // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
            // Modulation is applied to the two long delays for a slow shimmer/chorus effect.
            typedef E::Reserve<150,
                    E::Reserve<214,
                            E::Reserve < 319,
                            E::Reserve < 527,
                            E::Reserve < 2182,
                            E::Reserve < 2690,
                            E::Reserve < 4501,
                            E::Reserve < 2525,
                            E::Reserve < 2197,
                            E::Reserve < 6312> > > > > > > > > > Memory;
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
                c.Interpolate(del2, 6261.0f, LFO_2, 50.0f, krt);
                c.Lp(lp_1, klp);
                c.Read(dap1a TAIL, -kap);
                c.WriteAllPass(dap1a, kap);
                c.Read(dap1b TAIL, kap);
                c.WriteAllPass(dap1b, -kap);
                c.Write(del1, 2.0f);
                c.Write(wet, 0.0f);

                *left += (wet - *left) * amount;

                c.Load(apout);
                c.Interpolate(del1, 4460.0f, LFO_1, 40.0f, krt);
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

        void Process(const float* in, float *left, float *right, size_t size) {
            if (tbd_delays_) {
                ProcessMonoTbd(in, left, right, size);
            } else {
                ProcessMonoDeluge(in, left, right, size);
            }
        }

        ITCM_AUDIT_32_TEXT void ProcessStereoWetAdd(const float *in_l,
                                                     const float *in_r,
                                                     float *out_l,
                                                     float *out_r,
                                                     const float *wet,
                                                     size_t size);

        void ProcessStereoWet(const float *in_l,
                              const float *in_r,
                              float *out_l,
                              float *out_r,
                              const float *wet,
                              size_t size) {
            StereoWetInput input = {in_l, in_r, wet};
            BufferOutput output = {out_l, out_r};
            if (tbd_delays_) {
                ProcessCore<TbdMemory>(input, output, size,
                                       6815.2383f, 54.42177f,
                                       4854.4219f, 43.53742f);
            } else {
                ProcessCore<DelugeMemory>(input, output, size,
                                          6261.0f, 50.0f,
                                          4460.0f, 40.0f);
            }
        }

        void ProcessMonoWet(const float *in,
                            float *out_l,
                            float *out_r,
                            const float *wet,
                            size_t size) {
            MonoWetInput input = {in, wet};
            BufferOutput output = {out_l, out_r};
            if (tbd_delays_) {
                ProcessCore<TbdMemory>(input, output, size,
                                       6815.2383f, 54.42177f,
                                       4854.4219f, 43.53742f);
            } else {
                ProcessCore<DelugeMemory>(input, output, size,
                                          6261.0f, 50.0f,
                                          4460.0f, 40.0f);
            }
        }

        inline void set_delay_mode(bool tbd) {
            if (tbd_delays_ != tbd) {
                tbd_delays_ = tbd;
                Clear();
            }
        }

        inline void set_amount(float amount) {
            amount_ = amount;
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

        inline void Clear() {
            engine_.Clear();
            lp_decay_1_ = 0.0f;
            lp_decay_2_ = 0.0f;
            hp_l_ = 0.0f;
            hp_r_ = 0.0f;
            lp_l_ = 0.0f;
            lp_r_ = 0.0f;
        }

    private:
        struct MonoInput {
            const float *in;

            __attribute__((always_inline)) inline float Next() {
                return *in++;
            }
        };

        struct StereoWetInput {
            const float *left;
            const float *right;
            const float *wet;

            __attribute__((always_inline)) inline float Next() {
                const float mono = 0.5f * (*left++ + *right++);
                return mono * *wet;
            }
        };

        struct MonoWetInput {
            const float *in;
            const float *wet;

            __attribute__((always_inline)) inline float Next() {
                return *in++ * *wet;
            }
        };

        struct BufferOutput {
            float *left;
            float *right;

            __attribute__((always_inline)) inline void Store(float left_value,
                                                              float right_value) {
                *left++ = left_value;
                *right++ = right_value;
            }
        };

        struct AddOutput {
            float *left;
            float *right;

            __attribute__((always_inline)) inline void Store(float left_value,
                                                              float right_value) {
                *left++ += left_value;
                *right++ += right_value;
            }
        };

        void ProcessMonoTbd(const float* in, float *left, float *right, size_t size) {
            MonoInput input = {in};
            BufferOutput output = {left, right};
            ProcessCore<TbdMemory>(input, output, size,
                                   6815.2383f, 54.42177f,
                                   4854.4219f, 43.53742f);
        }

        void ProcessMonoDeluge(const float* in, float *left, float *right, size_t size) {
            MonoInput input = {in};
            BufferOutput output = {left, right};
            ProcessCore<DelugeMemory>(input, output, size,
                                      6261.0f, 50.0f,
                                      4460.0f, 40.0f);
        }

        template<typename Memory, typename Input, typename Output>
        __attribute__((always_inline)) inline
        void ProcessCore(Input& input, Output& output, size_t size,
                         float del2_offset, float del2_modulation,
                         float del1_offset, float del1_modulation) {
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
            float lp_1 = lp_decay_1_;
            float lp_2 = lp_decay_2_;
            float hp_l = hp_l_;
            float hp_r = hp_r_;
            float lp_l = lp_l_;
            float lp_r = lp_r_;
            const float hp_coefficient = hp_coefficient_;
            const float lp_coefficient = lp_coefficient_;
            E::BlockState block;
            engine_.BeginBlock(&block);

            while (size--) {
                float wet;
                float apout = 0.0f;
                engine_.Start(&c, &block);

                c.Read(input.Next());
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
                c.Interpolate(del2, del2_offset, LFO_2, del2_modulation, krt);
                c.Lp(lp_1, klp);
                c.Read(dap1a TAIL, -kap);
                c.WriteAllPass(dap1a, kap);
                c.Read(dap1b TAIL, kap);
                c.WriteAllPass(dap1b, -kap);
                c.Write(del1, 2.0f);
                c.Write(wet, 0.0f);

                wet -= one_pole(hp_r, wet, hp_coefficient);
                const float right_value = one_pole(lp_r, wet, lp_coefficient);

                c.Load(apout);
                c.Interpolate(del1, del1_offset, LFO_1, del1_modulation, krt);
                c.Lp(lp_2, klp);
                c.Read(dap2a TAIL, -kap);
                c.WriteAllPass(dap2a, kap);
                c.Read(dap2b TAIL, kap);
                c.WriteAllPass(dap2b, -kap);
                c.Write(del2, 2.0f);
                c.Write(wet, 0.0f);

                wet -= one_pole(hp_l, wet, hp_coefficient);
                const float left_value = one_pole(lp_l, wet, lp_coefficient);

                output.Store(left_value, right_value);
            }

            engine_.EndBlock(&block);
            lp_decay_1_ = lp_1;
            lp_decay_2_ = lp_2;
            hp_l_ = hp_l;
            hp_r_ = hp_r;
            lp_l_ = lp_l;
            lp_r_ = lp_r;
        }
        E engine_;
        bool tbd_delays_ = false;

        float amount_ = 0.f;
        float input_gain_ = 0.2f;
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
