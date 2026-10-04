#include "Audio/patch_preview_audio.h"

#include <string.h>

#include "Audio/Engines/acid_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/tb303_engine.h"
#include "Audio/mixer.h"
#include "Audio/preview_voice_dsp.h"
#include "Mod/mod_instrument_runtime.h"
#include "Param/param_filter_audio.h"
#include "Platform/memory_layout.h"

typedef enum
{
    PATCH_PREVIEW_AUDIO_EMPTY = 0U,
    PATCH_PREVIEW_AUDIO_PREPARED,
    PATCH_PREVIEW_AUDIO_ACTIVE
} patch_preview_audio_state_t;

typedef struct
{
    patch_preview_audio_state_t state;
    patch_preview_engine_t engine;
    uint8_t note;
    uint8_t modulation_present;
    mod_instrument_runtime_t modulation;
} patch_preview_audio_runtime_t;

static patch_preview_audio_runtime_t g_patch_preview_audio;
static AUDIO_HOT float g_patch_preview_render[BRICK6_FM_RENDER_BLOCK];

static void patch_preview_engine_reset(patch_preview_engine_t engine)
{
    switch (engine)
    {
        case PATCH_PREVIEW_ENGINE_FM: brick6_fm_preview_reset(); break;
        case PATCH_PREVIEW_ENGINE_PRISM:
            brick6_braids_runtime_reset_instance(BRICK6_BRAIDS_PREVIEW_INSTANCE_ID); break;
        case PATCH_PREVIEW_ENGINE_STACK:
            brick6_stack_runtime_reset_instance(BRICK6_STACK_PREVIEW_INSTANCE_ID); break;
        case PATCH_PREVIEW_ENGINE_TB303:
            brick6_tb303_runtime_reset_instance(BRICK6_TB303_PREVIEW_INSTANCE_ID); break;
        case PATCH_PREVIEW_ENGINE_ACID:
            brick6_acid_runtime_reset_instance(BRICK6_ACID_PREVIEW_INSTANCE_ID); break;
        default: break;
    }
}

static uint8_t patch_preview_prism_apply(param_id_t id, float value)
{
    const uint8_t instance = BRICK6_BRAIDS_PREVIEW_INSTANCE_ID;
    uint8_t osc;
    switch (id)
    {
        case PARAM_PRISM_OSC1_MODEL: case PARAM_PRISM_OSC2_MODEL:
            osc = (id == PARAM_PRISM_OSC2_MODEL) ? 1U : 0U;
            brick6_braids_runtime_set_osc_edit(instance, osc, value); return 1U;
        case PARAM_PRISM_PITCH_MOD1: case PARAM_PRISM_PITCH_MOD2:
            osc = (id == PARAM_PRISM_PITCH_MOD2) ? 1U : 0U;
            brick6_braids_runtime_set_osc_pitch_mod(instance, osc, value); return 1U;
        case PARAM_PRISM_OSC1_PARAM1: case PARAM_PRISM_OSC2_PARAM1:
            osc = (id == PARAM_PRISM_OSC2_PARAM1) ? 1U : 0U;
            brick6_braids_runtime_set_osc_timbre(instance, osc, value); return 1U;
        case PARAM_PRISM_OSC1_AMOD: case PARAM_PRISM_OSC2_AMOD:
            osc = (id == PARAM_PRISM_OSC2_AMOD) ? 1U : 0U;
            brick6_braids_runtime_set_osc_modulation(instance, osc, value); return 1U;
        case PARAM_PRISM_OSC1_PARAM2: case PARAM_PRISM_OSC2_PARAM2:
            osc = (id == PARAM_PRISM_OSC2_PARAM2) ? 1U : 0U;
            brick6_braids_runtime_set_osc_color(instance, osc, value); return 1U;
        case PARAM_PRISM_PHASE1_RESET:
            brick6_braids_runtime_set_phase_reset(instance, value >= 0.5f); return 1U;
        case PARAM_PRISM_VOLUME: brick6_braids_runtime_set_volume(instance, value); return 1U;
        case PARAM_PRISM_BALANCE: brick6_braids_runtime_set_balance(instance, value); return 1U;
        case PARAM_PRISM_TUNE: brick6_braids_runtime_set_tune(instance, value); return 1U;
        case PARAM_PRISM_DETUNE: brick6_braids_runtime_set_detune(instance, value); return 1U;
        case PARAM_PRISM_DRIFT: brick6_braids_runtime_set_drift(instance, value); return 1U;
        default: return 0U;
    }
}

static uint8_t patch_preview_stack_apply(param_id_t id, float value)
{
    const uint8_t instance = BRICK6_STACK_PREVIEW_INSTANCE_ID;
    if ((id >= PARAM_STACK_OSC1_LEVEL) && (id <= PARAM_STACK_OSC3_LEVEL))
    {
        brick6_stack_runtime_set_slot_level(instance,
            (uint8_t)(id - PARAM_STACK_OSC1_LEVEL), value);
        return 1U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint16_t relative = id - PARAM_STACK_OSC1_MODEL;
        const uint8_t slot = (uint8_t)(relative / 4U);
        switch (relative % 4U)
        {
            case 0U: brick6_stack_runtime_set_slot_model(instance, slot,
                (brick6_stack_model_t)(uint8_t)(value + 0.5f)); break;
            case 1U: brick6_stack_runtime_set_slot_tune(instance, slot, value); break;
            case 2U: brick6_stack_runtime_set_slot_timbre(instance, slot, value); break;
            case 3U: brick6_stack_runtime_set_slot_color(instance, slot, value); break;
            default: return 0U;
        }
        return 1U;
    }
    if (id == PARAM_STACK_NOISE_LEVEL)
        brick6_stack_runtime_set_noise_level(instance, value);
    else if (id == PARAM_STACK_OSC_DETUNE)
        brick6_stack_runtime_set_osc_detune(instance, value);
    else if (id == PARAM_STACK_PHASE_RESET)
        brick6_stack_runtime_set_phase_reset(instance, value >= 0.5f);
    else return 0U;
    return 1U;
}

static uint8_t patch_preview_303_apply(patch_preview_engine_t engine,
                                       param_id_t id, float value)
{
    const uint8_t acid = (engine == PATCH_PREVIEW_ENGINE_ACID) ? 1U : 0U;
    const param_id_t first = acid ? PARAM_ACID_WAVE : PARAM_TB303_WAVE;
    if ((id < first) || (id > (param_id_t)(first + 8U))) return 0U;
    const uint8_t relative = (uint8_t)(id - first);
#define APPLY_303(name_, arg_) do { if (acid) brick6_acid_runtime_set_##name_(BRICK6_ACID_PREVIEW_INSTANCE_ID, (arg_)); else brick6_tb303_runtime_set_##name_(BRICK6_TB303_PREVIEW_INSTANCE_ID, (arg_)); } while (0)
    switch (relative)
    {
        case 0U: APPLY_303(wave, value >= 0.5f); break;
        case 1U: APPLY_303(tune, value); break;
        case 2U: APPLY_303(cut, value); break;
        case 3U: APPLY_303(res, value); break;
        case 4U: APPLY_303(env_mod, value); break;
        case 5U: APPLY_303(decay, value); break;
        case 6U: APPLY_303(accent, value); break;
        case 7U: APPLY_303(slide, value >= 0.5f); break;
        case 8U: APPLY_303(vcf_rate, value >= 0.5f); break;
        default: return 0U;
    }
#undef APPLY_303
    return 1U;
}

static uint8_t patch_preview_engine_apply(patch_preview_engine_t engine,
                                          param_id_t id, float value)
{
    switch (engine)
    {
        case PATCH_PREVIEW_ENGINE_FM: return brick6_fm_preview_apply_param(id, value);
        case PATCH_PREVIEW_ENGINE_PRISM: return patch_preview_prism_apply(id, value);
        case PATCH_PREVIEW_ENGINE_STACK: return patch_preview_stack_apply(id, value);
        case PATCH_PREVIEW_ENGINE_TB303: case PATCH_PREVIEW_ENGINE_ACID:
            return patch_preview_303_apply(engine, id, value);
        default: return 0U;
    }
}

static uint8_t patch_preview_mod_apply(void *context, param_id_t id, float value)
{
    (void)context;
    if (preview_voice_dsp_apply_param(id, value) != 0U) return 1U;
    return patch_preview_engine_apply(g_patch_preview_audio.engine, id, value);
}

static uint8_t patch_preview_engine_prepare(const patch_preview_publication_t *publication)
{
    patch_preview_engine_reset(publication->engine);
    if (publication->engine == PATCH_PREVIEW_ENGINE_FM)
        return brick6_fm_preview_prepare(&publication->fm_base,
                                         &publication->fm_macros);
    const uint8_t count = publication->engine_param_count;
    if (count == 0U) return 0U;
    for (uint8_t slot = 0U; slot < count; ++slot)
    {
        const patch_preview_engine_param_t *const item =
            &publication->engine_param[slot];
        if (patch_preview_engine_apply(publication->engine,
                item->parameter, item->value) == 0U) return 0U;
    }
    if (publication->engine == PATCH_PREVIEW_ENGINE_PRISM)
        brick6_braids_runtime_set_vca_release_seconds(
            BRICK6_BRAIDS_PREVIEW_INSTANCE_ID,
            param_filter_audio_release_s(publication->vca.release));
    return 1U;
}

void patch_preview_audio_init(void)
{
    memset(&g_patch_preview_audio, 0, sizeof(g_patch_preview_audio));
    brick6_fm_preview_reset();
    preview_voice_dsp_reset();
    mod_instrument_runtime_reset(&g_patch_preview_audio.modulation);
}

uint8_t patch_preview_audio_prepare(const patch_preview_publication_t *publication)
{
    if ((publication == NULL) || (publication->engine <= PATCH_PREVIEW_ENGINE_NONE)
            || (publication->engine > PATCH_PREVIEW_ENGINE_ACID)) return 0U;
    (void)patch_preview_audio_stop();
    g_patch_preview_audio.engine = publication->engine;
    if ((patch_preview_engine_prepare(publication) == 0U)
            || (preview_voice_dsp_prepare(&publication->filter,
                                          &publication->vca) == 0U))
    {
        (void)patch_preview_audio_stop();
        return 0U;
    }
    if ((publication->modulation_present != 0U)
            && (mod_instrument_runtime_prepare(&g_patch_preview_audio.modulation,
                &publication->modulation) == 0U))
    {
        (void)patch_preview_audio_stop();
        return 0U;
    }
    g_patch_preview_audio.modulation_present = publication->modulation_present;
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_PREPARED;
    return 1U;
}

uint8_t patch_preview_audio_note_on(uint8_t note, uint8_t velocity)
{
    if ((g_patch_preview_audio.state == PATCH_PREVIEW_AUDIO_EMPTY)
            || (note > 127U) || (velocity == 0U) || (velocity > 127U)) return 0U;
    switch (g_patch_preview_audio.engine)
    {
        case PATCH_PREVIEW_ENGINE_FM: brick6_fm_preview_note_on(note, velocity); break;
        case PATCH_PREVIEW_ENGINE_PRISM: brick6_braids_runtime_note_on(
            BRICK6_BRAIDS_PREVIEW_INSTANCE_ID, (float)note,
            (float)velocity * (1.0f / 127.0f)); break;
        case PATCH_PREVIEW_ENGINE_STACK: brick6_stack_runtime_note_on(
            BRICK6_STACK_PREVIEW_INSTANCE_ID, note, velocity); break;
        case PATCH_PREVIEW_ENGINE_TB303: brick6_tb303_runtime_note_on(
            BRICK6_TB303_PREVIEW_INSTANCE_ID, note, velocity); break;
        case PATCH_PREVIEW_ENGINE_ACID: brick6_acid_runtime_note_on(
            BRICK6_ACID_PREVIEW_INSTANCE_ID, note, velocity); break;
        default: return 0U;
    }
    preview_voice_dsp_note_on(note, velocity);
    if (g_patch_preview_audio.modulation_present != 0U)
        mod_instrument_runtime_note_on(&g_patch_preview_audio.modulation);
    g_patch_preview_audio.note = note;
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_ACTIVE;
    return 1U;
}

uint8_t patch_preview_audio_note_off(uint8_t note)
{
    if (g_patch_preview_audio.state == PATCH_PREVIEW_AUDIO_EMPTY) return 1U;
    switch (g_patch_preview_audio.engine)
    {
        case PATCH_PREVIEW_ENGINE_FM: brick6_fm_preview_note_off(note); break;
        case PATCH_PREVIEW_ENGINE_PRISM: brick6_braids_runtime_note_off(
            BRICK6_BRAIDS_PREVIEW_INSTANCE_ID, note); break;
        case PATCH_PREVIEW_ENGINE_STACK: brick6_stack_runtime_note_off(
            BRICK6_STACK_PREVIEW_INSTANCE_ID, note); break;
        case PATCH_PREVIEW_ENGINE_TB303: brick6_tb303_runtime_note_off(
            BRICK6_TB303_PREVIEW_INSTANCE_ID, note); break;
        case PATCH_PREVIEW_ENGINE_ACID: brick6_acid_runtime_note_off(
            BRICK6_ACID_PREVIEW_INSTANCE_ID, note); break;
        default: break;
    }
    preview_voice_dsp_note_off(note);
    if (g_patch_preview_audio.modulation_present != 0U)
        mod_instrument_runtime_note_off(&g_patch_preview_audio.modulation);
    return 1U;
}

uint8_t patch_preview_audio_stop(void)
{
    patch_preview_engine_reset(g_patch_preview_audio.engine);
    preview_voice_dsp_reset();
    mod_instrument_runtime_reset(&g_patch_preview_audio.modulation);
    g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_EMPTY;
    g_patch_preview_audio.engine = PATCH_PREVIEW_ENGINE_NONE;
    g_patch_preview_audio.note = 0U;
    g_patch_preview_audio.modulation_present = 0U;
    return 1U;
}

static uint8_t patch_preview_engine_render(float *mono, uint32_t frames)
{
    switch (g_patch_preview_audio.engine)
    {
        case PATCH_PREVIEW_ENGINE_FM: return brick6_fm_preview_render(mono, frames);
        case PATCH_PREVIEW_ENGINE_PRISM: return brick6_braids_runtime_render_instance(
            BRICK6_BRAIDS_PREVIEW_INSTANCE_ID, mono, frames);
        case PATCH_PREVIEW_ENGINE_STACK: return brick6_stack_runtime_render_instance(
            BRICK6_STACK_PREVIEW_INSTANCE_ID, mono, frames, 1U);
        case PATCH_PREVIEW_ENGINE_TB303: return brick6_tb303_runtime_render_instance(
            BRICK6_TB303_PREVIEW_INSTANCE_ID, mono, frames);
        case PATCH_PREVIEW_ENGINE_ACID: return brick6_acid_runtime_render_instance(
            BRICK6_ACID_PREVIEW_INSTANCE_ID, mono, frames);
        default: return 0U;
    }
}

uint8_t patch_preview_audio_render_main(float *out_main_l, float *out_main_r,
                                        uint32_t frames)
{
    if ((g_patch_preview_audio.state != PATCH_PREVIEW_AUDIO_ACTIVE)
            || (out_main_l == NULL) || (out_main_r == NULL)
            || (frames == 0U) || (frames > BRICK6_FM_RENDER_BLOCK)) return 0U;
    if (g_patch_preview_audio.modulation_present != 0U)
    {
        float filter_envelope;
        float vca_envelope;
        preview_voice_dsp_prepare_mod_sources(frames, &filter_envelope,
                                              &vca_envelope);
        mod_instrument_runtime_process(&g_patch_preview_audio.modulation,
            frames, filter_envelope, vca_envelope,
            patch_preview_mod_apply, NULL);
    }
    memset(g_patch_preview_render, 0, frames * sizeof(*g_patch_preview_render));
    (void)patch_preview_engine_render(g_patch_preview_render, frames);
    const uint8_t running = preview_voice_dsp_process(g_patch_preview_render, frames);
    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        const float sample = g_patch_preview_render[frame] * MIXER_TRACK_NOMINAL_TRIM;
        out_main_l[frame] += sample;
        out_main_r[frame] += sample;
    }
    if (running == 0U)
    {
        patch_preview_engine_reset(g_patch_preview_audio.engine);
        g_patch_preview_audio.state = PATCH_PREVIEW_AUDIO_PREPARED;
    }
    return 1U;
}
