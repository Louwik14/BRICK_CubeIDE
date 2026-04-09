#include "Audio/drum_synth.h"

#include <cstring>

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
#define DRUM_SYNTH_INSTANCE_COUNT UI_TRACK_COUNT
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
    ui_track_type_t active_type;
    uint8_t ever_triggered;
} drum_synth_instance_t;

static drum_synth_instance_t g_drum_instances[DRUM_SYNTH_INSTANCE_COUNT];

static uint8_t drum_synth_model_is_drum(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_DRUM_TRX_BD:
        case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
        case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
        case UI_TRACK_TYPE_DRUM_TRX_SNARE:
        case UI_TRACK_TYPE_DRUM_FM_KICK:
        case UI_TRACK_TYPE_DRUM_FM_SNARE:
        case UI_TRACK_TYPE_DRUM_FM_TOM:
        case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
        case UI_TRACK_TYPE_DRUM_FM_CLAP:
        case UI_TRACK_TYPE_DRUM_FM_COWBELL:
        case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
            return 1U;
        default:
            return 0U;
    }
}

static DrumModel *drum_synth_resolve_model(drum_synth_instance_t *instance, ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_DRUM_TRX_BD:
            return &instance->trx_bass_drum;
        case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
            return &instance->trx_claves;
        case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
            return &instance->trx_hihat;
        case UI_TRACK_TYPE_DRUM_TRX_SNARE:
            return &instance->trx_snare;
        case UI_TRACK_TYPE_DRUM_FM_KICK:
            return &instance->fm_kick;
        case UI_TRACK_TYPE_DRUM_FM_SNARE:
            return &instance->fm_snare;
        case UI_TRACK_TYPE_DRUM_FM_TOM:
            return &instance->fm_tom;
        case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
            return &instance->fm_rimshot;
        case UI_TRACK_TYPE_DRUM_FM_CLAP:
            return &instance->fm_clap;
        case UI_TRACK_TYPE_DRUM_FM_COWBELL:
            return &instance->fm_cowbell;
        case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
            return &instance->fm_cymbal;
        default:
            return nullptr;
    }
}

void drum_synth_init(float sample_rate)
{
    (void)sample_rate;

    std::memset(g_drum_instances, 0, sizeof(g_drum_instances));
    for (uint8_t instance = 0U; instance < DRUM_SYNTH_INSTANCE_COUNT; ++instance)
    {
        (void)drum_synth_set_model_for_instance(instance, UI_TRACK_TYPE_DRUM_TRX_BD);
    }
}

uint8_t drum_synth_set_model_for_instance(uint8_t instance_id, ui_track_type_t model_type)
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

    instance->active_model = drum_synth_resolve_model(instance, model_type);
    instance->active_type = model_type;
    instance->ever_triggered = 0U;
    if (instance->active_model != nullptr)
    {
        instance->active_model->Init();
    }

    return (instance->active_model != nullptr) ? 1U : 0U;
}

ui_track_type_t drum_synth_get_model_for_instance(uint8_t instance_id)
{
    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return UI_TRACK_TYPE_AUDIO;
    }

    return g_drum_instances[instance_id].active_type;
}

void drum_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    (void)midi_note;
    (void)velocity;

    if (instance_id >= DRUM_SYNTH_INSTANCE_COUNT)
    {
        return;
    }

    drum_synth_instance_t *const instance = &g_drum_instances[instance_id];
    if (instance->active_model == nullptr)
    {
        return;
    }

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
        mono_out[i] = instance->active_model->Process();
    }
}

void drum_synth_all_notes_off_all(void)
{
    for (uint8_t instance = 0U; instance < DRUM_SYNTH_INSTANCE_COUNT; ++instance)
    {
        drum_synth_all_notes_off_for_instance(instance);
    }
}
