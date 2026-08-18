#include "Param/param_registry_catalog.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_registry_apply_bindings.h"
#include "Mod/mod_lfo_v1.h"
#include "Sampler/sample_global_pool.h"
#include "Seq/seq_types.h"
#include "Seq/seq_division_catalog.h"
#include "ui_core.h"
#include <stddef.h>
static const char *const g_modfx_model_labels[] = {
    "Off", "Q.M", "F.M", "Q.S", "F.S",
    "Daisy", "Dimension", "Teensy", "Junologue", NULL
};
#define PARAM_DESC_EX(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels, _apply) \
    [(_id)] = {                                                                  \
        .id = (_id),                                                             \
        .name = (_name),                                                         \
        .type = (_type),                                                         \
        .min = (_min),                                                           \
        .max = (_max),                                                           \
        .step = (_step),                                                         \
        .default_value = (_default),                                             \
        .display_type = (_display),                                              \
        .unit = (_unit),                                                         \
        .labels = (_labels),                                                     \
        .apply = (_apply),                                                       \
    }

#define PARAM_DESC(_id, _name, _type, _min, _max, _step, _default, _unit, _apply)                           \
    PARAM_DESC_EX((_id),                                                                                      \
                  (_name),                                                                                    \
                  (_type),                                                                                    \
                  (_min),                                                                                     \
                  (_max),                                                                                     \
                  (_step),                                                                                    \
                  (_default),                                                                                 \
                  (((_type) == PARAM_TYPE_BOOL) ? PARAM_DISPLAY_BOOL                                         \
                                                 : (((_type) == PARAM_TYPE_ENUM) ? PARAM_DISPLAY_ENUM        \
                                                                            : PARAM_DISPLAY_FLOAT)),         \
                  (_unit),                                                                                    \
                  NULL,                                                                                       \
                  (_apply))

static const char *const g_bool_labels[] = {"Off", "On", NULL};
static const char *const g_comp_model_labels[] = {"OFF", "DELUGE", "BRICK", NULL};
static const char *const g_comp_detect_labels[] = {"PEAK", "RMS", NULL};
static const char *const g_stack_reset_labels[] = {"FREE", "RESET", NULL};
static const char *const g_wave_pos_update_labels[] = {"FULL", "8", "16", "32", NULL};
static const char *const g_delay_time_labels[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1D", "1 bar", NULL};
static const char *const g_delay_type_labels[] = {"CLASSIC", "DUAL", NULL};
static const char *const g_delay_mode_labels[] = {"Normal", "PingPong", "Tap", "ClassicPP", NULL};
static const char *const g_reverb_delays_labels[] = {"DELUGE", "TBD", NULL};
static const char *const g_sampler_mode_labels[] = {"Shot", "RevShot", "Loop", "PingPong", NULL};
static const char *const g_sampler_slice_count_labels[] = {"Off", "2", "4", "8", "16", "32", "64", NULL};
static const char *const g_sampler_clip_sync_length_labels[] = {"Off", "1 bar", "2 bars", "4 bars", "Auto", NULL};
static const char *const g_sampler_clip_play_mode_labels[] = {"Gate", "Launch", NULL};
static const char *const g_sampler_clip_stretch_mode_labels[] = {"Off", "Speed", "Shifter", NULL};
static const char *const g_sampler_clip_grain_labels[] = {"384", "512", "768", "1024", "1536", "2048", NULL};
static const char *const g_sampler_clip_hop_labels[] = {"32", "64", "96", "128", "256", "512", NULL};
static const char *const g_sampler_clip_search_labels[] = {"0", "4", "8", "12", "16", NULL};
static const char *const g_looper_arm_labels[] = {"Off", "Rec", "Overd", NULL};
static const char *const g_looper_len_labels[] = {"Free", "1", "2", "4", "8", "16", NULL};
static const char *const g_looper_play_labels[] = {"Off", "Auto", NULL};


static const char *const g_prism_edit_labels[] = {"CSAW", "Morph", "SawSq", "SinTri", "Buzz", "SqSub", "SawSub", "SqSync", "SawSync", "TriSaw", "TriSq", "TriTri", "TriSin", "Ring", "Swarm", "Toy", "Vosim", "Vowel", "FOF", "FM", "FB FM", "Chaos", "WTbl", "WMap", "WLine", "WPara", "Noise", "TwinPk", "Clock", "Cloud", "Particle", "DigiMod", "????", NULL};
_Static_assert((sizeof(g_prism_edit_labels) / sizeof(g_prism_edit_labels[0])) - 1U == BRICK6_PRISM_MODEL_COUNT,
               "Prism labels and active model count must stay aligned");
static const char *const g_stack_model_labels[] = {"SINE", "TRI", "SQUARE", "SAW", "SHAPE", "TRIPLE SAW", NULL};
static const char *const g_md_model_labels[] = {"TRX-BD", "TRX-SD", "TRX-CH", "EFM-BD", "EFM-SD", "EFM-CB", NULL};
static const char *const g_track_family_labels[] = {"Off", "-", "-", "-", "Synth", "Drum", "MIDI", "Sampler", "External", NULL};
static const char *const g_external_input_labels[] = {"Input 1", NULL};
static const char *const g_track_midi_source_labels[] = {"INT", "EXT", "ALL", NULL};
static const char *const g_cfg_start_labels[] = {"DEFAULT", "TRIG", "ROLL 1/4", "ROLL 1/2", "ROLL 1", NULL};
static const char *const g_cfg_sync_labels[] = {"INT", "MidiEXT", "UsbEXT", NULL};
static const char *const g_cfg_rec_len_labels[] = {"Overdub", "Pattern", NULL};
static const char *const g_kbd_root_labels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
static const char *const g_kbd_scale_labels[] = {"Major", "NatMin", "Dorian", "Mixoly", "PntMaj", "PntMin", "Chrom", NULL};
static const char *const g_kbd_note_order_labels[] = {"Natural", "Fifths", NULL};
static const char *const g_midi_fx_model_labels[] = {"OFF", "ARP", NULL};
/* ID 4 is intentionally a retired hole: persisted COMP values resolve OFF. */
static const char *const g_audio_fx_model_labels[] = {"OFF", "LOFI", "FOLD", "DRIVE", "-", "POINT", "-", "-", "SUB", "-", "RING", "SUB LIGHT", NULL};
static const char *const g_filter_mode_labels[] = {"OFF", "LOW", "HIGH", NULL};
static const char *const g_fm_operator_mode_labels[] = {"RATIO", "FIXED", NULL};
static const char *const g_fm_algorithm_labels[] = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16",
    "17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32",NULL
};
static const char *const g_fm_operator_labels[] = {"1", "2", "3", "4", "5", "6", NULL};
static const char *const g_midi_fx_style_labels[] = {"ORDER", "UP", "DOWN", "UP/DOWN", "RANDOM", NULL};
static const char *const g_lfo_shape_labels[] = {"SIN", "TRI", "SAW", "SQR", "RND", "SIN+", "TRI+", "SQR+", "RSAW", NULL};
static const char *const g_lfo_trig_labels[] = {
    "FREE", "TRIG", "HOLD", "ONE", "P.Trig", "P.Hold", "P.One", NULL
};
static const char *const g_mod_matrix_source_labels[] = {"Off", "LFO 1", "LFO 2", "LFO 3", "env flt", "env vca", "env mod", "MULT1", "MULT2", "SLEW1", "SLEW2", NULL};

#define PARAM_DESC_LFO(_rate, _shape, _trig, _phase, _apply_rate, _apply_shape, _apply_trig, _apply_phase) \
    PARAM_DESC_EX((_rate), "Rate", PARAM_TYPE_FLOAT, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, (_apply_rate)), \
    PARAM_DESC_EX((_shape), "Shape", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_shape_labels, (_apply_shape)), \
    PARAM_DESC_EX((_trig), "Trig", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_trig_labels, (_apply_trig)), \
    PARAM_DESC_EX((_phase), "Phase", PARAM_TYPE_FLOAT, 0.0f, 360.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "deg", NULL, (_apply_phase))

const param_desc_t param_registry[PARAM_COUNT] = {
    PARAM_DESC(PARAM_MODFX_BANK_DELUGE_MONO_AB, "ModFX DM AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8197.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DELUGE_MONO_CD, "ModFX DM CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8217.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DELUGE_STEREO_AB, "ModFX DS AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8197.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DELUGE_STEREO_CD, "ModFX DS CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8217.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DAISY_AB, "ModFX DA AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 15805.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DAISY_CD, "ModFX DA CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 12185.0f, "", NULL),

    PARAM_DESC(PARAM_MODFX_BANK_DIMENSION_AB, "ModFX DI AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8197.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_DIMENSION_CD, "ModFX DI CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8217.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_TEENSY_AB, "ModFX TE AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 2304.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_TEENSY_CD, "ModFX TE CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 0.0f, "", NULL),

    PARAM_DESC(PARAM_MODFX_BANK_JUNOLOGUE_AB, "ModFX JU AB", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MODFX_BANK_JUNOLOGUE_CD, "ModFX JU CD", PARAM_TYPE_INT, 0.0f, 16383.0f, 1.0f, 8192.0f, "", NULL),
    PARAM_DESC_EX(PARAM_RESERVED_012, "Reserved 012", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_013, "Reserved 013", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_MIX_MUTE, "Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_015, "Reserved 015", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_CFG_POLY_VOICES, "VOICES", PARAM_TYPE_INT, 1.0f, 8.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_CFG_POLY_SPREAD, "SPREAD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_RESERVED_018, "Reserved 018", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_019, "Reserved 019", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_020, "Reserved 020", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_MODEL, "MODEL", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_md_model_labels, NULL),

    PARAM_DESC_EX(PARAM_DRUM_MD_P1, "P1", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P2, "P2", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P3, "P3", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 32.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P4, "P4", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 48.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P5, "P5", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 48.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P6, "P6", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P7, "P7", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 48.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_MD_P8, "P8", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_RESERVED_030, "Reserved 030", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_031, "Reserved 031", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_032, "Reserved 032", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_033, "Reserved 033", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_034, "Reserved 034", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_035, "Reserved 035", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_036, "Reserved 036", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_037, "Reserved 037", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC(PARAM_MIX_SEND0_FX, "Send0 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send0_fx),
    PARAM_DESC(PARAM_MIX_SEND1_FX, "Send1 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send1_fx),
    PARAM_DESC(PARAM_MIX_LEVEL, "Level", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_PAN, "Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND1, "Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND2, "Send2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND3, "Send3", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC_EX(PARAM_MODFX_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_modfx_model_labels, apply_modfx_model),
    PARAM_DESC(PARAM_MODFX_RATE, "Rate", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_modfx_rate),
    PARAM_DESC(PARAM_MODFX_DEPTH, "Depth", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_modfx_depth),
    PARAM_DESC(PARAM_MODFX_FEEDBACK, "Feedback", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_modfx_feedback),
    PARAM_DESC(PARAM_MODFX_OFFSET, "Offset", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_modfx_offset),

    PARAM_DESC_EX(PARAM_BUS_COMP_THRESHOLD_DB, "THRESH", PARAM_TYPE_FLOAT, -48.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_threshold),
    PARAM_DESC_EX(PARAM_BUS_COMP_RATIO, "RATIO", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_bus_comp_ratio),
    PARAM_DESC_EX(PARAM_BUS_COMP_ATTACK_INDEX, "ATTACK", PARAM_TYPE_FLOAT, 0.0001f, 0.1f, 0.0001f, 0.01f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_bus_comp_attack_index),
    PARAM_DESC_EX(PARAM_BUS_COMP_RELEASE_INDEX, "RELEASE", PARAM_TYPE_FLOAT, 0.02f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_bus_comp_release_index),
    PARAM_DESC_EX(PARAM_BUS_COMP_MAKEUP_DB, "MAKEUP", PARAM_TYPE_FLOAT, 0.0f, 18.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_AUTO_MAKEUP, "BusComp AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_bus_comp_auto_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_DRYWET, "MIX", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_bus_comp_drywet),
    PARAM_DESC_EX(PARAM_BUS_COMP_HPF_HZ, "SC HPF", PARAM_TYPE_FLOAT, 0.0f, 200.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "Hz", NULL, apply_bus_comp_hpf),

    PARAM_DESC_EX(PARAM_EQ_LOW_DB, "EQ Low", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_low_db),
    PARAM_DESC_EX(PARAM_EQ_MID_DB, "EQ Mid", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_mid_db),
    PARAM_DESC_EX(PARAM_EQ_HIGH_DB, "EQ High", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_high_db),

    PARAM_DESC_EX(PARAM_SAT_TONE, "Sat Tone", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_tone),
    PARAM_DESC_EX(PARAM_SAT_BIAS, "Sat Bias", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_bias),
    PARAM_DESC_EX(PARAM_SAT_DRIVE, "Sat Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_drive),
    PARAM_DESC_EX(PARAM_SAT_MIX, "Sat Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_mix),

    PARAM_DESC_EX(PARAM_FILTER_MORPH, "MORPH", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_filter_morph),
    PARAM_DESC_EX(PARAM_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 0.01f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_cutoff),
    PARAM_DESC_EX(PARAM_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_resonance),
    PARAM_DESC_EX(PARAM_FILTER_EG_AMT, "EG Amt", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_eg_amount),
    PARAM_DESC_EX(PARAM_FILTER_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_attack),
    PARAM_DESC_EX(PARAM_FILTER_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_decay),
    PARAM_DESC_EX(PARAM_FILTER_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_sustain),
    PARAM_DESC_EX(PARAM_FILTER_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_release),
    PARAM_DESC_EX(PARAM_FILTER_KEYTRK, "KeyTrk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_keytrack),
    PARAM_DESC_EX(PARAM_FILTER_ENVRST, "EnvRst", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_filter_env_reset),
    PARAM_DESC_EX(PARAM_FILTER_ENVDLY, "EnvDly", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_env_delay),
    PARAM_DESC_EX(PARAM_RESERVED_FILTER_SLOT_1, "Reserved", PARAM_TYPE_INT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_FILTER_SLOT_2, "Reserved", PARAM_TYPE_INT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_FILTER_SLOT_3, "Reserved", PARAM_TYPE_INT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FILTER_DRIVE, "Drive", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_drive),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_BITS, "Bits", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_bits),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE, "Rate", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE2, "Rate2", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate2),
    PARAM_DESC_EX(PARAM_VCA_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_CFG_TRACK, "Track", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_track_family_labels, apply_cfg_track),
    PARAM_DESC_EX(PARAM_CFG_TRACK_TYPE, "Type", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_cfg_track_type),
    PARAM_DESC_EX(PARAM_CFG_MIDI_CH, "Midi CH", PARAM_TYPE_INT, 1.0f, 16.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, apply_cfg_midi_ch),
    PARAM_DESC_EX(PARAM_CFG_MIDI_SRC, "Midi Src", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_track_midi_source_labels, apply_cfg_midi_src),
    PARAM_DESC_EX(PARAM_CFG_START, "START", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_start_labels, apply_cfg_start),
    PARAM_DESC_EX(PARAM_CFG_TEMPO, "Tempo", PARAM_TYPE_FLOAT, 40.0f, 300.0f, 0.1f, 120.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_cfg_tempo),
    PARAM_DESC_EX(PARAM_CFG_SYNC, "Sync", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_sync_labels, apply_cfg_sync),
    PARAM_DESC_EX(PARAM_CFG_REC_LEN, "Len", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_rec_len_labels, apply_cfg_rec_len),
    PARAM_DESC_EX(PARAM_CFG_METRO, "METRO", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_cfg_metro),
    PARAM_DESC_EX(PARAM_SEQ_LENGTH, "LENGTH", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, (float)SEQ_DEFAULT_LENGTH_STEPS, PARAM_DISPLAY_INT, "", NULL, apply_seq_length),
    PARAM_DESC_EX(PARAM_SEQ_DIV, "DIV", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", seq_division_track_labels, apply_seq_div),
    PARAM_DESC_EX(PARAM_SEQ_QUANT, "QUANT", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_seq_quant),
    PARAM_DESC_EX(PARAM_SEQ_SWING, "SWING", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_seq_swing),

    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V5_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V5_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V5_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V5_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V6_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V6_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V6_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V6_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V7_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V7_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V7_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V7_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V8_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V8_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V8_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V8_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_KBD_ROOT, "Root", PARAM_TYPE_ENUM, 0.0f, 11.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_root_labels, apply_kbd_root),
    PARAM_DESC_EX(PARAM_KBD_SCALE, "Scale", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_scale_labels, apply_kbd_scale),
    PARAM_DESC_EX(PARAM_KBD_OMNICHORD, "Omni", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_omnichord),
    PARAM_DESC_EX(PARAM_KBD_NOTE_ORDER, "Order", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_note_order_labels, apply_kbd_note_order),
    PARAM_DESC_EX(PARAM_KBD_CHORD_OVERRIDE, "ChrOvr", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_chord_override),
    PARAM_DESC_EX(PARAM_KBD_MONO_LAST, "MonoLast", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_mono_last),


    PARAM_DESC_EX(PARAM_MASTER_GAIN, "Master Gain", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_master_gain),
    PARAM_DESC_EX(PARAM_POST_GAIN, "Post Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_post_gain),
    PARAM_DESC_EX(PARAM_OUTPUT_COMP, "Output Comp", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_output_comp),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 2.0f, 0.01f, 0.4f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_PITCH_SWEEP, "Sweep", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_SWEEP_DECAY, "Swp Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_ATTACK, "Attack", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_NOISE, "Noise", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_HARMONICS, "Harm", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_DRIVE, "Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_LFO(PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE, apply_lfo1_rate, apply_lfo1_shape, apply_lfo1_trig, apply_lfo1_phase),
    PARAM_DESC_LFO(PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE, apply_lfo2_rate, apply_lfo2_shape, apply_lfo2_trig, apply_lfo2_phase),
    PARAM_DESC_LFO(PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE, apply_lfo3_rate, apply_lfo3_shape, apply_lfo3_trig, apply_lfo3_phase),
    PARAM_DESC_EX(PARAM_MOD_MATRIX_SLOT, "Slot", PARAM_TYPE_INT, 0.0f, 7.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_mod_matrix_slot),
    PARAM_DESC_EX(PARAM_MOD_MATRIX_SOURCE, "Source", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_matrix_source),
    PARAM_DESC_EX(PARAM_MOD_MATRIX_DEST, "Dest", PARAM_TYPE_INT, 0.0f, (float)PARAM_COUNT, 1.0f, (float)PARAM_COUNT, PARAM_DISPLAY_INT, "", NULL, apply_mod_matrix_dest),
    PARAM_DESC_EX(PARAM_MOD_MATRIX_DEPTH, "Depth", PARAM_TYPE_BIPOLAR, -127.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_mod_matrix_depth),
    PARAM_DESC_EX(PARAM_MOD_MULTI_1_A, "M1A", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_multi_1_a),
    PARAM_DESC_EX(PARAM_MOD_MULTI_1_B, "M1B", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_multi_1_b),
    PARAM_DESC_EX(PARAM_MOD_MULTI_2_A, "M2A", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_multi_2_a),
    PARAM_DESC_EX(PARAM_MOD_MULTI_2_B, "M2B", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 6.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_multi_2_b),
    PARAM_DESC_EX(PARAM_MOD_SLEW_1_SOURCE, "S1SRC", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_slew_1_source),
    PARAM_DESC_EX(PARAM_MOD_SLEW_1_AMOUNT, "S1AMT", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mod_slew_1_amount),
    PARAM_DESC_EX(PARAM_MOD_SLEW_2_SOURCE, "S2SRC", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_mod_matrix_source_labels, apply_mod_slew_2_source),
    PARAM_DESC_EX(PARAM_MOD_SLEW_2_AMOUNT, "S2AMT", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mod_slew_2_amount),
    PARAM_DESC_EX(PARAM_ENV3_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_env3_attack),
    PARAM_DESC_EX(PARAM_ENV3_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 32.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_env3_decay),
    PARAM_DESC_EX(PARAM_ENV3_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_env3_sustain),
    PARAM_DESC_EX(PARAM_ENV3_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 32.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_env3_release),
    PARAM_DESC_EX(PARAM_ENV_RETRIG_FILTER, "ENV FLT", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_ENV_RETRIG_VCA, "ENV VCA", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_ENV_RETRIG_MOD, "ENV MOD", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),

    PARAM_DESC_EX(PARAM_MIX_REVERB_WET, "Wet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_wet),
    PARAM_DESC_EX(PARAM_MIX_REVERB_ROOM_SIZE, "ROOM SIZE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.6f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_room_size),
    PARAM_DESC_EX(PARAM_MIX_REVERB_DAMPING, "DAMPING", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.72f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_damping),
    PARAM_DESC_EX(PARAM_MIX_REVERB_WIDTH, "WIDTH", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_width),
    PARAM_DESC_EX(PARAM_MIX_REVERB_HPF, "HPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_hpf),
    PARAM_DESC_EX(PARAM_MIX_REVERB_LPF, "LPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_lpf),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TYPE, "TYPE", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_delay_type_labels, apply_mix_delay_type),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TIME, "TIME", PARAM_TYPE_ENUM, 0.0f, 12.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_delay_time_labels, apply_mix_delay_time),
    PARAM_DESC_EX(PARAM_MIX_DELAY_PINGPONG, "X", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_delay_pingpong),
    PARAM_DESC_EX(PARAM_MIX_DELAY_MODE, "MODE", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_delay_mode_labels, apply_mix_delay_mode),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TIME_R, "TIME_R", PARAM_TYPE_ENUM, 0.0f, 12.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_delay_time_labels, apply_mix_delay_time_r),
    PARAM_DESC_EX(PARAM_MIX_DELAY_WIDTH, "WID", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_mix_delay_width),
    PARAM_DESC_EX(PARAM_MIX_DELAY_FEEDBACK, "FDBK", PARAM_TYPE_FLOAT, 0.0f, 1.20f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_feedback),
    PARAM_DESC_EX(PARAM_MIX_DELAY_SPECTRAL_POSITION, "POSITION", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_spectral_position),
    PARAM_DESC_EX(PARAM_MIX_DELAY_SPECTRAL_WIDTH, "WIDTH", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_spectral_width),
    PARAM_DESC_EX(PARAM_MIX_DELAY_FBW, "FBW", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_mix_delay_feedback_width),
    PARAM_DESC_EX(PARAM_MIX_REVERB_DELAYS, "DELAYS", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_reverb_delays_labels, apply_mix_reverb_delays),
    PARAM_DESC_EX(PARAM_RESERVED_175, "Reserved 175", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MIX_DELAY_MOD, "MOD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_mod),
    PARAM_DESC_EX(PARAM_MIX_DELAY_MOD_RATE, "M.RATE", PARAM_TYPE_FLOAT, 0.01f, 12.0f, 0.01f, 0.25f, PARAM_DISPLAY_FLOAT, "Hz", NULL, apply_mix_delay_mod_rate),
    PARAM_DESC_EX(PARAM_MIX_DELAY_REV, "REV", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_rev),
    PARAM_DESC_EX(PARAM_MIX_DELAY_VOL, "VOL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_vol),

    PARAM_DESC_EX(PARAM_SAMPLER_SAMPLE, "Sample", PARAM_TYPE_ENUM, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_sampler_sample),
    PARAM_DESC_EX(PARAM_SAMPLER_GAIN, "Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_gain),
    PARAM_DESC_EX(PARAM_SAMPLER_START, "Start", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_start),
    PARAM_DESC_EX(PARAM_SAMPLER_END, "End", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_end),
    PARAM_DESC_EX(PARAM_SAMPLER_MODE, "Mode", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_mode_labels, apply_sampler_mode),
    PARAM_DESC_EX(PARAM_SAMPLER_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_sampler_tune),
    PARAM_DESC_EX(PARAM_SAMPLER_SLICE_COUNT, "Slice Count", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_slice_count_labels, apply_sampler_slice_count),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_SOURCE_BPM, "Src BPM", PARAM_TYPE_FLOAT, 40.0f, 300.0f, 0.1f, 120.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_sampler_clip_source_bpm),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_SYNC_LENGTH, "Sync Len", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_sync_length_labels, apply_sampler_clip_sync_length),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_PITCH, "Tune", PARAM_TYPE_BIPOLAR, -12.0f, 12.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_sampler_clip_pitch),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_PLAY_MODE, "PlayMode", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_play_mode_labels, apply_sampler_clip_play_mode),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_LOOP, "Loop", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_sampler_clip_loop),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_STRETCH_MODE, "Stretch", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_stretch_mode_labels, apply_sampler_clip_stretch_mode),

    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_GRAIN, "Grain", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 4.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_grain_labels, apply_sampler_clip_grain),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_HOP, "Hop", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 3.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_hop_labels, apply_sampler_clip_hop),
    PARAM_DESC_EX(PARAM_SAMPLER_CLIP_SEARCH, "Search", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 4.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_search_labels, apply_sampler_clip_search),
    PARAM_DESC_EX(PARAM_FILTER_MODE, "F MODE", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_filter_mode_labels, NULL),
    PARAM_DESC_EX(PARAM_FM_RATIO, "RATIO", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_ALGORITHM, "ALGO", PARAM_TYPE_ENUM, 0.0f, 31.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_fm_algorithm_labels, NULL),
    PARAM_DESC_EX(PARAM_FM_FEEDBACK, "FDBK", PARAM_TYPE_ENUM, 0.0f, 7.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_SYNC, "SYNC", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_FM_BRIGHT, "BRIGHT", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_BODY, "BODY", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_DETAIL, "DETAIL", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_METAL, "METAL", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_ENV_ATTACK, "ATTACK", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_ENV_DECAY, "DECAY", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_ENV_SUSTAIN, "SUSTAIN", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_ENV_RELEASE, "RELEASE", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_263, "Reserved 263", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_264, "Reserved 264", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_RESERVED_265, "Reserved 265", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_ARM, "ARM", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_arm_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_LEN, "LEN", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_len_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_PLAY, "PLAY", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_play_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_XFADE, "XFade", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_STRETCH, "Stretch", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_stretch_mode_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -12.0f, 12.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_GRAIN, "Grain", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 4.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_grain_labels, NULL),
    PARAM_DESC_EX(PARAM_SAMPLER_MULTI_LOOP, "Loop", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_sampler_multi_loop),
    PARAM_DESC_EX(PARAM_SAMPLER_LOOP_START, "Loop", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_loop_start),
    PARAM_DESC_EX(PARAM_PRISM_EDIT, "MODEL", PARAM_TYPE_ENUM, 0.0f, (float)BRICK6_PRISM_LAST_MODEL, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_prism_edit_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_FINE, "Fine", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_COARSE, "TUNE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_FM, "FM AMT", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_TIMBRE, "PARAM1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_MODULATION, "AMOD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_COLOR, "PARAM2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_PHASE_RESET, "PHASE", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_LEVEL, "LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_EDIT, "MODEL", PARAM_TYPE_ENUM, 0.0f, (float)BRICK6_PRISM_LAST_MODEL, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_prism_edit_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_FINE, "Fine", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_COARSE, "TUNE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_FM, "FM AMT", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_TIMBRE, "PARAM1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_MODULATION, "AMOD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_COLOR, "PARAM2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_PHASE_RESET, "PHASE", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_LEVEL, "LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_LEVEL, "OSC1 LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_LEVEL, "OSC2 LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_LEVEL, "OSC3 LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_NOISE_LEVEL, "Noise", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC_DETUNE, "OSC DETUNE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_PHASE_RESET, "RESET", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_stack_reset_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_TABLE, "TABLE", PARAM_TYPE_INT, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U), 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_POS, "POS", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_START, "START", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_END, "END", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_LEVEL, "LEVEL 1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_TUNE, "TUNE 1", PARAM_TYPE_BIPOLAR, -60.0f, 60.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_TABLE, "TABLE", PARAM_TYPE_INT, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U), 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_POS, "POS", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_START, "START", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_END, "END", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_LEVEL, "LEVEL 2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_TUNE, "TUNE 2", PARAM_TYPE_BIPOLAR, -60.0f, 60.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_FRAME_INTERP, "FRAME", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_SAMPLE_INTERP, "SAMPLE", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_POS_UPDATE, "POSUPD", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_wave_pos_update_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_POS_SMOOTH, "SMOOTH", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIDI_PROGRAM, "Program", PARAM_TYPE_INT, 0.0f, 128.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_program),
    PARAM_DESC_EX(PARAM_MIDI_CC1_1, "CC16", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_1),
    PARAM_DESC_EX(PARAM_MIDI_CC1_2, "CC17", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_2),
    PARAM_DESC_EX(PARAM_MIDI_CC1_3, "CC18", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_3),
    PARAM_DESC_EX(PARAM_MIDI_CC1_4, "CC19", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_4),
    PARAM_DESC_EX(PARAM_MIDI_CC2_1, "CC20", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_1),
    PARAM_DESC_EX(PARAM_MIDI_CC2_2, "CC21", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_2),
    PARAM_DESC_EX(PARAM_MIDI_CC2_3, "CC22", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_3),
    PARAM_DESC_EX(PARAM_MIDI_CC2_4, "CC23", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_4),
    PARAM_DESC_EX(PARAM_MIDI_CC3_1, "CC24", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_1),
    PARAM_DESC_EX(PARAM_MIDI_CC3_2, "CC25", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_2),
    PARAM_DESC_EX(PARAM_MIDI_CC3_3, "CC26", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_3),
    PARAM_DESC_EX(PARAM_MIDI_CC3_4, "CC27", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_4),
    PARAM_DESC_EX(PARAM_COMP_MODEL, "MODEL", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_comp_model_labels, apply_comp_model),
    PARAM_DESC_EX(PARAM_COMP_DETECT, "DETECT", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_comp_detect_labels, apply_comp_detect),
    PARAM_DESC_EX(PARAM_COMP_KNEE_DB, "KNEE", PARAM_TYPE_FLOAT, 0.0f, 12.0f, 0.5f, 6.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_comp_knee),
    PARAM_DESC_EX(PARAM_COMP_DELUGE_SAT, "SAT", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_comp_deluge_sat),
    PARAM_DESC_EX(PARAM_EXTERNAL_INPUT, "INPUT", PARAM_TYPE_ENUM, 0.0f, (float)(ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_external_input_labels, NULL),

#define PARAM_DESC_MIDI_FX_SLOT(_p1, _p2, _p3, _model) \
    PARAM_DESC_EX((_p1), "PARAM1", PARAM_TYPE_ENUM, 0.0f, 7.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", seq_division_arp_labels, NULL), \
    PARAM_DESC_EX((_p2), "PARAM2", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_midi_fx_style_labels, NULL), \
    PARAM_DESC_EX((_p3), "PARAM3", PARAM_TYPE_INT, 1.0f, 4.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_model), "MODEL", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_midi_fx_model_labels, NULL)
    PARAM_DESC_MIDI_FX_SLOT(PARAM_MIDI_FX_S1_PARAM1, PARAM_MIDI_FX_S1_PARAM2, PARAM_MIDI_FX_S1_PARAM3, PARAM_MIDI_FX_S1_MODEL),
    PARAM_DESC_MIDI_FX_SLOT(PARAM_MIDI_FX_S2_PARAM1, PARAM_MIDI_FX_S2_PARAM2, PARAM_MIDI_FX_S2_PARAM3, PARAM_MIDI_FX_S2_MODEL),
    PARAM_DESC_MIDI_FX_SLOT(PARAM_MIDI_FX_S3_PARAM1, PARAM_MIDI_FX_S3_PARAM2, PARAM_MIDI_FX_S3_PARAM3, PARAM_MIDI_FX_S3_MODEL),
#undef PARAM_DESC_MIDI_FX_SLOT
    PARAM_DESC_EX(PARAM_FM_PLAY_VEL, "VEL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PLAY_KEY, "KEY", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PLAY_PITCH_ENV, "P ENV", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PLAY_PITCH_TIME, "P TIME", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_OPERATOR_SELECT, "OP", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_fm_operator_labels, NULL),
#define PARAM_DESC_FM_OPERATOR(_level, _freq, _detune, _attack, _decay, _sustain, _release, _on, _mode, _vel, _key) \
    PARAM_DESC_EX((_level), "LEVEL", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_freq), "FREQ", PARAM_TYPE_FLOAT, 0.25f, 16.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "x", NULL, NULL), \
    PARAM_DESC_EX((_detune), "DETUNE", PARAM_TYPE_BIPOLAR, -7.0f, 7.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_attack), "ATTACK", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_decay), "DECAY", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 92.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_sustain), "SUSTAIN", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 80.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_release), "RELEASE", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 72.0f, PARAM_DISPLAY_INT, "", NULL, NULL), \
    PARAM_DESC_EX((_on), "ON", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL), \
    PARAM_DESC_EX((_mode), "MODE", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_fm_operator_mode_labels, NULL), \
    PARAM_DESC_EX((_vel), "VEL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL), \
    PARAM_DESC_EX((_key), "KEY", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL)
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP1_LEVEL, PARAM_FM_OP1_FREQ, PARAM_FM_OP1_DETUNE, PARAM_FM_OP1_ENV_ATTACK, PARAM_FM_OP1_ENV_DECAY, PARAM_FM_OP1_ENV_SUSTAIN, PARAM_FM_OP1_ENV_RELEASE, PARAM_FM_OP1_ON, PARAM_FM_OP1_MODE, PARAM_FM_OP1_VEL, PARAM_FM_OP1_KEY),
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP2_LEVEL, PARAM_FM_OP2_FREQ, PARAM_FM_OP2_DETUNE, PARAM_FM_OP2_ENV_ATTACK, PARAM_FM_OP2_ENV_DECAY, PARAM_FM_OP2_ENV_SUSTAIN, PARAM_FM_OP2_ENV_RELEASE, PARAM_FM_OP2_ON, PARAM_FM_OP2_MODE, PARAM_FM_OP2_VEL, PARAM_FM_OP2_KEY),
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP3_LEVEL, PARAM_FM_OP3_FREQ, PARAM_FM_OP3_DETUNE, PARAM_FM_OP3_ENV_ATTACK, PARAM_FM_OP3_ENV_DECAY, PARAM_FM_OP3_ENV_SUSTAIN, PARAM_FM_OP3_ENV_RELEASE, PARAM_FM_OP3_ON, PARAM_FM_OP3_MODE, PARAM_FM_OP3_VEL, PARAM_FM_OP3_KEY),
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP4_LEVEL, PARAM_FM_OP4_FREQ, PARAM_FM_OP4_DETUNE, PARAM_FM_OP4_ENV_ATTACK, PARAM_FM_OP4_ENV_DECAY, PARAM_FM_OP4_ENV_SUSTAIN, PARAM_FM_OP4_ENV_RELEASE, PARAM_FM_OP4_ON, PARAM_FM_OP4_MODE, PARAM_FM_OP4_VEL, PARAM_FM_OP4_KEY),
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP5_LEVEL, PARAM_FM_OP5_FREQ, PARAM_FM_OP5_DETUNE, PARAM_FM_OP5_ENV_ATTACK, PARAM_FM_OP5_ENV_DECAY, PARAM_FM_OP5_ENV_SUSTAIN, PARAM_FM_OP5_ENV_RELEASE, PARAM_FM_OP5_ON, PARAM_FM_OP5_MODE, PARAM_FM_OP5_VEL, PARAM_FM_OP5_KEY),
    PARAM_DESC_FM_OPERATOR(PARAM_FM_OP6_LEVEL, PARAM_FM_OP6_FREQ, PARAM_FM_OP6_DETUNE, PARAM_FM_OP6_ENV_ATTACK, PARAM_FM_OP6_ENV_DECAY, PARAM_FM_OP6_ENV_SUSTAIN, PARAM_FM_OP6_ENV_RELEASE, PARAM_FM_OP6_ON, PARAM_FM_OP6_MODE, PARAM_FM_OP6_VEL, PARAM_FM_OP6_KEY),
#undef PARAM_DESC_FM_OPERATOR
#define PARAM_DESC_FM_DX7_PACK(_id) \
    PARAM_DESC_EX((_id), "DX7 BASE", PARAM_TYPE_INT, 0.0f, 16777215.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL)
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP1_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP1_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP1_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP1_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP2_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP2_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP2_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP2_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP3_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP3_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP3_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP3_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP4_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP4_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP4_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP4_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP5_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP5_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP5_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP5_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP6_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP6_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP6_PACK_C),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_OP6_PACK_D),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_PITCH_PACK_A),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_PITCH_PACK_B),
    PARAM_DESC_FM_DX7_PACK(PARAM_FM_DX7_PITCH_PACK_C),
#undef PARAM_DESC_FM_DX7_PACK

    PARAM_DESC_EX(PARAM_FM_TRANSPOSE, "TRANSPOSE", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_R1, "R1", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_R2, "R2", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_R3, "R3", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_R4, "R4", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_L1, "L1", PARAM_TYPE_BIPOLAR, -49.0f, 50.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_L2, "L2", PARAM_TYPE_BIPOLAR, -49.0f, 50.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_L3, "L3", PARAM_TYPE_BIPOLAR, -49.0f, 50.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_FM_PITCH_L4, "L4", PARAM_TYPE_BIPOLAR, -49.0f, 50.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_AUDIO_FX_P1, "P1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_AUDIO_FX_P2, "P2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    /* Shared BRICK control: model-specific interpretation stays downstream. */
    PARAM_DESC_EX(PARAM_AUDIO_FX_P3, "P3", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_AUDIO_FX_MODEL, "MODEL", PARAM_TYPE_ENUM, 0.0f, 11.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_audio_fx_model_labels, NULL),
};
