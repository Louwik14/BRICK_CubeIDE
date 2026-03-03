#include "fx_BusCompressorCore.h"

#include <math.h>

namespace EmbeddedPort {

const float WaveshaperCurvesCore::TABLE_RANGE = 4.0f;

static HarmonicProfile makeHarmonic(float h2, float h3, float evenOdd, float h4 = 0.0f, float h5 = 0.0f, float h6 = 0.0f, float h7 = 0.0f)
{
    HarmonicProfile hp;
    hp.h2 = h2;
    hp.h3 = h3;
    hp.h4 = h4;
    hp.h5 = h5;
    hp.h6 = h6;
    hp.h7 = h7;
    hp.evenOddRatio = evenOdd;
    return hp;
}

HardwareUnitProfile getConsoleBusProfile()
{
    HardwareUnitProfile p;
    p.inputTransformer.hasTransformer = true;
    p.inputTransformer.saturationThreshold = 0.9f;
    p.inputTransformer.saturationAmount = 0.03f;
    p.inputTransformer.lowFreqSaturation = 1.05f;
    p.inputTransformer.highFreqRolloff = 22000.0f;
    p.inputTransformer.dcBlockingFreq = 10.0f;
    p.inputTransformer.harmonics = makeHarmonic(0.002f, 0.004f, 0.4f);
    return p;
}

float WaveshaperCurvesCore::clampFloat(float value, float lo, float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

float WaveshaperCurvesCore::indexToInput(int index)
{
    return (((float)index / (TABLE_SIZE - 1)) * TABLE_RANGE) - (TABLE_RANGE / 2.0f);
}

WaveshaperCurvesCore::WaveshaperCurvesCore()
{
    initializeTransformerCurve();
}

float WaveshaperCurvesCore::processTransformer(float input) const
{
    float normalized = (input + TABLE_RANGE / 2.0f) / TABLE_RANGE;
    normalized = clampFloat(normalized, 0.0f, 0.9999f);
    float indexFloat = normalized * (TABLE_SIZE - 1);
    int index0 = (int)indexFloat;
    int index1 = index0 + 1;
    if (index1 >= TABLE_SIZE)
        index1 = TABLE_SIZE - 1;
    float frac = indexFloat - (float)index0;
    return transformerCurve[index0] * (1.0f - frac) + transformerCurve[index1] * frac;
}

void WaveshaperCurvesCore::initializeTransformerCurve()
{
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        float x = indexToInput(i);
        float absX = fabsf(x);
        float sign = (x >= 0.0f) ? 1.0f : -1.0f;

        if (absX < 0.7f)
        {
            float harmonic2 = x * absX * 0.05f;
            transformerCurve[i] = x + harmonic2;
        }
        else if (absX < 1.2f)
        {
            float excess = absX - 0.7f;
            float compressed = 0.7f + excess * (1.0f - excess * 0.25f);
            float harmonic2 = (sign * compressed) * compressed * 0.08f;
            transformerCurve[i] = sign * compressed + harmonic2;
        }
        else
        {
            float excess = absX - 1.2f;
            float hard = 1.05f + tanhf(excess * 1.5f) * 0.15f;
            transformerCurve[i] = sign * hard;
        }
    }
}

TransformerEmulationCore::TransformerEmulationCore()
    : sampleRate(44100.0), enabled(false), dcBlockerCoeff(0.999f), hfRolloffCoeff(0.99f), hfRolloffEnabled(true)
{
    reset();
    profile.hasTransformer = true;
    profile.dcBlockingFreq = 10.0f;
    profile.highFreqRolloff = 20000.0f;
    profile.lowFreqSaturation = 1.0f;
    profile.saturationAmount = 0.0f;
}

float TransformerEmulationCore::clampFloat(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int TransformerEmulationCore::clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void TransformerEmulationCore::prepare(double sr, int)
{
    sampleRate = sr;
    updateDCBlocker(profile.dcBlockingFreq);
    updateHFRolloff(profile.highFreqRolloff);
    reset();
}

void TransformerEmulationCore::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        dcBlockerX1[ch] = 0.0f;
        dcBlockerY1[ch] = 0.0f;
        hfFilterState[ch] = 0.0f;
        lastSample[ch] = 0.0f;
        hfEstimate[ch] = 0.0f;
    }
}

void TransformerEmulationCore::setProfile(const TransformerProfile& np)
{
    profile = np;
    enabled = profile.hasTransformer;
    updateDCBlocker(profile.dcBlockingFreq);
    updateHFRolloff(profile.highFreqRolloff);
}

void TransformerEmulationCore::setEnabled(bool b)
{
    enabled = b && profile.hasTransformer;
}

void TransformerEmulationCore::updateDCBlocker(float cutoff)
{
    float dc = cutoff > 0.5f ? cutoff : 0.5f;
    dcBlockerCoeff = 1.0f - (6.283185f * dc / (float)sampleRate);
}

void TransformerEmulationCore::updateHFRolloff(float cutoff)
{
    if (cutoff <= 0.0f)
    {
        hfRolloffEnabled = false;
        hfRolloffCoeff = 1.0f;
        return;
    }

    hfRolloffEnabled = true;
    float w = 6.283185f * cutoff / (float)sampleRate;
    hfRolloffCoeff = w / (w + 1.0f);
}

float TransformerEmulationCore::estimateHighFrequencyContent(float in, int ch)
{
    float diff = fabsf(in - lastSample[ch]);
    lastSample[ch] = in;
    hfEstimate[ch] = hfEstimate[ch] * 0.95f + diff * 0.05f;
    return clampFloat(hfEstimate[ch] * 3.0f, 0.0f, 1.0f);
}

float TransformerEmulationCore::applyTransformerSaturation(float in)
{
    return waveshaper.processTransformer(in);
}

float TransformerEmulationCore::addHarmonics(float in, const HarmonicProfile& h)
{
    if (h.h2 <= 0.0f && h.h3 <= 0.0f && h.h4 <= 0.0f)
        return in;

    float x = in;
    float x2 = x * x;
    float x3 = x2 * x;
    float out = x;
    out += h.h2 * x2;
    out += h.h3 * x3;
    if (h.h4 > 0.0f)
        out += h.h4 * x2 * x2;
    return out;
}

float TransformerEmulationCore::applyHFRolloff(float in, int ch)
{
    hfFilterState[ch] += hfRolloffCoeff * (in - hfFilterState[ch]);
    return hfFilterState[ch];
}

float TransformerEmulationCore::processDCBlocker(float in, int ch)
{
    float y = in - dcBlockerX1[ch] + dcBlockerCoeff * dcBlockerY1[ch];
    dcBlockerX1[ch] = in;
    dcBlockerY1[ch] = y;
    return y;
}

float TransformerEmulationCore::processSample(float input, int channel)
{
    if (!enabled)
        return input;

    channel = clampInt(channel, 0, 1);
    float hf = estimateHighFrequencyContent(input, channel);
    float lf = profile.lowFreqSaturation * (1.0f - hf * 0.5f);
    float driven = input * lf;
    float sat = applyTransformerSaturation(driven);
    float out = input + (sat - input) * profile.saturationAmount;
    out = addHarmonics(out, profile.harmonics);
    if (hfRolloffEnabled)
        out = applyHFRolloff(out, channel);
    out = processDCBlocker(out, channel);
    return out;
}

BusCompressorCore::BusCompressorCore() : numChannels(0), sampleRate(0.0)
{
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        detectors[i].envelope = 1.0f;
        detectors[i].rms = 0.0f;
        detectors[i].previousLevel = 0.0f;
        detectors[i].hpState = 0.0f;
        detectors[i].prevInput = 0.0f;
        detectors[i].previousGR = 0.0f;
    }
}

float BusCompressorCore::clampFloat(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int BusCompressorCore::clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float BusCompressorCore::absFloat(float x)
{
    return x < 0.0f ? -x : x;
}

float BusCompressorCore::decibelsToGain(float dB)
{
    return powf(10.0f, dB / 20.0f);
}

float BusCompressorCore::gainToDecibels(float g)
{
    if (g <= 0.0f)
        return -1000.0f;
    return 20.0f * log10f(g);
}

void BusCompressorCore::prepare(double sr, int ch, int)
{
    if (sr <= 0.0 || ch <= 0)
        return;

    sampleRate = sr;
    numChannels = ch > MAX_CHANNELS ? MAX_CHANNELS : ch;

    for (int c = 0; c < numChannels; ++c)
    {
        detectors[c].envelope = 1.0f;
        detectors[c].rms = 0.0f;
        detectors[c].previousLevel = 0.0f;
        detectors[c].hpState = 0.0f;
        detectors[c].prevInput = 0.0f;
        detectors[c].previousGR = 0.0f;
    }

    HardwareUnitProfile p = getConsoleBusProfile();
    inputTransformer.prepare(sr, numChannels);
    inputTransformer.setProfile(p.inputTransformer);
    inputTransformer.setEnabled(true);
}

float BusCompressorCore::process(float input, int channel, float threshold, float ratio,
                                 int attackIndex, int releaseIndex, float makeupGain,
                                 float mixAmount, float sidechainHpfHz, bool oversample, float sidechainSignal,
                                 bool useExternalSidechain)
{
    (void)oversample;

    if (channel >= numChannels || channel < 0)
        return input;
    if (sampleRate <= 0.0)
        return input;

    Detector& detector = detectors[channel];
    float transformedInput = inputTransformer.processSample(input, channel);

    float detectionLevel;
    if (useExternalSidechain)
    {
        detectionLevel = absFloat(sidechainSignal);
    }
    else
    {
        float hpfHz = clampFloat(sidechainHpfHz, 20.0f, 200.0f);
        float hpCutoff = hpfHz / (float)sampleRate;
        float hpAlpha = hpCutoff < 1.0f ? hpCutoff : 1.0f;
        detector.hpState = transformedInput - detector.prevInput + detector.hpState * (1.0f - hpAlpha);
        detector.prevInput = transformedInput;
        detectionLevel = absFloat(detector.hpState);
    }

    float thresholdLin = decibelsToGain(threshold);
    float reduction = 0.0f;
    if (detectionLevel > thresholdLin)
    {
        float overThreshDb = gainToDecibels(detectionLevel / thresholdLin);
        reduction = overThreshDb * (1.0f - 1.0f / ratio);
        if (reduction > BUS_MAX_REDUCTION_DB)
            reduction = BUS_MAX_REDUCTION_DB;
    }

    const float attackTimes[6] = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
    const float releaseTimes[5] = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f};

    float attackTime = attackTimes[clampInt(attackIndex, 0, 5)] * 0.001f;
    float releaseTime = releaseTimes[clampInt(releaseIndex, 0, 4)] * 0.001f;

    if (releaseTime < 0.0f)
    {
        float signalDelta = absFloat(detectionLevel - detector.previousLevel);
        detector.previousLevel = detector.previousLevel * 0.95f + detectionLevel * 0.05f;
        float transientDensity = clampFloat(signalDelta * 20.0f, 0.0f, 1.0f);
        float compressionFactor = clampFloat(reduction / 12.0f, 0.0f, 1.0f);
        float sustainedFactor = (1.0f - transientDensity) * compressionFactor;
        releaseTime = 0.15f + sustainedFactor * (0.45f - 0.15f);
    }

    float targetGain = decibelsToGain(-reduction);
    if (targetGain < detector.envelope)
    {
        float divisor = attackTime * (float)sampleRate;
        if (divisor < EPSILON)
            divisor = EPSILON;
        float attackCoeff = 1.0f - 1.0f / divisor;
        attackCoeff = clampFloat(attackCoeff, 0.0f, 0.9999f);
        detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;
    }
    else
    {
        float divisor = releaseTime * (float)sampleRate;
        if (divisor < EPSILON)
            divisor = EPSILON;
        float releaseCoeff = 1.0f - 1.0f / divisor;
        releaseCoeff = clampFloat(releaseCoeff, 0.0f, 0.9999f);
        detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;
    }

    float currentGR = 1.0f - detector.envelope;
    currentGR = 0.9f * currentGR + 0.1f * detector.previousGR;
    detector.previousGR = currentGR;
    detector.envelope = 1.0f - currentGR;

    if (!(detector.envelope == detector.envelope))
        detector.envelope = 1.0f;

    float compressed = transformedInput * detector.envelope;
    float processed = compressed;
    const float k2 = 0.004f;
    const float k3 = 0.003f;
    float x2 = processed * processed;
    float x3 = x2 * processed;
    processed = processed + k2 * x2 + k3 * x3;

    float dry = input;
    float wet = processed;
    float mixed = dry + (wet - dry) * clampFloat(mixAmount, 0.0f, 1.0f);

    float output = mixed * decibelsToGain(makeupGain);
    if (output < -OUTPUT_HARD_LIMIT)
        output = -OUTPUT_HARD_LIMIT;
    if (output > OUTPUT_HARD_LIMIT)
        output = OUTPUT_HARD_LIMIT;
    return output;
}

float BusCompressorCore::getGainReduction(int channel) const
{
    if (channel < 0 || channel >= numChannels)
        return 0.0f;
    return gainToDecibels(detectors[channel].envelope);
}

} // namespace EmbeddedPort
