#include "Audio/drum_synth.h"

#include <cstring>
#include <cmath>

#include "../../md-drum-synth-main/TRXBassDrum.h"
#include "../../md-drum-synth-main/TRXClaves.h"
#include "../../md-drum-synth-main/TRXHiHat.h"
#include "../../md-drum-synth-main/TRXSnareDrum.h"
#include "../../md-drum-synth-main/FmKickModel.h"
#include "../../md-drum-synth-main/FmSnareModel.h"
#include "../../md-drum-synth-main/FmTomModel.h"
#include "../../md-drum-synth-main/FmRimshotModel.h"
#include "../../md-drum-synth-main/FmClapModel.h"
#include "../../md-drum-synth-main/FmCowbellModel.h"
#include "../../md-drum-synth-main/FmCymbalModel.h"

#ifndef DRUM_SYNTH_INSTANCE_COUNT
#define DRUM_SYNTH_INSTANCE_COUNT 8U
#endif

typedef struct
{
    TRXBassDrum trx_bass_drum;
    TRXClaves trx_claves;
    TRXHiHat trx_hihat;
    TRXSnareDrum trx_snare;
    FmKickModel fm_kick;
    FmSnareModel fm_snare;
    FmTomModel fm_tom;
    FmRimshotModel fm_rimshot;
    FmClapModel fm_clap;
    FmCowbellModel fm_cowbell;
    FmCymbalModel fm_cymbal;

    DrumModel *active_model;
    uint8_t active_type;
    uint8_t ever_triggered;
    float note_pitch_factor;
    float velocity_gain;
    float ui_param_cache[PARAM_COUNT];
    uint8_t ui_param_valid[PARAM_COUNT];
} drum_synth_instance_t;

static drum_synth_instance_t g_drum_instances[DRUM_SYNTH_INSTANCE_COUNT];

static void drum_synth_reset_runtime_state(drum_synth_instance_t *instance)
{
    if (instance == nullptr)
    {
        return;
    }

    // IMPORTANT: do not memset() drum_synth_instance_t.
    // It contains polymorphic C++ engine objects whose vptr must remain valid.
    instance->active_model = nullptr;
    instance->active_type = (uint8_t)DRUM_MODEL_ID_COUNT;
    instance->ever_triggered = 0U;
    instance->note_pitch_factor = 1.0f;
    instance->velocity_gain = 1.0f;
    std::memset(instance->ui_param_cache, 0, sizeof(instance->ui_param_cache));
    std::memset(instance->ui_param_valid, 0, sizeof(instance->ui_param_valid));
}

typedef struct
{
    param_id_t param;
    float ui_min;
    float ui_max;
    float dsp_min;
    float dsp_max;
} drum_ui_dsp_linear_map_t;

static const drum_ui_dsp_linear_map_t g_drum_ui_dsp_linear_maps[] = {
    { PARAM_DRUM_TRX_CLAVES_INTERVAL, 0.0f, 127.0f, 0.0f, 400.0f },
    { PARAM_DRUM_TRX_HIHAT_HP_TONE, 0.0f, 127.0f, 100.0f, 10000.0f },
    { PARAM_DRUM_TRX_HIHAT_LP_TONE, 0.0f, 127.0f, 1000.0f, 12000.0f },
    { PARAM_DRUM_TRX_SNARE_TUNE_INTERVAL, 0.0f, 127.0f, 0.0f, 400.0f },
    { PARAM_DRUM_FM_TOM_MOD_FREQ, 0.0f, 127.0f, 100.0f, 2000.0f },
    { PARAM_DRUM_FM_TOM_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 50.0f },
    { PARAM_DRUM_FM_RIMSHOT_HP_TONE, 0.0f, 127.0f, 100.0f, 2000.0f },
    { PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 50.0f },
    { PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 50.0f },
    { PARAM_DRUM_FM_CLAP_HP_TONE, 0.0f, 127.0f, 20.0f, 2000.0f },
    { PARAM_DRUM_FM_CLAP_CLAP_SPACING, 0.0f, 5.0f, 0.005f, 0.05f },
    { PARAM_DRUM_FM_CLAP_BASE_FREQ, 0.0f, 127.0f, 100.0f, 1200.0f },
    { PARAM_DRUM_FM_CLAP_MOD_FREQ, 0.0f, 127.0f, 100.0f, 3000.0f },
    { PARAM_DRUM_FM_CLAP_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 100.0f },
    { PARAM_DRUM_FM_COWBELL_DECAY_SHORT, 0.0f, 20.0f, 0.005f, 0.2f },
    { PARAM_DRUM_FM_COWBELL_MOD_FREQ, 0.0f, 127.0f, 500.0f, 3000.0f },
    { PARAM_DRUM_FM_COWBELL_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 100.0f },
    { PARAM_DRUM_FM_CYMBAL_HP_TONE, 0.0f, 127.0f, 100.0f, 2000.0f },
    { PARAM_DRUM_FM_CYMBAL_FM_AMOUNT, 0.0f, 127.0f, 0.0f, 30.0f }
};

typedef struct
{
    param_id_t param;
    float center_hz;
} drum_pitch_offset_map_t;

static const drum_pitch_offset_map_t g_drum_pitch_offset_maps[] = {
    { PARAM_DRUM_TRX_BD_PITCH, 50.0f },
    { PARAM_DRUM_TRX_CLAVES_PITCH, 600.0f },
    { PARAM_DRUM_TRX_SNARE_PITCH, 180.0f },
    { PARAM_DRUM_FM_KICK_PITCH, 50.0f },
    { PARAM_DRUM_FM_SNARE_PITCH, 200.0f },
    { PARAM_DRUM_FM_TOM_PITCH, 150.0f },
    { PARAM_DRUM_FM_RIMSHOT_RIM_PITCH, 600.0f },
    { PARAM_DRUM_FM_RIMSHOT_BODY_PITCH, 200.0f },
    { PARAM_DRUM_FM_COWBELL_PITCH, 540.0f }
};

static float drum_synth_map_linear(float value, float in_min, float in_max, float out_min, float out_max)
{
    const float in_span = in_max - in_min;
    if (in_span <= 0.0f)
    {
        return out_min;
    }
    float t = (value - in_min) / in_span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return out_min + (out_max - out_min) * t;
}

static const drum_pitch_offset_map_t *drum_synth_find_pitch_offset_map(param_id_t param)
{
    for (uint32_t i = 0U; i < (sizeof(g_drum_pitch_offset_maps) / sizeof(g_drum_pitch_offset_maps[0])); ++i)
    {
        if (g_drum_pitch_offset_maps[i].param == param)
        {
            return &g_drum_pitch_offset_maps[i];
        }
    }
    return nullptr;
}

static float drum_synth_note_pitch_factor(uint8_t midi_note)
{
    const float semitones = (float)((int32_t)midi_note - 60);
    return powf(2.0f, semitones / 12.0f);
}

static float drum_synth_velocity_to_gain(uint8_t velocity)
{
    const float normalized = (float)((velocity == 0U) ? 1U : velocity) / 127.0f;
    const float curved = powf(normalized, 1.6f);
    return 0.12f + (0.88f * curved);
}

static float drum_synth_map_ui_to_dsp(param_id_t param, float ui_value, float note_pitch_factor)
{
    const drum_pitch_offset_map_t *const pitch_map = drum_synth_find_pitch_offset_map(param);
    if (pitch_map != nullptr)
    {
        const float freq = pitch_map->center_hz * powf(2.0f, ui_value / 12.0f);
        return freq * note_pitch_factor;
    }

    for (uint32_t i = 0U; i < (sizeof(g_drum_ui_dsp_linear_maps) / sizeof(g_drum_ui_dsp_linear_maps[0])); ++i)
    {
        const drum_ui_dsp_linear_map_t *const map = &g_drum_ui_dsp_linear_maps[i];
        if (map->param == param)
        {
            return drum_synth_map_linear(ui_value, map->ui_min, map->ui_max, map->dsp_min, map->dsp_max);
        }
    }

    return ui_value;
}

static uint8_t drum_synth_model_is_drum(drum_model_id_t type)
{
    switch (type)
    {
        case DRUM_MODEL_ID_TRX_BD:
        case DRUM_MODEL_ID_TRX_CLAVES:
        case DRUM_MODEL_ID_TRX_HIHAT:
        case DRUM_MODEL_ID_TRX_SNARE:
        case DRUM_MODEL_ID_FM_KICK:
        case DRUM_MODEL_ID_FM_SNARE:
        case DRUM_MODEL_ID_FM_TOM:
        case DRUM_MODEL_ID_FM_RIMSHOT:
        case DRUM_MODEL_ID_FM_CLAP:
        case DRUM_MODEL_ID_FM_COWBELL:
        case DRUM_MODEL_ID_FM_CYMBAL:
            return 1U;
        default:
            return 0U;
    }
}

static DrumModel *drum_synth_resolve_model(drum_synth_instance_t *instance, drum_model_id_t type)
{
    switch (type)
    {
        case DRUM_MODEL_ID_TRX_BD:
            return &instance->trx_bass_drum;
        case DRUM_MODEL_ID_TRX_CLAVES:
            return &instance->trx_claves;
        case DRUM_MODEL_ID_TRX_HIHAT:
            return &instance->trx_hihat;
        case DRUM_MODEL_ID_TRX_SNARE:
            return &instance->trx_snare;
        case DRUM_MODEL_ID_FM_KICK:
            return &instance->fm_kick;
        case DRUM_MODEL_ID_FM_SNARE:
            return &instance->fm_snare;
        case DRUM_MODEL_ID_FM_TOM:
            return &instance->fm_tom;
        case DRUM_MODEL_ID_FM_RIMSHOT:
            return &instance->fm_rimshot;
        case DRUM_MODEL_ID_FM_CLAP:
            return &instance->fm_clap;
        case DRUM_MODEL_ID_FM_COWBELL:
            return &instance->fm_cowbell;
        case DRUM_MODEL_ID_FM_CYMBAL:
            return &instance->fm_cymbal;
        default:
            return nullptr;
    }
}

static uint8_t drum_synth_set_direct_param(DrumModel *model,
                                           uint8_t param_index,
                                           uint8_t param_count,
                                           float value)
{
    // Runtime drum parameter path must remain direct and RT-safe:
    // no saveParameters/loadParameters text mutation here.
    if ((model == nullptr) || (param_index >= param_count))
    {
        return 0U;
    }
    return model->SetParamByIndex(param_index, value) ? 1U : 0U;
}

void drum_synth_init(float sample_rate)
{
    (void)sample_rate;

    for (uint8_t instance = 0U; instance < DRUM_SYNTH_INSTANCE_COUNT; ++instance)
    {
        drum_synth_reset_runtime_state(&g_drum_instances[instance]);
        (void)drum_synth_set_model_for_instance(instance, DRUM_MODEL_ID_TRX_BD);
    }
}

uint8_t drum_synth_set_model_for_instance(uint8_t instance_id, drum_model_id_t model_type)
{
    if ((instance_id >= DRUM_SYNTH_INSTANCE_COUNT) || (drum_synth_model_is_drum(model_type) == 0U))
    {
        return 0U;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if ((instance->active_model != nullptr) && (instance->active_type == model_type))
    {
        return 1U;
    }

    DrumModel *const resolved_model = drum_synth_resolve_model(instance, model_type);
    if (resolved_model == nullptr)
    {
        instance->active_model = nullptr;
        instance->active_type = (uint8_t)DRUM_MODEL_ID_COUNT;
        instance->ever_triggered = 0U;
        instance->note_pitch_factor = 1.0f;
        instance->velocity_gain = 1.0f;
        return 0U;
    }

    /*
     * Keep runtime calls safe while switching model type:
     * temporarily expose no active model during reinitialization.
     */
    instance->active_model = nullptr;
    instance->active_type = (uint8_t)DRUM_MODEL_ID_COUNT;
    instance->ever_triggered = 0U;
    instance->note_pitch_factor = 1.0f;
    instance->velocity_gain = 1.0f;
    resolved_model->Init();

    instance->active_type = model_type;
    instance->active_model = resolved_model;
    return 1U;
}

drum_model_id_t drum_synth_get_model_for_instance(uint8_t instance_id)
{
    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return DRUM_MODEL_ID_COUNT;
    }

    return (drum_model_id_t)g_drum_instances[instance_id].active_type;
}

void drum_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if (instance->active_model == nullptr)
    {
        return;
    }

    const float note_factor = drum_synth_note_pitch_factor(midi_note);
    if (instance->note_pitch_factor != note_factor)
    {
        instance->note_pitch_factor = note_factor;
        switch ((drum_model_id_t)instance->active_type)
        {
            case DRUM_MODEL_ID_TRX_BD:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_TRX_BD_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_TRX_BD_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_TRX_BD_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_TRX_CLAVES:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_TRX_CLAVES_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_TRX_CLAVES_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_TRX_CLAVES_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_TRX_SNARE:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_TRX_SNARE_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_TRX_SNARE_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_TRX_SNARE_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_FM_KICK:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_KICK_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_KICK_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_KICK_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_FM_SNARE:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_SNARE_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_SNARE_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_SNARE_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_FM_TOM:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_TOM_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_TOM_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_TOM_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_FM_RIMSHOT:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_RIMSHOT_RIM_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_RIMSHOT_RIM_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_RIMSHOT_RIM_PITCH] : 0.0f);
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_RIMSHOT_BODY_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_RIMSHOT_BODY_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_RIMSHOT_BODY_PITCH] : 0.0f);
                break;
            case DRUM_MODEL_ID_FM_COWBELL:
                (void)drum_synth_set_param_for_instance(instance_id,
                                                        PARAM_DRUM_FM_COWBELL_PITCH,
                                                        (instance->ui_param_valid[PARAM_DRUM_FM_COWBELL_PITCH] != 0U) ? instance->ui_param_cache[PARAM_DRUM_FM_COWBELL_PITCH] : 0.0f);
                break;
            default:
                break;
        }
    }

    instance->velocity_gain = drum_synth_velocity_to_gain(velocity);
    instance->active_model->Trigger();
    instance->ever_triggered = 1U;
}

void drum_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    (void)instance_id;
    (void)midi_note;
}

void drum_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if (instance->active_model != nullptr)
    {
        instance->active_model->Init();
    }
    instance->ever_triggered = 0U;
    instance->velocity_gain = 1.0f;
}

void drum_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    if ((mono_out == nullptr) || (instance_id >= DRUM_SYNTH_INSTANCE_COUNT))
    {
        return;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if ((instance->active_model == nullptr) || (instance->ever_triggered == 0U))
    {
        std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        mono_out[i] = instance->active_model->Process() * instance->velocity_gain;
    }
}

uint8_t drum_synth_set_param_for_instance(uint8_t instance_id, param_id_t param, float value)
{
    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return 0U;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if (instance->active_model == nullptr)
    {
        return 0U;
    }
    if (param < PARAM_COUNT)
    {
        instance->ui_param_cache[param] = value;
        instance->ui_param_valid[param] = 1U;
    }

    switch ((drum_model_id_t)instance->active_type)
    {
        case DRUM_MODEL_ID_TRX_BD:
            switch (param)
            {
                case PARAM_DRUM_TRX_BD_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_BD_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, value);
                case PARAM_DRUM_TRX_BD_PITCH_SWEEP: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, value);
                case PARAM_DRUM_TRX_BD_SWEEP_DECAY: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, value);
                case PARAM_DRUM_TRX_BD_ATTACK: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, value);
                case PARAM_DRUM_TRX_BD_NOISE: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, value);
                case PARAM_DRUM_TRX_BD_HARMONICS: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_TRX_BD_DRIVE: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_TRX_CLAVES:
            switch (param)
            {
                case PARAM_DRUM_TRX_CLAVES_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 5U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_CLAVES_INTERVAL: return drum_synth_set_direct_param(instance->active_model, 1U, 5U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_CLAVES_DECAY: return drum_synth_set_direct_param(instance->active_model, 2U, 5U, value);
                case PARAM_DRUM_TRX_CLAVES_BALANCE: return drum_synth_set_direct_param(instance->active_model, 3U, 5U, value);
                case PARAM_DRUM_TRX_CLAVES_DRIVE: return drum_synth_set_direct_param(instance->active_model, 4U, 5U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_TRX_HIHAT:
            switch (param)
            {
                case PARAM_DRUM_TRX_HIHAT_GAP: return drum_synth_set_direct_param(instance->active_model, 0U, 6U, value);
                case PARAM_DRUM_TRX_HIHAT_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 6U, value);
                case PARAM_DRUM_TRX_HIHAT_LP_TONE: return drum_synth_set_direct_param(instance->active_model, 2U, 6U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_HIHAT_HP_TONE: return drum_synth_set_direct_param(instance->active_model, 3U, 6U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_HIHAT_PEAK: return drum_synth_set_direct_param(instance->active_model, 4U, 6U, value);
                case PARAM_DRUM_TRX_HIHAT_METAL: return drum_synth_set_direct_param(instance->active_model, 5U, 6U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_TRX_SNARE:
            switch (param)
            {
                case PARAM_DRUM_TRX_SNARE_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_SNARE_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, value);
                case PARAM_DRUM_TRX_SNARE_SNAP: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, value);
                case PARAM_DRUM_TRX_SNARE_NOISE: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, value);
                case PARAM_DRUM_TRX_SNARE_TONE_MIX: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, value);
                case PARAM_DRUM_TRX_SNARE_TUNE_INTERVAL: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_TRX_SNARE_BUMP: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_TRX_SNARE_DRIVE: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_KICK:
            switch (param)
            {
                case PARAM_DRUM_FM_KICK_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 11U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_KICK_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 11U, value);
                case PARAM_DRUM_FM_KICK_MOD_FREQ: return drum_synth_set_direct_param(instance->active_model, 2U, 11U, value);
                case PARAM_DRUM_FM_KICK_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 3U, 11U, value);
                case PARAM_DRUM_FM_KICK_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 11U, value);
                case PARAM_DRUM_FM_KICK_FEEDBACK: return drum_synth_set_direct_param(instance->active_model, 5U, 11U, value);
                case PARAM_DRUM_FM_KICK_PITCH_SWEEP: return drum_synth_set_direct_param(instance->active_model, 6U, 11U, value);
                case PARAM_DRUM_FM_KICK_SWEEP_DECAY: return drum_synth_set_direct_param(instance->active_model, 7U, 11U, value);
                case PARAM_DRUM_FM_KICK_RATIO_MODE: return drum_synth_set_direct_param(instance->active_model, 8U, 11U, value);
                case PARAM_DRUM_FM_KICK_RATIO_INDEX: return drum_synth_set_direct_param(instance->active_model, 9U, 11U, value);
                case PARAM_DRUM_FM_KICK_MOD_ENV_SYNC: return drum_synth_set_direct_param(instance->active_model, 10U, 11U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_SNARE:
            switch (param)
            {
                case PARAM_DRUM_FM_SNARE_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_SNARE_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, value);
                case PARAM_DRUM_FM_SNARE_MOD_FREQ: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, value);
                case PARAM_DRUM_FM_SNARE_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, value);
                case PARAM_DRUM_FM_SNARE_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, value);
                case PARAM_DRUM_FM_SNARE_NOISE: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, value);
                case PARAM_DRUM_FM_SNARE_NOISE_DECAY: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_FM_SNARE_HP_TONE: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_TOM:
            switch (param)
            {
                case PARAM_DRUM_FM_TOM_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_TOM_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, value);
                case PARAM_DRUM_FM_TOM_MOD_FREQ: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_TOM_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_TOM_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, value);
                case PARAM_DRUM_FM_TOM_PITCH_SWEEP: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, value);
                case PARAM_DRUM_FM_TOM_SWEEP_DECAY: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_FM_TOM_START_PHASE: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_RIMSHOT:
            switch (param)
            {
                case PARAM_DRUM_FM_RIMSHOT_RIM_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 9U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_RIMSHOT_RIM_DECAY: return drum_synth_set_direct_param(instance->active_model, 1U, 9U, value);
                case PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 2U, 9U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_RIMSHOT_BODY_PITCH: return drum_synth_set_direct_param(instance->active_model, 3U, 9U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_RIMSHOT_BODY_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 9U, value);
                case PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 5U, 9U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_RIMSHOT_BODY_MIX: return drum_synth_set_direct_param(instance->active_model, 6U, 9U, value);
                case PARAM_DRUM_FM_RIMSHOT_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 7U, 9U, value);
                case PARAM_DRUM_FM_RIMSHOT_HP_TONE: return drum_synth_set_direct_param(instance->active_model, 8U, 9U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_CLAP:
            switch (param)
            {
                case PARAM_DRUM_FM_CLAP_BASE_FREQ: return drum_synth_set_direct_param(instance->active_model, 0U, 10U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CLAP_MOD_FREQ: return drum_synth_set_direct_param(instance->active_model, 1U, 10U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CLAP_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 2U, 10U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CLAP_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 3U, 10U, value);
                case PARAM_DRUM_FM_CLAP_CLAP_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 10U, value);
                case PARAM_DRUM_FM_CLAP_TAIL_DECAY: return drum_synth_set_direct_param(instance->active_model, 5U, 10U, value);
                case PARAM_DRUM_FM_CLAP_CLAP_COUNT: return drum_synth_set_direct_param(instance->active_model, 6U, 10U, value);
                case PARAM_DRUM_FM_CLAP_CLAP_SPACING: return drum_synth_set_direct_param(instance->active_model, 7U, 10U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CLAP_HP_TONE: return drum_synth_set_direct_param(instance->active_model, 8U, 10U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CLAP_FEEDBACK: return drum_synth_set_direct_param(instance->active_model, 9U, 10U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_COWBELL:
            switch (param)
            {
                case PARAM_DRUM_FM_COWBELL_PITCH: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_COWBELL_DECAY_SHORT: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_COWBELL_DECAY_LONG: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, value);
                case PARAM_DRUM_FM_COWBELL_MOD_FREQ: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_COWBELL_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_COWBELL_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, value);
                case PARAM_DRUM_FM_COWBELL_FEEDBACK: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_FM_COWBELL_ENV_MIX: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, value);
                default: return 0U;
            }
        case DRUM_MODEL_ID_FM_CYMBAL:
            switch (param)
            {
                case PARAM_DRUM_FM_CYMBAL_BASE_CARRIER: return drum_synth_set_direct_param(instance->active_model, 0U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_BASE_MOD: return drum_synth_set_direct_param(instance->active_model, 1U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_DECAY: return drum_synth_set_direct_param(instance->active_model, 2U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_FM_AMOUNT: return drum_synth_set_direct_param(instance->active_model, 3U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                case PARAM_DRUM_FM_CYMBAL_MOD_DECAY: return drum_synth_set_direct_param(instance->active_model, 4U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_FEEDBACK: return drum_synth_set_direct_param(instance->active_model, 5U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_SUSTAIN: return drum_synth_set_direct_param(instance->active_model, 6U, 8U, value);
                case PARAM_DRUM_FM_CYMBAL_HP_TONE: return drum_synth_set_direct_param(instance->active_model, 7U, 8U, drum_synth_map_ui_to_dsp(param, value, instance->note_pitch_factor));
                default: return 0U;
            }
        default:
            return 0U;
    }
}

void drum_synth_all_notes_off_all(void)
{
    for (uint8_t instance = 0U; instance < DRUM_SYNTH_INSTANCE_COUNT; ++instance)
    {
        drum_synth_all_notes_off_for_instance(instance);
    }
}
