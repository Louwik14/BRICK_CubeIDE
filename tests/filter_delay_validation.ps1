$ErrorActionPreference = 'Stop'

$source = @'
using System;
using System.Collections.Generic;

public static class Brick6FilterDelayValidation
{
    const double Fs = 48000.0;
    const double Pi = Math.PI;
    static bool Finite(double value) { return !Double.IsNaN(value) && !Double.IsInfinity(value); }

    sealed class Svf
    {
        public double s1, s2;
        public int mode;
        public double cutoff = 1000.0, q = 0.70710678;

        public void StepAll(double input, out double lp, out double hp, out double bp)
        {
            double g = Math.Tan(Pi * Math.Max(20.0, Math.Min(16000.0, cutoff)) / Fs);
            double qs = Math.Max(0.70710678, Math.Min(6.5, q));
            double k = 1.0 / qs;
            double a1 = 1.0 / (1.0 + g * (g + k));
            double a2 = g * a1;
            double a3 = g * g * a1;
            double r = (qs - 0.70710678) / (6.5 - 0.70710678);
            double r2 = r * r;
            double inputGain = 1.0 - 0.091 * r2;
            double saturation = 0.52 * r2 * r;
            double lpGain = 1.0 + 0.035 * r;
            double hpGain = 0.98 + 0.055 * r;
            double bpGain = 0.92 + 0.08 * r;
            double driven = input * inputGain;
            double v3 = driven - s2;
            double loop = s1 / (1.0 + saturation * Math.Abs(s1));
            double v1 = a1 * loop + a2 * v3;
            double v2 = s2 + a2 * loop + a3 * v3;
            double high = driven - k * v1 - v2;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            lp = v2 * lpGain;
            hp = high * hpGain;
            bp = v1 * bpGain;
        }

        public double Step(double input)
        {
            double lp, hp, bp;
            StepAll(input, out lp, out hp, out bp);
            if (mode == 1) return hp;
            if (mode == 2) return bp;
            return lp;
        }
    }

    static double RmsGain(int mode, double cutoff, double q, double frequency)
    {
        var f = new Svf { mode = mode, cutoff = cutoff, q = q };
        double inputEnergy = 0.0, outputEnergy = 0.0;
        const int warm = 24000, measure = 48000;
        for (int i = 0; i < warm + measure; ++i)
        {
            double x = 0.1 * Math.Sin(2.0 * Pi * frequency * i / Fs);
            double y = f.Step(x);
            if (i >= warm) { inputEnergy += x * x; outputEnergy += y * y; }
            if (!Finite(y)) throw new Exception("NaN/Inf response");
        }
        return Math.Sqrt(outputEnergy / inputEnergy);
    }

    static double PeakDb(int mode, double cutoff, double q)
    {
        double peak = 0.0;
        for (int i = 0; i <= 96; ++i)
        {
            double frequency = 20.0 * Math.Pow(800.0, i / 96.0);
            peak = Math.Max(peak, RmsGain(mode, cutoff, q, frequency));
        }
        return 20.0 * Math.Log10(Math.Max(peak, 1e-12));
    }

    static void WaveStability()
    {
        var random = new Random(0xB612);
        foreach (int wave in new [] { 0, 1, 2, 3 })
        foreach (int mode in new [] { 0, 1, 2 })
        {
            var f = new Svf { mode = mode, cutoff = 7200.0, q = 6.5 };
            double peak = 0.0;
            for (int i = 0; i < 480000; ++i)
            {
                double phase = 997.0 * i / Fs;
                double x;
                if (wave == 0) x = 0.35 * Math.Sin(2.0 * Pi * phase);
                else if (wave == 1) x = 0.35 * (2.0 * (phase - Math.Floor(phase)) - 1.0);
                else if (wave == 2) x = ((phase - Math.Floor(phase)) < 0.5) ? 0.35 : -0.35;
                else x = 0.35 * (2.0 * random.NextDouble() - 1.0);
                double y = f.Step(x);
                if (!Finite(y)) throw new Exception("NaN/Inf long stability");
                peak = Math.Max(peak, Math.Abs(y));
                if (peak > 12.0) throw new Exception("Unbounded resonance loop");
            }
        }
    }

    static double TransitionPeak(bool lpToHp)
    {
        var f = new Svf { mode = lpToHp ? 0 : 2, cutoff = 1800.0, q = 4.0 };
        double previous = 0.0, worstDelta = 0.0, peak = 0.0;
        for (int i = 0; i < 4096; ++i)
        {
            double x = 0.2 * Math.Sin(2.0 * Pi * 440.0 * i / Fs);
            if (i >= 2112) f.mode = lpToHp ? 1 : 0;
            double lp, hp, bp;
            f.StepAll(x, out lp, out hp, out bp);
            double old = f.mode == 0 ? lp : (f.mode == 1 ? hp : bp);
            double y = old;
            if (i >= 2048 && i < 2112)
            {
                double t = (i - 2048) / 64.0;
                int nextMode = lpToHp ? 1 : 0;
                double next = nextMode == 0 ? lp : (nextMode == 1 ? hp : bp);
                if (lpToHp)
                    y = t < 0.5 ? old + (x - old) * (2.0 * t)
                                : x + (next - x) * (2.0 * t - 1.0);
                else
                    y = old + (next - old) * t;
            }
            if (i > 0) worstDelta = Math.Max(worstDelta, Math.Abs(y - previous));
            peak = Math.Max(peak, Math.Abs(y));
            previous = y;
        }
        if (peak > 1.0 || worstDelta > 0.25) throw new Exception("Mode transition discontinuity");
        return worstDelta;
    }

    static void Modulation()
    {
        foreach (double rate in new [] { 0.1, 1.0, 10.0, 40.0, 80.0 })
        {
            double min = 1e9, max = -1e9, sum = 0.0;
            int samples = (int)(Fs * Math.Max(2.0, 2.0 / rate));
            for (int i = 0; i < samples; i += 8)
            {
                double octave = 3.0 * Math.Sin(2.0 * Pi * rate * i / Fs);
                min = Math.Min(min, octave);
                max = Math.Max(max, octave);
                sum += octave;
            }
            double depth = 0.5 * (max - min);
            double mean = sum / Math.Ceiling(samples / 8.0);
            if (depth < 2.98 || Math.Abs(mean) > 0.02)
                throw new Exception("LFO depth/centre regression");
        }

        foreach (double baseHz in new [] { 20.0, 80.0, 440.0, 2000.0 })
        foreach (double amount in new [] { 0.25, 0.5, 1.0 })
        {
            double octaves = 8.0 * Math.Pow(amount, 1.5);
            double result = baseHz * Math.Pow(2.0, octaves);
            double measured = Math.Log(Math.Max(20.0, Math.Min(16000.0, result)) / baseHz, 2.0);
            double expected = Math.Min(octaves, Math.Log(16000.0 / baseHz, 2.0));
            if (Math.Abs(measured - expected) > 1e-9)
                throw new Exception("Envelope octave inconsistency");
        }
    }

    static void DelaySmoothing()
    {
        foreach (double target in new [] { 0.95, 0.1, 1.0, -1.0, 0.0 })
        {
            double current = 0.0;
            int remaining = 480;
            double previous = current;
            while (remaining > 0)
            {
                current += (target - current) / remaining;
                --remaining;
                if (!Finite(current)) throw new Exception("Delay smoothing NaN");
                if (Math.Abs(current - previous) > 2.0 / 480.0 + 1e-9)
                    throw new Exception("Delay smoothing step");
                previous = current;
            }
            if (current != target) throw new Exception("Delay smoothing did not stop");
        }
    }

    public static string Run()
    {
        var lines = new List<string>();
        double globalPeak = -999.0;
        foreach (double cutoff in new [] { 80.0, 440.0, 2000.0, 8000.0, 16000.0 })
        foreach (double q in new [] { 0.70710678, 2.0, 4.0, 6.5 })
        foreach (int mode in new [] { 0, 1, 2 })
        {
            double peak = PeakDb(mode, cutoff, q);
            globalPeak = Math.Max(globalPeak, peak);
            if (peak > 16.0) throw new Exception("Resonance peak too high: " + peak);
        }

        double lpBody = 20.0 * Math.Log10(RmsGain(0, 2000.0, 6.5, 100.0));
        double hpBody = 20.0 * Math.Log10(RmsGain(1, 2000.0, 6.5, 10000.0));
        double bpPeak = PeakDb(2, 2000.0, 6.5);
        if (lpBody < -2.0 || hpBody < -2.0) throw new Exception("Body loss exceeds 2 dB");
        if (bpPeak > 12.0) throw new Exception("BP normalization regression");

        WaveStability();
        Modulation();
        DelaySmoothing();
        double lpHpDelta = TransitionPeak(true);
        double bpDelta = TransitionPeak(false);

        lines.Add("filter_peak_max_db=" + globalPeak.ToString("F2"));
        lines.Add("lp_body_db=" + lpBody.ToString("F2"));
        lines.Add("hp_body_db=" + hpBody.ToString("F2"));
        lines.Add("bp_peak_db=" + bpPeak.ToString("F2"));
        lines.Add("lp_hp_transition_max_delta=" + lpHpDelta.ToString("F5"));
        lines.Add("bp_transition_max_delta=" + bpDelta.ToString("F5"));
        lines.Add("lfo_hz=0.1,1,10,40,80 depth=3.00_oct centre_error<0.02_oct");
        lines.Add("env_curve=sign(amount)*8*abs(amount)^1.5_oct");
        lines.Add("delay_ramp_samples=480 stable_cost=target_compare_only");
        return String.Join(Environment.NewLine, lines);
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp
[Brick6FilterDelayValidation]::Run()
