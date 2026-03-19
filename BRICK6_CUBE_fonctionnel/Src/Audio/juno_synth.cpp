#include "Audio/juno_synth.h"
#include "Audio/audio_float.h"
#include "Audio/juno_midi_queue.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if __cplusplus < 201703L
namespace std {
template <typename T>
constexpr const T& clamp(const T& value, const T& low, const T& high)
{
    return (value < low) ? low : ((high < value) ? high : value);
}
} // namespace std
#endif

#include "../../Juno/KR106LFO.h"
#include "../../Juno/KR106VcfFreqJ6.h"
#include "../../Juno/KR106Voice.h"

namespace {

constexpr uint32_t JUNO_SYNTH_NUM_VOICES = 4U;
constexpr uint32_t JUNO_MIDI_EVENTS_PER_BLOCK = 16U;
constexpr float JUNO_PORTAMENTO = 0.22f;
constexpr float JUNO_BEND_RANGE_OCT = 2.0f / 12.0f;

struct JunoVoiceSlot
{
    uint8_t active;
    uint8_t note;
    uint8_t held;
    uint8_t sustained;
    uint32_t age;
};

struct JunoSynthState
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t test_mode;
    uint8_t unison_prepared;
    uint8_t sustain;
    float sample_rate;
    float bend;
    uint32_t block_size;
    uint32_t voice_age_counter;
    kr106::LFO lfo;
    kr106::Voice<float> voices[JUNO_SYNTH_NUM_VOICES];
    JunoVoiceSlot slots[JUNO_SYNTH_NUM_VOICES];
    float lfo_buffer[AUDIO_BLOCK_SIZE];
    float lfo_raw_buffer[AUDIO_BLOCK_SIZE];
};

static JunoSynthState g_juno;

static float midi_note_to_pitch(uint8_t midi_note)
{
    return (static_cast<float>(midi_note) - 69.0f) * (1.0f / 12.0f);
}

static void juno_voice_apply_patch(kr106::Voice<float>& voice, uint32_t voice_index)
{
    voice.InitVariance(static_cast<int>(voice_index));
    voice.mADSR.mJ6Mode = true;
    voice.mSawOn = true;
    voice.mPulseOn = true;
    voice.mSubOn = true;
    voice.mPwmMode = 0;
    voice.mVcaMode = 0;
    voice.mVcfEnvInvert = 1;

    voice.mDcoPwm = 0.38f;
    voice.mDcoSub = 0.32f;
    voice.mDcoNoise = 0.0f;
    voice.mDcoLfo = kr106::Voice<float>::dcoLfoDepth6(0.08f);

    voice.mVcfFreq = kr106::j6_vcf_freq_from_slider(0.58f);
    voice.mVcfRes = 0.18f;
    voice.mVcfEnv = 0.42f;
    voice.mVcfLfo = kr106::Voice<float>::vcfLfoDepth6(0.06f);
    voice.mVcfKbd = 0.55f;
    voice.mBendDco = JUNO_BEND_RANGE_OCT;
    voice.mBendVcf = 0.0f;
    voice.mBendLfo = 0.0f;
    voice.mRawBend = g_juno.bend;
    voice.mBenderModAmt = 0.0f;
    voice.mOctTranspose = 0.0f;

    voice.mPortaEnabled = true;
    voice.mPortaRateParam = JUNO_PORTAMENTO;
    voice.mADSR.SetAttackTau(0.010f);
    voice.mADSR.SetDecayTau(0.280f);
    voice.mADSR.SetSustain(0.68f);
    voice.mADSR.SetReleaseTau(0.360f);
    voice.SetSampleRateAndBlockSize(g_juno.sample_rate, static_cast<int>(g_juno.block_size));
}

static void juno_release_sustained_voices(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if((g_juno.slots[i].active != 0U) &&
           (g_juno.slots[i].held == 0U) &&
           (g_juno.slots[i].sustained != 0U))
        {
            g_juno.voices[i].Release();
            g_juno.slots[i].sustained = 0U;
        }
    }
}

static uint32_t juno_find_voice_for_note_on(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.voices[i].GetBusy() == false)
            return i;
    }

    uint32_t oldest_index = 0U;
    uint32_t oldest_age = g_juno.slots[0].age;

    for(uint32_t i = 1U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.slots[i].age < oldest_age)
        {
            oldest_age = g_juno.slots[i].age;
            oldest_index = i;
        }
    }

    return oldest_index;
}

static void juno_note_on_internal(uint8_t midi_note, uint8_t velocity)
{
    const uint32_t voice_index = juno_find_voice_for_note_on();
    kr106::Voice<float>& voice = g_juno.voices[voice_index];
    JunoVoiceSlot& slot = g_juno.slots[voice_index];

    voice.mPitch = midi_note_to_pitch(midi_note);
    voice.mMidiNote = static_cast<int>(midi_note);
    voice.mRawBend = g_juno.bend;

    slot.note = midi_note;
    slot.active = 1U;
    slot.held = 1U;
    slot.sustained = 0U;
    slot.age = ++g_juno.voice_age_counter;

    voice.Trigger(static_cast<float>(velocity) * (1.0f / 127.0f), false);
}

static void juno_note_off_internal(uint8_t midi_note)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        JunoVoiceSlot& slot = g_juno.slots[i];

        if((slot.active == 0U) || (slot.note != midi_note) || (slot.held == 0U))
            continue;

        slot.held = 0U;

        if(g_juno.sustain != 0U)
        {
            slot.sustained = 1U;
        }
        else
        {
            slot.sustained = 0U;
            g_juno.voices[i].Release();
        }
    }
}

static void juno_all_notes_off_internal(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.slots[i].active != 0U)
            g_juno.voices[i].Release();

        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
    }

    g_juno.sustain = 0U;
}

static void juno_panic_internal(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].Release();
        g_juno.slots[i].active = 0U;
        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
    }

    g_juno.sustain = 0U;
}

static void juno_set_pitch_bend_internal(int16_t value)
{
    if(value < -8192)
        value = -8192;
    if(value > 8191)
        value = 8191;

    g_juno.bend = (value >= 0) ? (float)value * (1.0f / 8191.0f)
                               : (float)value * (1.0f / 8192.0f);

    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
        g_juno.voices[i].mRawBend = g_juno.bend;
}

static void juno_cc_internal(uint8_t cc, uint8_t value)
{
    if(cc != 64U)
        return;

    const uint8_t sustain = (value >= 64U) ? 1U : 0U;

    if((g_juno.sustain != 0U) && (sustain == 0U))
        juno_release_sustained_voices();

    g_juno.sustain = sustain;
}

static void juno_consume_midi_queue(uint32_t max_events)
{
    juno_midi_event_t event;

    for(uint32_t i = 0U; i < max_events; ++i)
    {
        if(juno_midi_queue_pop(&event) == 0U)
            break;

        switch(event.type)
        {
            case JUNO_MIDI_EVENT_NOTE_ON:
                juno_note_on_internal(event.data1, event.data2);
                break;

            case JUNO_MIDI_EVENT_NOTE_OFF:
                juno_note_off_internal(event.data1);
                break;

            case JUNO_MIDI_EVENT_PITCH_BEND:
                juno_set_pitch_bend_internal(event.value);
                break;

            case JUNO_MIDI_EVENT_ALL_NOTES_OFF:
                juno_all_notes_off_internal();
                break;

            case JUNO_MIDI_EVENT_CC:
                juno_cc_internal(event.data1, event.data2);
                break;

            default:
                break;
        }
    }
}

static uint8_t juno_any_voice_busy(void)
{
    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if(g_juno.voices[i].GetBusy())
            return 1U;
    }

    return 0U;
}

} // namespace

extern "C" {

void juno_synth_init(float sample_rate, uint32_t block_size)
{
    memset(&g_juno, 0, sizeof(g_juno));

    g_juno.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    g_juno.block_size = (block_size <= AUDIO_BLOCK_SIZE) ? block_size : AUDIO_BLOCK_SIZE;
    g_juno.bend = 0.0f;
    g_juno.enabled = 0U;
    g_juno.test_mode = 0U;
    g_juno.unison_prepared = 0U;
    g_juno.sustain = 0U;

    juno_midi_queue_init();

    g_juno.lfo.mJ6Mode = true;
    g_juno.lfo.SetRate(0.18f, g_juno.sample_rate);
    g_juno.lfo.SetDelay(0.0f);
    g_juno.lfo.SetMode(0);
    g_juno.lfo.SetTrigger(false);

    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        juno_voice_apply_patch(g_juno.voices[i], i);
        g_juno.slots[i].active = 0U;
        g_juno.slots[i].note = 0U;
        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
        g_juno.slots[i].age = 0U;
    }

    g_juno.initialized = 1U;
}

void juno_synth_set_enabled(uint8_t enabled)
{
    g_juno.enabled = enabled ? 1U : 0U;

    if(g_juno.enabled == 0U)
    {
        juno_panic_internal();
        juno_midi_queue_clear();
    }
}

uint8_t juno_synth_is_enabled(void)
{
    return g_juno.enabled;
}

void juno_synth_set_test_mode(uint8_t enabled)
{
    g_juno.test_mode = enabled ? 1U : 0U;

    if(g_juno.test_mode == 0U)
        juno_all_notes_off_internal();
}

void juno_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    if(g_juno.enabled == 0U)
        return;

    if(g_juno.test_mode != 0U)
        return;

    if(g_juno.unison_prepared != 0U)
    {
        for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            juno_note_on_internal(midi_note, velocity);
    }
    else
    {
        juno_note_on_internal(midi_note, velocity);
    }
}

void juno_synth_note_off(uint8_t midi_note)
{
    juno_note_off_internal(midi_note);
}

void juno_synth_pitch_bend(int16_t value)
{
    juno_set_pitch_bend_internal(value);
}

void juno_synth_all_notes_off(void)
{
    juno_all_notes_off_internal();
}

void juno_synth_cc(uint8_t cc, uint8_t value)
{
    juno_cc_internal(cc, value);
}

void juno_synth_process_block(float *mono_out, uint32_t frames)
{
    if(mono_out == nullptr)
        return;

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    memset(mono_out, 0, frames * sizeof(float));

    if((g_juno.initialized == 0U) || (frames == 0U))
        return;

    if(g_juno.enabled == 0U)
    {
        juno_midi_queue_clear();
        return;
    }

    juno_consume_midi_queue(JUNO_MIDI_EVENTS_PER_BLOCK);

    if((g_juno.test_mode != 0U) && (juno_any_voice_busy() == 0U))
        juno_note_on_internal(48U, 108U);

    const uint8_t voice_active = juno_any_voice_busy();
    g_juno.lfo.SetVoiceActive(voice_active != 0U);

    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_juno.lfo_buffer[i] = g_juno.lfo.Process();
        g_juno.lfo_raw_buffer[i] = g_juno.lfo.mLastTri;
    }

    float *inputs[2] = {
        g_juno.lfo_buffer,
        g_juno.lfo_raw_buffer,
    };
    float *outputs[1] = {
        mono_out,
    };

    for(uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if((g_juno.slots[i].active == 0U) && (g_juno.voices[i].GetBusy() == false))
            continue;

        g_juno.voices[i].ProcessSamplesAccumulating(inputs, outputs, 2, 1, 0, static_cast<int>(frames));

        if(g_juno.voices[i].GetBusy() == false)
        {
            g_juno.slots[i].active = 0U;
            g_juno.slots[i].held = 0U;
            g_juno.slots[i].sustained = 0U;
        }
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        if(!std::isfinite(mono_out[i]))
            mono_out[i] = 0.0f;
    }
}

} // extern "C"
