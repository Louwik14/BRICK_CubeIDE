#include "Audio/juno_synth.h"
#include "Audio/audio_float.h"
#include "Audio/juno_midi_queue.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "stm32h7xx_hal.h"

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

const uint16_t kr106::LFO::kLfoRampTable[8] = {
    0xFFFF,
    0x0419,
    0x020C,
    0x015E,
    0x0100,
    0x0100,
    0x0100,
    0x0100
};

namespace {

constexpr uint32_t JUNO_SYNTH_NUM_VOICES = 4U;
constexpr uint32_t JUNO_MIDI_EVENTS_PER_BLOCK = 16U;
constexpr uint32_t JUNO_HELD_NOTE_STACK_LEN = 16U;
constexpr float JUNO_BEND_RANGE_OCT = 2.0f / 12.0f;
constexpr uint32_t JUNO_ALL_PARAMS_MASK = (1UL << JUNO_PARAM_COUNT) - 1UL;

constexpr float kJunoDefaultParams[JUNO_PARAM_COUNT] = {
    1.0f,
    1.0f,
    0.32f,
    0.38f,
    0.58f,
    0.18f,
    0.42f,
    0.06f,
    0.22f,
    0.44f,
    0.68f,
    0.46f,
    0.18f,
    1.0f,
    0.22f,
    (float)JUNO_PLAY_MODE_POLY,
};

struct JunoVoiceSlot
{
    uint8_t active;
    uint8_t note;
    uint8_t held;
    uint8_t sustained;
    uint32_t age;
};

struct JunoHpf
{
    static constexpr float kShelfFreqHz = 150.0f;
    static constexpr float kShelfGainLin = 3.162f;
    static constexpr float kHPFFreqs[4] = { 0.0f, 0.0f, 240.0f, 720.0f };
    static constexpr float kDCBlockHz = 5.0f;

    int mode = 1;
    float sample_rate = 44100.0f;
    float g = 0.0f;
    float hp_state = 0.0f;
    float lp_state = 0.0f;
    float dc_g = 0.0f;
    float dc_state = 0.0f;

    void Init()
    {
        hp_state = 0.0f;
        lp_state = 0.0f;
        dc_state = 0.0f;
    }

    void SetSampleRate(float sr)
    {
        sample_rate = sr;
        Recalc();
    }

    void SetMode(int new_mode)
    {
        new_mode = std::clamp(new_mode, 0, 3);
        if (new_mode == mode)
        {
            return;
        }

        mode = new_mode;
        Recalc();
    }

    void Recalc()
    {
        const float dc_frq = std::clamp(kDCBlockHz / (sample_rate * 0.5f), 0.001f, 0.9f);
        dc_g = tanf(dc_frq * static_cast<float>(M_PI) * 0.5f);

        if (mode == 1)
        {
            g = 0.0f;
            return;
        }

        const float fc = (mode == 0) ? kShelfFreqHz : kHPFFreqs[mode];
        const float frq = std::clamp(fc / (sample_rate * 0.5f), 0.001f, 0.9f);
        g = tanf(frq * static_cast<float>(M_PI) * 0.5f);
    }

    float DCBlock(float input)
    {
        const float v = (input - dc_state) * dc_g / (1.0f + dc_g);
        const float lp = dc_state + v;
        dc_state = lp + v;
        return input - lp;
    }

    float Process(float input)
    {
        if (mode == 1)
        {
            return DCBlock(input);
        }

        if (mode == 0)
        {
            const float v = (input - lp_state) * g / (1.0f + g);
            const float lp = lp_state + v;
            lp_state = lp + v;
            return DCBlock(input + (kShelfGainLin - 1.0f) * lp);
        }

        const float v = (input - hp_state) * g / (1.0f + g);
        const float lp = hp_state + v;
        hp_state = lp + v;
        return input - lp;
    }
};

struct JunoSynthState
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t test_mode;
    uint8_t sustain;
    uint8_t mode;
    uint8_t held_notes[JUNO_HELD_NOTE_STACK_LEN];
    uint8_t held_note_count;
    uint8_t last_velocity;
    float sample_rate;
    float bend;
    uint32_t block_size;
    uint32_t voice_age_counter;
    volatile uint32_t param_dirty_mask;
    float params[JUNO_PARAM_COUNT];
    float pending_params[JUNO_PARAM_COUNT];
    kr106::LFO lfo;
    kr106::Voice<float> voices[JUNO_SYNTH_NUM_VOICES];
    JunoVoiceSlot slots[JUNO_SYNTH_NUM_VOICES];
    JunoHpf hpf;
    float lfo_buffer[AUDIO_BLOCK_SIZE];
    float lfo_raw_buffer[AUDIO_BLOCK_SIZE];
};

static JunoSynthState g_juno;

static float midi_note_to_pitch(uint8_t midi_note)
{
    return (static_cast<float>(midi_note) - 69.0f) * (1.0f / 12.0f);
}

static float juno_clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

static float juno_attack_tau_from_slider(float slider)
{
    static constexpr float kAttackTau[11] = {
        0.000558f, 0.001674f, 0.008762f, 0.029468f, 0.064015f, 0.120998f,
        0.238481f, 0.495993f, 0.607950f, 1.392486f, 1.674332f
    };

    const float s = juno_clamp01(slider) * 10.0f;
    int idx = static_cast<int>(s);
    if (idx >= 10)
    {
        idx = 9;
    }

    const float frac = s - static_cast<float>(idx);
    return std::exp(std::log(kAttackTau[idx]) + frac * (std::log(kAttackTau[idx + 1]) - std::log(kAttackTau[idx])));
}

static float juno_decay_release_tau_from_slider(float slider)
{
    const float s = juno_clamp01(slider);
    return 0.003577f * std::exp(12.9460f * s + -5.0638f * s * s);
}

static void juno_reset_held_notes(void)
{
    g_juno.held_note_count = 0U;
    std::memset(g_juno.held_notes, 0, sizeof(g_juno.held_notes));
}

static void juno_remove_held_note(uint8_t midi_note)
{
    for (uint32_t i = 0U; i < g_juno.held_note_count; ++i)
    {
        if (g_juno.held_notes[i] != midi_note)
        {
            continue;
        }

        for (uint32_t j = i + 1U; j < g_juno.held_note_count; ++j)
        {
            g_juno.held_notes[j - 1U] = g_juno.held_notes[j];
        }

        g_juno.held_note_count--;
        return;
    }
}

static void juno_push_held_note(uint8_t midi_note)
{
    juno_remove_held_note(midi_note);

    if (g_juno.held_note_count >= JUNO_HELD_NOTE_STACK_LEN)
    {
        for (uint32_t i = 1U; i < JUNO_HELD_NOTE_STACK_LEN; ++i)
        {
            g_juno.held_notes[i - 1U] = g_juno.held_notes[i];
        }
        g_juno.held_note_count = JUNO_HELD_NOTE_STACK_LEN - 1U;
    }

    g_juno.held_notes[g_juno.held_note_count++] = midi_note;
}

static int16_t juno_get_last_held_note(void)
{
    if (g_juno.held_note_count == 0U)
    {
        return -1;
    }

    return g_juno.held_notes[g_juno.held_note_count - 1U];
}

static uint8_t juno_any_voice_busy(void)
{
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if (g_juno.voices[i].GetBusy())
        {
            return 1U;
        }
    }

    return 0U;
}

static void juno_update_voice_porta_flags(void)
{
    const uint8_t porta_enabled = (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_POLY_PORTA) ? 1U : 0U;

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].mPortaEnabled = (porta_enabled != 0U);
        g_juno.voices[i].mPortaRateParam = juno_clamp01(g_juno.params[JUNO_PARAM_PORTA]);
    }
}

static void juno_apply_common_voice_patch(kr106::Voice<float>& voice, uint32_t voice_index)
{
    voice.InitVariance(static_cast<int>(voice_index));
    voice.mADSR.mJ6Mode = true;
    voice.mVCF.mJ106Res = false;
    voice.mOsc.mPulseInvert = false;
    voice.mDcoNoise = 0.0f;
    voice.mDcoLfo = kr106::Voice<float>::dcoLfoDepth6(0.08f);
    voice.mVcfKbd = 0.55f;
    voice.mBendDco = JUNO_BEND_RANGE_OCT;
    voice.mBendVcf = 0.0f;
    voice.mBendLfo = 0.0f;
    voice.mRawBend = g_juno.bend;
    voice.mBenderModAmt = 0.0f;
    voice.mOctTranspose = 0.0f;
    voice.mPwmMode = 0;
    voice.mVcaMode = 0;
    voice.mVcfEnvInvert = 1;
    voice.SetSampleRateAndBlockSize(g_juno.sample_rate, static_cast<int>(g_juno.block_size));
}

static void juno_release_sustained_voices(void)
{
    if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
    {
        if (g_juno.held_note_count == 0U)
        {
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].Release();
                g_juno.slots[i].held = 0U;
                g_juno.slots[i].sustained = 0U;
            }
        }
        return;
    }

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if ((g_juno.slots[i].active != 0U) &&
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
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if (g_juno.voices[i].GetBusy() == false)
        {
            return i;
        }
    }

    uint32_t oldest_index = 0U;
    uint32_t oldest_age = g_juno.slots[0].age;

    for (uint32_t i = 1U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if (g_juno.slots[i].age < oldest_age)
        {
            oldest_age = g_juno.slots[i].age;
            oldest_index = i;
        }
    }

    return oldest_index;
}

static void juno_note_on_poly(uint8_t midi_note, uint8_t velocity)
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

static void juno_note_off_poly(uint8_t midi_note)
{
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        JunoVoiceSlot& slot = g_juno.slots[i];

        if ((slot.active == 0U) || (slot.note != midi_note) || (slot.held == 0U))
        {
            continue;
        }

        slot.held = 0U;

        if (g_juno.sustain != 0U)
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

static void juno_unison_set_slots(uint8_t midi_note, uint8_t held, uint8_t sustained)
{
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.slots[i].active = 1U;
        g_juno.slots[i].note = midi_note;
        g_juno.slots[i].held = held;
        g_juno.slots[i].sustained = sustained;
        g_juno.slots[i].age = ++g_juno.voice_age_counter;
    }
}

static void juno_unison_play_note(uint8_t midi_note, uint8_t velocity, uint8_t retrigger)
{
    const float level = static_cast<float>(velocity) * (1.0f / 127.0f);

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].mMidiNote = static_cast<int>(midi_note);
        g_juno.voices[i].SetUnisonPitch(midi_note_to_pitch(midi_note));
        g_juno.voices[i].mRawBend = g_juno.bend;
        if (retrigger != 0U)
        {
            g_juno.voices[i].Trigger(level, g_juno.voices[i].GetBusy());
        }
    }

    juno_unison_set_slots(midi_note, 1U, 0U);
}

static void juno_note_on_unison(uint8_t midi_note, uint8_t velocity)
{
    g_juno.last_velocity = velocity;
    juno_push_held_note(midi_note);
    juno_unison_play_note(midi_note, velocity, 1U);
}

static void juno_note_off_unison(uint8_t midi_note)
{
    juno_remove_held_note(midi_note);

    const int16_t next_note = juno_get_last_held_note();
    if (next_note >= 0)
    {
        juno_unison_play_note((uint8_t)next_note, g_juno.last_velocity, 1U);
        return;
    }

    if (g_juno.sustain != 0U)
    {
        for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
        {
            g_juno.slots[i].held = 0U;
            g_juno.slots[i].sustained = 1U;
        }
        return;
    }

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].Release();
        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
    }
}

static void juno_all_notes_off_internal(void)
{
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if (g_juno.slots[i].active != 0U)
        {
            g_juno.voices[i].Release();
        }

        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
    }

    g_juno.sustain = 0U;
    juno_reset_held_notes();
}

static void juno_panic_internal(void)
{
    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].Release();
        g_juno.slots[i].active = 0U;
        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
    }

    g_juno.sustain = 0U;
    juno_reset_held_notes();
}

static void juno_set_pitch_bend_internal(int16_t value)
{
    if (value < -8192)
    {
        value = -8192;
    }
    if (value > 8191)
    {
        value = 8191;
    }

    g_juno.bend = (value >= 0) ? (float)value * (1.0f / 8191.0f)
                               : (float)value * (1.0f / 8192.0f);

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        g_juno.voices[i].mRawBend = g_juno.bend;
    }
}

static void juno_cc_internal(uint8_t cc, uint8_t value)
{
    if (cc != 64U)
    {
        return;
    }

    const uint8_t sustain = (value >= 64U) ? 1U : 0U;

    if ((g_juno.sustain != 0U) && (sustain == 0U))
    {
        juno_release_sustained_voices();
    }

    g_juno.sustain = sustain;
}

static void juno_apply_param(uint32_t index)
{
    const float value = g_juno.params[index];

    switch ((juno_param_id_t)index)
    {
        case JUNO_PARAM_SAW:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mSawOn = (value > 0.5f);
            }
            break;

        case JUNO_PARAM_PULSE:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mPulseOn = (value > 0.5f);
            }
            break;

        case JUNO_PARAM_SUB:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mSubOn = (value > 0.001f);
                g_juno.voices[i].mDcoSub = juno_clamp01(value);
            }
            break;

        case JUNO_PARAM_PWM:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mPwmMode = 0;
                g_juno.voices[i].mDcoPwm = juno_clamp01(value);
            }
            break;

        case JUNO_PARAM_VCF_FREQ:
        {
            const float slider = juno_clamp01(value);
            const float hz = kr106::j6_vcf_freq_from_slider(slider);
            const uint16_t cutoff_int = static_cast<uint16_t>(slider * 0x3F80);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mVcfFreq = hz;
                g_juno.voices[i].mVcfCutoffInt = cutoff_int;
            }
            break;
        }

        case JUNO_PARAM_VCF_RES:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mVcfRes = juno_clamp01(value);
            }
            break;

        case JUNO_PARAM_VCF_ENV:
        {
            const float env = juno_clamp01(value);
            const uint8_t env_int = static_cast<uint8_t>(env * 254.0f);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mVcfEnv = env;
                g_juno.voices[i].mVcfEnvModInt = env_int;
            }
            break;
        }

        case JUNO_PARAM_VCF_LFO:
        {
            const float slider = juno_clamp01(value);
            const float depth = kr106::Voice<float>::vcfLfoDepth6(slider);
            const uint8_t lfo_int = static_cast<uint8_t>(slider * 254.0f);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mVcfLfo = depth;
                g_juno.voices[i].mVcfLfoDepthInt = lfo_int;
            }
            break;
        }

        case JUNO_PARAM_ATTACK:
        {
            const float tau = juno_attack_tau_from_slider(value);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mADSR.SetAttackTau(tau);
            }
            break;
        }

        case JUNO_PARAM_DECAY:
        {
            const float tau = juno_decay_release_tau_from_slider(value);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mADSR.SetDecayTau(tau);
            }
            break;
        }

        case JUNO_PARAM_SUSTAIN:
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mADSR.SetSustain(std::max(juno_clamp01(value), 0.001f));
            }
            break;

        case JUNO_PARAM_RELEASE:
        {
            const float tau = juno_decay_release_tau_from_slider(value);
            for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
            {
                g_juno.voices[i].mADSR.SetReleaseTau(tau);
            }
            break;
        }

        case JUNO_PARAM_LFO_RATE:
            g_juno.lfo.SetRate(juno_clamp01(value), g_juno.sample_rate);
            break;

        case JUNO_PARAM_HPF:
            g_juno.hpf.SetMode((int)std::clamp(value, 0.0f, 3.0f));
            break;

        case JUNO_PARAM_PORTA:
            juno_update_voice_porta_flags();
            break;

        case JUNO_PARAM_MODE:
            g_juno.mode = (uint8_t)std::clamp((int)(value + 0.5f), 0, (int)JUNO_PLAY_MODE_UNISON);
            juno_update_voice_porta_flags();
            break;

        default:
            break;
    }
}

static void juno_apply_pending_params(void)
{
    const uint32_t dirty_mask = g_juno.param_dirty_mask;
    if (dirty_mask == 0U)
    {
        return;
    }

    g_juno.param_dirty_mask = 0U;
    __DMB();

    uint8_t mode_changed = 0U;

    for (uint32_t i = 0U; i < (uint32_t)JUNO_PARAM_COUNT; ++i)
    {
        const uint32_t bit = 1UL << i;
        if ((dirty_mask & bit) == 0U)
        {
            continue;
        }

        g_juno.params[i] = g_juno.pending_params[i];
        if (i == (uint32_t)JUNO_PARAM_MODE)
        {
            mode_changed = 1U;
        }
        juno_apply_param(i);
    }

    if (mode_changed != 0U)
    {
        juno_panic_internal();
    }
}

static void juno_consume_midi_queue(uint32_t max_events)
{
    juno_midi_event_t event;

    for (uint32_t i = 0U; i < max_events; ++i)
    {
        if (juno_midi_queue_pop(&event) == 0U)
        {
            break;
        }

        switch (event.type)
        {
            case JUNO_MIDI_EVENT_NOTE_ON:
                if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
                {
                    juno_note_on_unison(event.data1, event.data2);
                }
                else
                {
                    juno_note_on_poly(event.data1, event.data2);
                }
                break;

            case JUNO_MIDI_EVENT_NOTE_OFF:
                if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
                {
                    juno_note_off_unison(event.data1);
                }
                else
                {
                    juno_note_off_poly(event.data1);
                }
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

} // namespace

extern "C" {

void juno_synth_init(float sample_rate, uint32_t block_size)
{
    g_juno = JunoSynthState{};

    g_juno.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    g_juno.block_size = (block_size <= AUDIO_BLOCK_SIZE) ? block_size : AUDIO_BLOCK_SIZE;
    g_juno.bend = 0.0f;
    g_juno.enabled = 0U;
    g_juno.test_mode = 0U;
    g_juno.sustain = 0U;
    g_juno.mode = (uint8_t)JUNO_PLAY_MODE_POLY;
    g_juno.last_velocity = 100U;

    juno_midi_queue_init();
    juno_reset_held_notes();

    g_juno.lfo.mJ6Mode = true;
    g_juno.lfo.SetDelay(0.0f);
    g_juno.lfo.SetMode(0);
    g_juno.lfo.SetTrigger(false);
    g_juno.hpf.Init();
    g_juno.hpf.SetSampleRate(g_juno.sample_rate);

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        juno_apply_common_voice_patch(g_juno.voices[i], i);
        g_juno.slots[i].active = 0U;
        g_juno.slots[i].note = 0U;
        g_juno.slots[i].held = 0U;
        g_juno.slots[i].sustained = 0U;
        g_juno.slots[i].age = 0U;
    }

    for (uint32_t i = 0U; i < (uint32_t)JUNO_PARAM_COUNT; ++i)
    {
        g_juno.params[i] = kJunoDefaultParams[i];
        g_juno.pending_params[i] = kJunoDefaultParams[i];
    }

    g_juno.param_dirty_mask = JUNO_ALL_PARAMS_MASK;
    juno_apply_pending_params();
    g_juno.initialized = 1U;
}

void juno_synth_set_enabled(uint8_t enabled)
{
    g_juno.enabled = enabled ? 1U : 0U;

    if (g_juno.enabled == 0U)
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

    if (g_juno.test_mode == 0U)
    {
        juno_all_notes_off_internal();
    }
}

void juno_synth_set_param(juno_param_id_t param, float value)
{
    if ((uint32_t)param >= (uint32_t)JUNO_PARAM_COUNT)
    {
        return;
    }

    g_juno.pending_params[(uint32_t)param] = value;
    __DMB();
    g_juno.param_dirty_mask |= (1UL << (uint32_t)param);
}

void juno_synth_note_on(uint8_t midi_note, uint8_t velocity)
{
    if ((g_juno.enabled == 0U) || (g_juno.test_mode != 0U))
    {
        return;
    }

    if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
    {
        juno_note_on_unison(midi_note, velocity);
    }
    else
    {
        juno_note_on_poly(midi_note, velocity);
    }
}

void juno_synth_note_off(uint8_t midi_note)
{
    if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
    {
        juno_note_off_unison(midi_note);
    }
    else
    {
        juno_note_off_poly(midi_note);
    }
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
    if (mono_out == nullptr)
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    std::memset(mono_out, 0, frames * sizeof(float));

    if ((g_juno.initialized == 0U) || (frames == 0U))
    {
        return;
    }

    if (g_juno.enabled == 0U)
    {
        juno_midi_queue_clear();
        return;
    }

    juno_apply_pending_params();
    juno_consume_midi_queue(JUNO_MIDI_EVENTS_PER_BLOCK);

    if ((g_juno.test_mode != 0U) && (juno_any_voice_busy() == 0U))
    {
        if (g_juno.mode == (uint8_t)JUNO_PLAY_MODE_UNISON)
        {
            juno_note_on_unison(48U, 108U);
        }
        else
        {
            juno_note_on_poly(48U, 108U);
        }
    }

    g_juno.lfo.SetVoiceActive(juno_any_voice_busy() != 0U);

    for (uint32_t i = 0U; i < frames; ++i)
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

    for (uint32_t i = 0U; i < JUNO_SYNTH_NUM_VOICES; ++i)
    {
        if ((g_juno.slots[i].active == 0U) && (g_juno.voices[i].GetBusy() == false))
        {
            continue;
        }

        g_juno.voices[i].ProcessSamplesAccumulating(inputs, outputs, 2, 1, 0, static_cast<int>(frames));

        if (g_juno.voices[i].GetBusy() == false)
        {
            g_juno.slots[i].active = 0U;
            g_juno.slots[i].held = 0U;
            g_juno.slots[i].sustained = 0U;
        }
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = g_juno.hpf.Process(mono_out[i]);
        if (!std::isfinite(mono_out[i]))
        {
            mono_out[i] = 0.0f;
        }
    }
}

} // extern "C"
