#pragma once

#include <stdint.h>

namespace EmbeddedPort {

struct HarmonicProfile {
    float h2;
    float h3;
    float h4;
    float h5;
    float h6;
    float h7;
    float evenOddRatio;
};

struct TransformerProfile {
    bool hasTransformer;
    float saturationThreshold;
    float saturationAmount;
    float lowFreqSaturation;
    float highFreqRolloff;
    float dcBlockingFreq;
    HarmonicProfile harmonics;
};

struct HardwareUnitProfile {
    TransformerProfile inputTransformer;
};

class WaveshaperCurvesCore {
public:
    static const int TABLE_SIZE = 4096;
    static const float TABLE_RANGE;

    WaveshaperCurvesCore();
    float processTransformer(float input) const;

private:
    float transformerCurve[TABLE_SIZE];

    static float clampFloat(float value, float lo, float hi);
    static float indexToInput(int index);
    void initializeTransformerCurve();
};

class TransformerEmulationCore {
public:
    TransformerEmulationCore();
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setProfile(const TransformerProfile& newProfile);
    void setEnabled(bool shouldBeEnabled);
    float processSample(float input, int channel);

private:
    TransformerProfile profile;
    double sampleRate;
    bool enabled;

    float dcBlockerCoeff;
    float dcBlockerX1[2];
    float dcBlockerY1[2];

    float hfRolloffCoeff;
    bool hfRolloffEnabled;
    float hfFilterState[2];

    float lastSample[2];
    float hfEstimate[2];

    WaveshaperCurvesCore waveshaper;

    static float clampFloat(float value, float lo, float hi);
    static int clampInt(int value, int lo, int hi);
    void updateDCBlocker(float cutoffFreq);
    void updateHFRolloff(float cutoffFreq);
    float estimateHighFrequencyContent(float input, int channel);
    float applyTransformerSaturation(float input);
    float addHarmonics(float input, const HarmonicProfile& harmonics);
    float applyHFRolloff(float input, int channel);
    float processDCBlocker(float input, int channel);
};

class BusCompressorCore {
public:
    static const int MAX_CHANNELS = 2;
    static constexpr float OUTPUT_HARD_LIMIT = 2.0f;
    static constexpr float EPSILON = 0.0001f;
    static constexpr float BUS_MAX_REDUCTION_DB = 20.0f;

    struct Detector {
        float envelope;
        float rms;
        float previousLevel;
        float hpState;
        float prevInput;
        float previousGR;
    };

    BusCompressorCore();
    void prepare(double sampleRate, int numChannels, int blockSize);
    float process(float input, int channel, float threshold, float ratio,
                  int attackIndex, int releaseIndex, float makeupGain,
                  float mixAmount, bool oversample, float sidechainSignal,
                  bool useExternalSidechain);

    float getGainReduction(int channel) const;

private:
    int numChannels;
    double sampleRate;
    Detector detectors[MAX_CHANNELS];

    TransformerEmulationCore inputTransformer;

    static float clampFloat(float value, float lo, float hi);
    static int clampInt(int value, int lo, int hi);
    static float absFloat(float x);
    static float decibelsToGain(float dB);
    static float gainToDecibels(float gain);
};

HardwareUnitProfile getConsoleBusProfile();

} // namespace EmbeddedPort
