#include "pages/ui_page_template_mod.h"

#include <stdio.h>
#include <string.h>

#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Mod/mod_destination_control.h"
#include "Param/param_registry.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_param.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_mod_subset = 0U;
static ui_template_page_state_t g_ui_template_mod_state;
static uint8_t ui_page_template_mod_virtual_slot_text(uint8_t slot,char*out_name,
    uint32_t out_name_len,char*out_value,uint32_t out_value_len);

typedef struct { const char *label; const char *short_label; const char *tweak_label; } ui_mod_label_form_t;

/* Presentation only: canonical labels in, compact glyph forms out. */
static const ui_mod_label_form_t g_ui_mod_label_forms[] = {
    {"Level","Lvl",0},{"LEVEL","Lvl",0},{"Send1","Snd1",0},{"Send2","Snd2",0},{"Send3","Snd3",0},
    {"MORPH","Mrph",0},{"Morph","Mrph",0},{"Cutoff","Cutf",0},{"CUTOFF","Cutf",0},
    {"Resonance","Res","Reson"},{"EG Amt","EgAm",0},{"KeyTrk","KeyT",0},{"EnvRst","ERst",0},{"EnvDly","EDly",0},
    {"Mode","Mode",0},{"MODEL","Modl",0},{"Model","Modl",0},{"P.MOD1","PMod",0},{"P.MOD2","PMod",0},
    {"A MOD","AMod",0},{"AMOD","AMod",0},{"DETUNE","Detu",0},{"Detune","Detu",0},{"DRIFT","Drft",0},
    {"RESET","Rst",0},{"Reset","Rst",0},{"Amount","Amt",0},{"AMOUNT","Amt",0},{"Input","Inpt",0},{"INPUT","Inpt",0},
    {"Depth","Dpth",0},{"DEPTH","Dpth",0},{"Delay","Dly",0},{"DELAY","Dly",0},{"Feedback","Fdbk",0},{"FEEDBACK","Fdbk",0},
    {"Rate","Rate",0},{"RATE","Rate",0},{"Shape","Shap",0},{"SHAPE","Shap",0},{"Trig","Trig",0},{"TRIG","Trig",0},
    {"Phase","Phas",0},{"PHASE","Phas",0},{"Attack","Atk",0},{"ATTACK","Atk",0},{"Decay","Dcy",0},{"DECAY","Dcy",0},
    {"Sustain","Sus",0},{"SUSTAIN","Sus",0},{"Release","Rel",0},{"RELEASE","Rel",0},{"Start","Strt",0},{"START","Strt",0},
    {"Src BPM","SBpm",0},{"Sync Len","SLen",0},{"PlayMode","Play",0},{"XFade","Xfad",0},{"Pitch","Ptch",0},{"PITCH","Ptch",0},
    {"Ratio","Rati",0},{"RATIO","Rati",0},{"Bright","Brit",0},{"BRIGHT","Brit",0},{"ENV MOD","EMod",0},{"VCF RATE","Rate",0},
    {"Noise","Nois",0},{"NOISE","Nois",0},{"Density","Dens",0},{"NWidth","NWid",0},{"NDepth","NDep",0},
    {"ToneDrv","TDrv",0},{"SinTri","STri",0},{"Sub Mix","SubM",0},{"Balance","Bal",0},{"FShift","FShf",0},
    {"Spread","Sprd",0},{"Interp","Intr",0},{"FltMix","FMix",0},{"Steps","Step",0},{"Grain","Gran",0},
    {"Speed","Spd",0},{"DRIVE","Driv",0},{"POINT","Pnt",0},{"SPEED","Spd",0},{"FOLD","Fold",0},{"BIAS","Bias",0},
    {"OSC1 LVL","Lvl","Osc1 Lvl"},{"OSC2 LVL","Lvl","Osc2 Lvl"},{"OSC3 LVL","Lvl","Osc3 Lvl"},
    {"Color","Colr",0},{"Timbre","Tmbr",0},{"Stretch","Strc",0},{"POSITION","Pos",0},{"SPREAD","Sprd",0},
    {"ACCENT","Acc",0},{"SLIDE","Slde",0},{"DETAIL","Detl",0},{"METAL","Mtal",0},{"Offset","Ofst",0},
    {"Width","Widt",0},{"P ENV","PEnv",0},{"P TIME","PTim",0},{"ENV FLT","EFlt",0},{"ENV VCA","EVca",0},
    {"Saw Shape","Saw","SawShape"},{"Formant 1","Frm1","Formant1"},{"Formant 2","Frm2","Formant2"},
    {"Formant X","FrmX","FormantX"},{"Formant Y","FrmY","FormantY"},{"OSC DETUNE","Detu","OscDetu"}
};

static const char *ui_page_template_mod_context(param_id_t id)
{
    if ((id >= PARAM_LFO1_RATE) && (id <= PARAM_LFO1_PHASE)) return "LFO1";
    if ((id >= PARAM_LFO2_RATE) && (id <= PARAM_LFO2_PHASE)) return "LFO2";
    if ((id >= PARAM_LFO3_RATE) && (id <= PARAM_LFO3_PHASE)) return "LFO3";
    if (((id >= PARAM_FILTER_MORPH) && (id <= PARAM_FILTER_ENVDLY)) || (id == PARAM_FILTER_MODE)) return "Fltr";
    if ((id >= PARAM_VCA_ATTACK) && (id <= PARAM_VCA_RELEASE)) return "Env";
    if ((id >= PARAM_ENV3_ATTACK) && (id <= PARAM_ENV_RETRIG_VCA)) return "Env";
    if ((id >= PARAM_FM_ENV_ATTACK) && (id <= PARAM_FM_ENV_RELEASE)) return "Env";
    if (((id >= PARAM_MIX_LEVEL) && (id <= PARAM_MIX_SEND2)) || (id == PARAM_MIX_SEND3)) return "Mix";
    if ((id == PARAM_GROUP_FX_A_LEVEL) || (id == PARAM_GROUP_FX_B_LEVEL)) return "Mix";
    if ((id >= PARAM_AUDIO_FX_P1) && (id <= PARAM_AUDIO_FX_P3)) return "FX1";
    if ((id >= PARAM_AUDIO_FX_B_P1) && (id <= PARAM_AUDIO_FX_B_P3)) return "FX2";
    if ((id >= PARAM_SAMPLER_CLIP_SOURCE_BPM) && (id <= PARAM_SAMPLER_CLIP_GRAIN)) return "Strm";
    if ((id >= PARAM_LOOPER_XFADE) && (id <= PARAM_LOOPER_GRAIN)) return "Loop";
    if ((id >= PARAM_SAMPLER_GAIN) && (id <= PARAM_SAMPLER_SLICE_COUNT)) return "Samp";
    if ((id == PARAM_SAMPLER_MULTI_LOOP) || (id == PARAM_SAMPLER_LOOP_START)) return "Mult";
    if ((id >= PARAM_TB303_WAVE) && (id <= PARAM_TB303_VCF_RATE)) return "303";
    if ((id >= PARAM_DRUM_MD_MODEL) && (id <= PARAM_DRUM_MD_P8)) return "Drum";
    if ((id >= PARAM_WAVE_OSC1_POS) && (id <= PARAM_WAVE_OSC1_LEN)) return "Osc1";
    if ((id >= PARAM_WAVE_OSC2_POS) && (id <= PARAM_WAVE_OSC2_LEN)) return "Osc2";
    if ((id >= PARAM_WAVE_VOLUME) && (id <= PARAM_WAVE_DETUNE)) return "Wave";
    switch(id) {
        case PARAM_PRISM_OSC1_MODEL: case PARAM_PRISM_PITCH_MOD1: case PARAM_PRISM_OSC1_PARAM1:
        case PARAM_PRISM_OSC1_AMOD: case PARAM_PRISM_OSC1_PARAM2: case PARAM_PRISM_PHASE1_RESET: return "Osc1";
        case PARAM_PRISM_OSC2_MODEL: case PARAM_PRISM_PITCH_MOD2: case PARAM_PRISM_OSC2_PARAM1:
        case PARAM_PRISM_OSC2_AMOD: case PARAM_PRISM_OSC2_PARAM2: return "Osc2";
        case PARAM_PRISM_VOLUME: case PARAM_PRISM_TUNE: return "Osc1";
        case PARAM_PRISM_BALANCE: case PARAM_PRISM_DETUNE: case PARAM_PRISM_DRIFT: return "Prsm";
        case PARAM_STACK_OSC1_LEVEL: case PARAM_STACK_OSC1_MODEL: case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC1_TIMBRE: case PARAM_STACK_OSC1_COLOR: return "Osc1";
        case PARAM_STACK_OSC2_LEVEL: case PARAM_STACK_OSC2_MODEL: case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC2_TIMBRE: case PARAM_STACK_OSC2_COLOR: return "Osc2";
        case PARAM_STACK_OSC3_LEVEL: case PARAM_STACK_OSC3_MODEL: case PARAM_STACK_OSC3_TUNE:
        case PARAM_STACK_OSC3_TIMBRE: case PARAM_STACK_OSC3_COLOR: return "Osc3";
        case PARAM_STACK_NOISE_LEVEL: case PARAM_STACK_OSC_DETUNE: case PARAM_STACK_PHASE_RESET: return "Stck";
        default: break;
    }
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST)) {
        static const char *const ops[]={"OP1","OP2","OP3","OP4","OP5","OP6"};
        return ops[((uint16_t)id-(uint16_t)PARAM_FM_OPERATOR_FIRST)/PARAM_FM_OPERATOR_PARAM_COUNT];
    }
    if (param_id_is_fm_public(id) != 0U) return "FM";
    return "Param";
}

static const ui_mod_label_form_t *ui_page_template_mod_find_form(const char *label)
{
    for (uint32_t i=0U;i<(uint32_t)(sizeof(g_ui_mod_label_forms)/sizeof(g_ui_mod_label_forms[0]));++i)
        if (strcmp(label,g_ui_mod_label_forms[i].label)==0) return &g_ui_mod_label_forms[i];
    return 0;
}

uint8_t ui_page_template_mod_project_destination(uint8_t track,uint16_t index,
    ui_mod_destination_projection_t *out)
{
    if(out==0)return 0U;
    *out=(ui_mod_destination_projection_t){.param=PARAM_COUNT};
    if(index==0U){(void)snprintf(out->widget_line1,sizeof(out->widget_line1),"Mod");(void)snprintf(out->widget_line2,sizeof(out->widget_line2),"Off");(void)snprintf(out->tweak_label,sizeof(out->tweak_label),"Off");return 1U;}
    uint8_t target=0U;param_id_t id=PARAM_COUNT;param_registry_resolved_track_param_t resolved;
    if(!mod_destination_address_resolve(mod_destination_catalog_address_from_index(track,index),&target,&id)
        ||!param_registry_resolve_track_param(target,id,&resolved)||!resolved.applicable||resolved.label==0)return 0U;
    const char *context=ui_page_template_mod_context(id),*label=resolved.label;const size_t n=strlen(context);
    if(strncmp(label,context,n)==0&&label[n]==' ')label+=n+1U;
    const ui_mod_label_form_t *form=ui_page_template_mod_find_form(label);const char *short_label=form?form->short_label:label;
    (void)snprintf(out->widget_line1,sizeof(out->widget_line1),"%.4s",context);
    (void)snprintf(out->widget_line2,sizeof(out->widget_line2),"%.4s",short_label);
    if((id>=PARAM_LFO1_RATE)&&(id<=PARAM_LFO3_PHASE))(void)snprintf(out->tweak_label,sizeof(out->tweak_label),"%.4s%.4s",context,short_label);
    else (void)snprintf(out->tweak_label,sizeof(out->tweak_label),"%.8s",(form&&form->tweak_label)?form->tweak_label:label);
    out->param=id;return 1U;
}

static const ui_template_family_t g_ui_template_mod_family_main = {
    .family_title = "MOD 1/2",
    .nav_labels = { "MATRIX", "LFO 1", "LFO 2", "LFO 3" },
    .subpages = {
        {
            .title = "MATRIX",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "LFO 1",
            .param_bank = { .params = { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_PHASE, PARAM_LFO1_TRIG } },
        },
        {
            .title = "LFO 2",
            .param_bank = { .params = { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_PHASE, PARAM_LFO2_TRIG } },
        },
        {
            .title = "LFO 3",
            .param_bank = { .params = { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_PHASE, PARAM_LFO3_TRIG } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mod_family_ops = {
    .family_title = "MOD 2/2",
    .nav_labels = { "MULTI", "SLEW", "-", "-" },
    .subpages = {
        {
            .title = "MULTI",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "SLEW",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_mod_resolve_family(void)
{
    if (g_ui_template_mod_subset != 0U)
    {
        return &g_ui_template_mod_family_ops;
    }

    return ui_template_family_resolve_effective_for_track(UI_TEMPLATE_FAMILY_MOD,
                                                           ui_get_active_lane(),
                                                           UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT);
}

static uint8_t ui_page_template_mod_is_lfo_param(param_id_t id, mod_lfo_param_t param)
{
    static const param_id_t lfo_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
        { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
        { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
        { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
    };

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if (lfo_params[lfo][param] == id)
        {
            return 1U;
        }
    }

    return 0U;
}

static ui_template_custom_widget_kind_t ui_page_template_mod_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    (void)subpage;

    if ((slot == 0U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_RATE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE;
    }
    if (((slot == 1U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_SHAPE) != 0U))
            || ((slot == 2U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uiw_widget_type_t ui_page_template_mod_pick_widget(uint8_t slot,
                                                          param_id_t id,
                                                          const char *value_label,
                                                          uiw_widget_type_t suggested_widget)
{
    (void)slot;
    (void)value_label;

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_TRIG) != 0U)
    {
        return UIW_WIDGET_ENUM_TEXT;
    }

    return suggested_widget;
}

static uint8_t ui_page_template_mod_param_text(uint8_t slot,
                                               param_id_t id,
                                               float value,
                                               char *out_name,
                                               uint32_t out_name_len,
                                               char *out_value,
                                               uint32_t out_value_len)
{
    (void)slot;
    (void)value;
    if ((out_name == NULL) || (out_name_len == 0U))
    {
        return 0U;
    }

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)
    {
        uint8_t lfo_index = MOD_LFO_COUNT_PER_TRACK;
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            if (g_ui_template_mod_family_main.subpages[lfo + 1U].param_bank.params[2] == id)
            {
                lfo_index = lfo;
                break;
            }
        }
        const uint8_t is_rnd = ((lfo_index < MOD_LFO_COUNT_PER_TRACK)
                && (mod_lfo_v1_shape_is_random(ui_get_active_lane(), lfo_index) != 0U)) ? 1U : 0U;
        (void)snprintf(out_name, out_name_len, "%s", (is_rnd != 0U) ? "Slew" : "Phase");
        if ((out_value != NULL) && (out_value_len > 0U) && (is_rnd != 0U))
        {
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned int)((value * 100.0f / 360.0f) + 0.5f));
        }
        return 1U;
    }

    return 1U;
}

static const char *ui_page_template_mod_source_label(uint8_t source)
{
    static const char *const labels[]={"OFF","LFO1","LFO2","LFO3","ENV1","ENV2","ENV3","MLT1","MLT2","SLW1","SLW2"};
    return (source<MOD_MATRIX_SOURCE_COUNT)?labels[source]:"OFF";
}

static ui_template_custom_widget_kind_t ui_page_template_mod_pick_virtual_widget(
    uint8_t slot, const ui_template_subpage_t *subpage)
{
    if ((subpage == NULL) || (slot >= 4U)) return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    if ((g_ui_template_mod_subset == 0U)
            && (g_ui_template_mod_state.active_subpage == 0U))
    {
        static const ui_template_custom_widget_kind_t matrix_widgets[] = {
            UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SLOT,
            UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE,
            UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST,
            UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH
        };
        return matrix_widgets[slot];
    }
    if ((g_ui_template_mod_subset != 0U)
            && (g_ui_template_mod_state.active_subpage == 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE;
    }
    if ((g_ui_template_mod_subset != 0U)
            && (g_ui_template_mod_state.active_subpage == 1U))
    {
        return ((slot & 1U) == 0U)
            ? UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE
            : UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_mod_virtual_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar)
{
    (void)frame_ctx;
    if ((out_value == NULL) || (out_bipolar == NULL) || (slot >= 4U)) return 0U;
    const uint8_t track = ui_get_active_lane();
    *out_bipolar = 0U;
    if ((g_ui_template_mod_subset == 0U)
            && (g_ui_template_mod_state.active_subpage == 0U))
    {
        if (slot == 0U) return mod_matrix_get_selected_slot(track, out_value);
        if (slot == 1U) return mod_matrix_get_selected_slot_source(track, out_value);
        if (slot == 2U) return mod_matrix_get_selected_slot_destination_index(track, out_value);
        *out_bipolar = 1U;
        return mod_matrix_get_selected_slot_depth(track, out_value);
    }
    if ((g_ui_template_mod_subset != 0U)
            && (g_ui_template_mod_state.active_subpage == 0U))
    {
        return mod_matrix_get_multi_source(track, (uint8_t)(slot >> 1U),
                                           (uint8_t)(slot & 1U), out_value);
    }
    if ((g_ui_template_mod_subset != 0U)
            && (g_ui_template_mod_state.active_subpage == 1U))
    {
        const uint8_t op = (uint8_t)(slot >> 1U);
        if ((slot & 1U) == 0U)
            return mod_matrix_get_slew_source(track, op, out_value);
        return mod_matrix_get_slew_amount(track, op, out_value);
    }
    return 0U;
}

static uint8_t ui_page_template_mod_virtual_slot_text(uint8_t slot,char*out_name,
    uint32_t out_name_len,char*out_value,uint32_t out_value_len)
{
    const uint8_t track=ui_get_active_lane();float value=0.0f;
    if(g_ui_template_mod_subset==0U&&g_ui_template_mod_state.active_subpage==0U){
        if(slot==0U){if(!mod_matrix_get_selected_slot(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"SLOT");(void)snprintf(out_value,out_value_len,"%u",(unsigned)((uint8_t)value+1U));return 1U;}
        if(slot==1U){if(!mod_matrix_get_selected_slot_source(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"SOURCE");(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));return 1U;}
        if(slot==2U){ui_mod_destination_projection_t projection;if(!mod_matrix_get_selected_slot_destination_index(track,&value)||!ui_page_template_mod_project_destination(track,(uint16_t)value,&projection))return 0U;(void)snprintf(out_name,out_name_len,"%s",((projection.param<PARAM_COUNT)&&ui_param_is_user_tweak_active(slot,projection.param))?projection.tweak_label:"DEST");(void)snprintf(out_value,out_value_len,"%s / %s",projection.widget_line1,projection.widget_line2);return 1U;}
        if(slot==3U){if(!mod_matrix_get_selected_slot_depth(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"DEPTH");(void)snprintf(out_value,out_value_len,"%+.0f",(double)value);return 1U;}}
    if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==0U){const uint8_t op=(uint8_t)(slot>>1U),input=(uint8_t)(slot&1U);if(!mod_matrix_get_multi_source(track,op,input,&value))return 0U;(void)snprintf(out_name,out_name_len,"M%u%c",(unsigned)(op+1U),input?'B':'A');(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));return 1U;}
    if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==1U){const uint8_t op=(uint8_t)(slot>>1U);if((slot&1U)==0U){if(!mod_matrix_get_slew_source(track,op,&value))return 0U;(void)snprintf(out_name,out_name_len,"S%u SRC",(unsigned)(op+1U));(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));}else{if(!mod_matrix_get_slew_amount(track,op,&value))return 0U;(void)snprintf(out_name,out_name_len,"S%u AMT",(unsigned)(op+1U));(void)snprintf(out_value,out_value_len,"%u%%",(unsigned)(value*100.0f+0.5f));}return 1U;}
    return 0U;
}

static uint8_t ui_page_template_mod_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((encoder >= 4U) || (delta == 0)) return 0U;
    const uint8_t track = ui_get_active_lane();
    float value = 0.0f;
    const float step = (delta > 0) ? 1.0f : -1.0f;
    if ((g_ui_template_mod_subset == 0U) && (g_ui_template_mod_state.active_subpage == 0U))
    {
        if (encoder == 0U) { (void)mod_matrix_get_selected_slot(track, &value); (void)mod_matrix_set_selected_slot(track, value + step); }
        else if (encoder == 1U) { (void)mod_matrix_get_selected_slot_source(track, &value); (void)mod_matrix_set_selected_slot_source(track, value + step); }
        else if (encoder == 2U) { ui_mod_destination_projection_t projection; (void)mod_matrix_get_selected_slot_destination_index(track, &value); (void)mod_matrix_set_selected_slot_destination_index(track, value + step); if (mod_matrix_get_selected_slot_destination_index(track, &value) && ui_page_template_mod_project_destination(track, (uint16_t)value, &projection) && (projection.param < PARAM_COUNT)) ui_param_note_user_tweak(encoder, projection.param); }
        else { (void)mod_matrix_get_selected_slot_depth(track, &value); (void)mod_matrix_set_selected_slot_depth(track, value + (float)delta); }
        return 1U;
    }
    if ((g_ui_template_mod_subset != 0U) && (g_ui_template_mod_state.active_subpage == 0U))
    {
        const uint8_t op = (uint8_t)(encoder >> 1U);
        const uint8_t input = (uint8_t)(encoder & 1U);
        (void)mod_matrix_get_multi_source(track, op, input, &value);
        (void)mod_matrix_set_multi_source(track, op, input, value + step);
        return 1U;
    }
    if ((g_ui_template_mod_subset != 0U) && (g_ui_template_mod_state.active_subpage == 1U))
    {
        const uint8_t op = (uint8_t)(encoder >> 1U);
        if ((encoder & 1U) == 0U) { (void)mod_matrix_get_slew_source(track, op, &value); (void)mod_matrix_set_slew_source(track, op, value + step); }
        else { (void)mod_matrix_get_slew_amount(track, op, &value); (void)mod_matrix_set_slew_amount(track, op, value + (float)delta * 0.01f); }
        return 1U;
    }
    return 0U;
}

static ui_template_page_state_t g_ui_template_mod_state = {
    .family = 0,
    .family_resolver = ui_page_template_mod_resolve_family,
    .widget_picker = ui_page_template_mod_pick_widget,
    .custom_widget_picker = ui_page_template_mod_pick_custom_widget,
    .virtual_custom_widget_picker = ui_page_template_mod_pick_virtual_widget,
    .param_text = ui_page_template_mod_param_text,
    .virtual_slot_text = ui_page_template_mod_virtual_slot_text,
    .virtual_slot_value = ui_page_template_mod_virtual_slot_value,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mod_open_primary(void)
{
    g_ui_template_mod_subset = 0U;
    g_ui_template_mod_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_mod_state, 0U);
}

void ui_page_template_mod_toggle_subset(void)
{
    g_ui_template_mod_subset = (g_ui_template_mod_subset == 0U) ? 1U : 0U;
    g_ui_template_mod_state.navigation_subset = g_ui_template_mod_subset;
    ui_navigation_restore_current_template_subpage();
}

void ui_page_template_mod_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; ++family)
    {
        const track_family_t track_family = (track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; ++type)
        {
            const track_type_t track_type = (track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_MOD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mod_family_main);
        }
    }
}

const ui_page_t g_ui_page_template_mod = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_encoder = ui_page_template_mod_handle_encoder,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_mod_state,
};
