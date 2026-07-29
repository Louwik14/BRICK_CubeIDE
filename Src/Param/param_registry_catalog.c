#include "Param/param_registry_catalog.h"
#include "Param/param_filter.h"
#include "Param/param_registry_apply_bindings.h"
#include "Mod/mod_lfo_v1.h"
#include "Sampler/sample_global_pool.h"
#include "Seq/seq_types.h"
#include "ui_core.h"
#include <stddef.h>
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
static const char *const g_stack_reset_labels[] = {"FREE", "RESET", NULL};
static const char *const g_wave_phase_labels[] = {"0", "90", "180", "270", NULL};
static const char *const g_wave_flip_labels[] = {"OFF", "X", "Y", "XY", NULL};
static const char *const g_wave_pos_update_labels[] = {"FULL", "8", "16", "32", NULL};
#if defined(BRICK6_VARIANT_LOWCOST)
static const char *const g_route_labels[] = {"None", "Master", NULL};
#define PARAM_MIX_ROUTE_MAX 1.0f
#else
static const char *const g_route_labels[] = {"None", "Master", "Cue", "Both", NULL};
#define PARAM_MIX_ROUTE_MAX 3.0f
#endif
static const char *const g_master_fx_type_labels[] = {"OFF", "DRIVE", "CRUSH", "PUMP", "CHOP", "WOBBLE", "COMB", "RING", "STUTTER", "FREEZE", "COLOR", NULL};
static const char *const g_filter_type_labels[] = {"Off", "EQ3", "LP", "HP", "BP", NULL};
static const char *const g_delay_time_labels[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1D", "1 bar", NULL};
static const char *const g_delay_type_labels[] = {"CLASSIC", "DUAL", NULL};
static const char *const g_delay_mode_labels[] = {"Normal", "PingPong", "Tap", "ClassicPP", NULL};
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


static const char *const g_prism_edit_labels[] = {"CSAW", "Morph", "SawSq", "SinTri", "Buzz", "SqSub", "SawSub", "SqSync", "SawSync", "TriSaw", "TriSq", "TriTri", "TriSin", "Ring", "Swarm", "Toy", "Vosim", "Vowel", "FOF", "Harm", "FM", "FB FM", "Chaos", "Bell", "Drum", "Kick", "Cymbal", "Snare", "WTbl", "WMap", "WLine", "WPara", "Noise", "TwinPk", "Clock", "Cloud", "Particle", "DigiMod", "????", NULL};
static const char *const g_stack_model_labels[] = {"SINFD", "SHAPE", "WAVETABLE", "SUB", "FM", "FB FM", "RING", "TRIPLE SAW", "TRIPLE SQR", "SWARM", "TRIFD", NULL};
static const char *const g_deluge_model_labels[] = {
    "SINE", "TRI", "SQUARE", "A-SQUARE", "SAW", "A-SAW", NULL
};
#if defined(BRICK6_VARIANT_LOWCOST)
static const char *const g_track_family_labels[] = {"Off", "Input1", "-", "-", "-", "Synth", "Drum", "Master", "MIDI", "Sampler", NULL};
#else
static const char *const g_track_family_labels[] = {"Off", "Input1", "Input2", "Input3", "Input4", "Synth", "Drum", "Master", "MIDI", "Sampler", NULL};
#endif
static const char *const g_track_midi_source_labels[] = {"INT", "EXT", "ALL", NULL};
static const char *const g_cfg_group_link_labels[] = {"OFF", "ON", NULL};
static const char *const g_cfg_start_labels[] = {"DEFAULT", "TRIG", "ROLL 1/4", "ROLL 1/2", "ROLL 1", NULL};
static const char *const g_cfg_sync_labels[] = {"INT", "MidiEXT", "UsbEXT", NULL};
static const char *const g_cfg_rec_len_labels[] = {"Overdub", "Pattern", NULL};
static const char *const g_seq_div_labels[] = {"OFF", "1/2", "1/4", "1/8", NULL};
static const char *const g_kbd_root_labels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
static const char *const g_kbd_scale_labels[] = {"Major", "NatMin", "Dorian", "Mixoly", "PntMaj", "PntMin", "Chrom", NULL};
static const char *const g_kbd_note_order_labels[] = {"Natural", "Fifths", NULL};
static const char *const g_arp_rate_labels[] = {"1/4", "1/8", "1/16", "1/32", "1/4t", "1/8t", "1/16t", "1/32t", NULL};
static const char *const g_arp_pattern_labels[] = {"Up", "Down", "UpDn", "Rnd", "Chord", NULL};
static const char *const g_arp_accent_labels[] = {"Off", "1st", "Alt", "Rnd", NULL};
static const char *const g_arp_strum_labels[] = {"Off", "Up", "Down", "Alt", "Rnd", NULL};
static const char *const g_arp_dir_labels[] = {"Normal", "PingPong", "RndWalk", NULL};
static const char *const g_arp_sync_labels[] = {"Int", "Clock", "Free", NULL};
static const char *const g_lfo_shape_labels[] = {"SIN", "TRI", "SAW", "SQR", "RND", "SIN+", "TRI+", "SQR+", "RSAW", NULL};
static const char *const g_lfo_trig_labels[] = {"FREE", "TRIG", "HOLD", "ONE", NULL};
static const char *const g_mod_matrix_source_labels[] = {"Off", "LFO 1", "LFO 2", "LFO 3", "env flt", "env vca", "env mod", "MULT1", "MULT2", "SLEW1", "SLEW2", NULL};

#define PARAM_DESC_LFO(_rate, _shape, _trig, _phase, _apply_rate, _apply_shape, _apply_trig, _apply_phase) \
    PARAM_DESC_EX((_rate), "Rate", PARAM_TYPE_FLOAT, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, (_apply_rate)), \
    PARAM_DESC_EX((_shape), "Shape", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_shape_labels, (_apply_shape)), \
    PARAM_DESC_EX((_trig), "Trig", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_trig_labels, (_apply_trig)), \
    PARAM_DESC_EX((_phase), "Phase", PARAM_TYPE_FLOAT, 0.0f, 360.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "deg", NULL, (_apply_phase))

const param_desc_t param_registry[PARAM_COUNT] = {
    PARAM_DESC_EX(PARAM_GRAN_DENSITY, "Gran Density", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_density),
    PARAM_DESC(PARAM_GRAN_PITCH, "Gran Pitch", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.1f, 0.0f, "st", apply_gran_pitch),
    PARAM_DESC_EX(PARAM_GRAN_MIX, "Gran Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_mix),
    PARAM_DESC_EX(PARAM_GRAN_FREEZE, "Gran Freeze", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_gran_freeze),
    PARAM_DESC_EX(PARAM_GRAN_SPREAD, "Gran Spread", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_spread),
    PARAM_DESC_EX(PARAM_GRAN_STEREO, "Gran Stereo", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_stereo),

    PARAM_DESC(PARAM_MIX_TRACK0_GAIN, "T0 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_GAIN, "T1 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_GAIN, "T2 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_GAIN, "T3 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_PAN, "T0 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_PAN, "T1 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_PAN, "T2 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_PAN, "T3 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_MUTE, "Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_MUTE, "T1 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_MUTE, "T2 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_MUTE, "T3 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_ROUTE, "T0 Route", PARAM_TYPE_ENUM, 0.0f, PARAM_MIX_ROUTE_MAX, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_ROUTE, "T1 Route", PARAM_TYPE_ENUM, 0.0f, PARAM_MIX_ROUTE_MAX, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_ROUTE, "T2 Route", PARAM_TYPE_ENUM, 0.0f, PARAM_MIX_ROUTE_MAX, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_ROUTE, "T3 Route", PARAM_TYPE_ENUM, 0.0f, PARAM_MIX_ROUTE_MAX, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_INSERT0, "T0 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK0_INSERT1, "T0 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT0, "T1 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT1, "T1 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT0, "T2 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT1, "T2 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT0, "T3 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT1, "T3 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_SEND0, "T0 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK0_SEND1, "T0 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND0, "T1 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND1, "T1 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND0, "T2 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND1, "T2 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND0, "T3 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND1, "T3 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_SEND0_FX, "Send0 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send0_fx),
    PARAM_DESC(PARAM_MIX_SEND1_FX, "Send1 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send1_fx),
    PARAM_DESC(PARAM_MIX_LEVEL, "Level", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_PAN, "Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND1, "Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND2, "Send2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC_EX(PARAM_BUS_COMP_THRESHOLD_DB, "BusComp Threshold", PARAM_TYPE_FLOAT, -60.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_threshold),
    PARAM_DESC_EX(PARAM_BUS_COMP_RATIO, "BusComp Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_bus_comp_ratio),
    /* DSP side clamps attack index to [0..5], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_ATTACK_INDEX, "BusComp Attack", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, "idx", apply_bus_comp_attack_index),
    /* DSP side clamps release index to [0..4], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_RELEASE_INDEX, "BusComp Release", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, "idx", apply_bus_comp_release_index),
    PARAM_DESC_EX(PARAM_BUS_COMP_MAKEUP_DB, "BusComp Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_AUTO_MAKEUP, "BusComp AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_bus_comp_auto_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_DRYWET, "BusComp DryWet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUS_COMP_HPF_HZ, "BusComp HPF", PARAM_TYPE_FLOAT, 0.0f, 1000.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "Hz", NULL, NULL),

    PARAM_DESC_EX(PARAM_DAISY_COMP_THRESHOLD_DB, "Daisy Threshold", PARAM_TYPE_FLOAT, -40.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_threshold),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RATIO, "Daisy Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_daisy_ratio),
    PARAM_DESC_EX(PARAM_DAISY_COMP_ATTACK_S, "Daisy Attack", PARAM_TYPE_FLOAT, 0.0001f, 0.5f, 0.0001f, 0.001f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_attack),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RELEASE_S, "Daisy Release", PARAM_TYPE_FLOAT, 0.01f, 5.0f, 0.01f, 0.6f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_release),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MAKEUP_DB, "Daisy Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_AUTO_MAKEUP, "Daisy AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_daisy_auto_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MIX, "Daisy Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_daisy_mix),

    PARAM_DESC_EX(PARAM_EQ_LOW_DB, "EQ Low", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_low_db),
    PARAM_DESC_EX(PARAM_EQ_MID_DB, "EQ Mid", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_mid_db),
    PARAM_DESC_EX(PARAM_EQ_HIGH_DB, "EQ High", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_high_db),

    PARAM_DESC_EX(PARAM_SAT_TONE, "Sat Tone", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_tone),
    PARAM_DESC_EX(PARAM_SAT_BIAS, "Sat Bias", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_bias),
    PARAM_DESC_EX(PARAM_SAT_DRIVE, "Sat Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_drive),
    PARAM_DESC_EX(PARAM_SAT_MIX, "Sat Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_mix),

    PARAM_DESC_EX(PARAM_FILTER_TYPE, "F Type", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_filter_type_labels, apply_filter_type),
    PARAM_DESC_EX(PARAM_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_cutoff),
    PARAM_DESC_EX(PARAM_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_resonance),
    PARAM_DESC_EX(PARAM_FILTER_EG_AMT, "EG Amt", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_eg_amount),
    PARAM_DESC_EX(PARAM_FILTER_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_attack),
    PARAM_DESC_EX(PARAM_FILTER_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_decay),
    PARAM_DESC_EX(PARAM_FILTER_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_sustain),
    PARAM_DESC_EX(PARAM_FILTER_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_release),
    PARAM_DESC_EX(PARAM_FILTER_KEYTRK, "KeyTrk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_keytrack),
    PARAM_DESC_EX(PARAM_FILTER_ENVRST, "EnvRst", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_filter_env_reset),
    PARAM_DESC_EX(PARAM_FILTER_ENVDLY, "EnvDly", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_env_delay),
    PARAM_DESC_EX(PARAM_FILTER_EQ_LOW, "Low", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_low),
    PARAM_DESC_EX(PARAM_FILTER_EQ_MID, "Mid", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_mid),
    PARAM_DESC_EX(PARAM_FILTER_EQ_HIGH, "High", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_high),
    PARAM_DESC_EX(PARAM_FILTER_DRIVE, "Drive", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_drive),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_BITS, "Bits", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_bits),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE, "Rate", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE2, "Rate2", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate2),
    PARAM_DESC_EX(PARAM_VCA_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_CFG_TRACK, "Track", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U), 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_track_family_labels, apply_cfg_track),
    PARAM_DESC_EX(PARAM_CFG_TRACK_TYPE, "Type", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_cfg_track_type),
    PARAM_DESC_EX(PARAM_CFG_MIDI_CH, "Midi CH", PARAM_TYPE_INT, 1.0f, 16.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, apply_cfg_midi_ch),
    PARAM_DESC_EX(PARAM_CFG_MIDI_SRC, "Midi Src", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_track_midi_source_labels, apply_cfg_midi_src),
    PARAM_DESC_EX(PARAM_CFG_GROUP_SPREAD, "SPREAD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_CFG_GROUP_LINK, "LINK", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_cfg_group_link_labels, NULL),
    PARAM_DESC_EX(PARAM_CFG_START, "START", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_start_labels, apply_cfg_start),
    PARAM_DESC_EX(PARAM_CFG_TEMPO, "Tempo", PARAM_TYPE_FLOAT, 40.0f, 300.0f, 0.1f, 120.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_cfg_tempo),
    PARAM_DESC_EX(PARAM_CFG_SYNC, "Sync", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_sync_labels, apply_cfg_sync),
    PARAM_DESC_EX(PARAM_CFG_REC_LEN, "Len", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_rec_len_labels, apply_cfg_rec_len),
    PARAM_DESC_EX(PARAM_CFG_METRO, "METRO", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_cfg_metro),
    PARAM_DESC_EX(PARAM_SEQ_LENGTH, "LENGTH", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, (float)SEQ_DEFAULT_LENGTH_STEPS, PARAM_DISPLAY_INT, "", NULL, apply_seq_length),
    PARAM_DESC_EX(PARAM_SEQ_DIV, "DIV", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_seq_div_labels, apply_seq_div),
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

    PARAM_DESC_EX(PARAM_KBD_ROOT, "Root", PARAM_TYPE_ENUM, 0.0f, 11.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_root_labels, apply_kbd_root),
    PARAM_DESC_EX(PARAM_KBD_SCALE, "Scale", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_scale_labels, apply_kbd_scale),
    PARAM_DESC_EX(PARAM_KBD_OMNICHORD, "Omni", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_omnichord),
    PARAM_DESC_EX(PARAM_KBD_NOTE_ORDER, "Order", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_note_order_labels, apply_kbd_note_order),
    PARAM_DESC_EX(PARAM_KBD_CHORD_OVERRIDE, "ChrOvr", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_chord_override),
    PARAM_DESC_EX(PARAM_KBD_MONO_LAST, "MonoLast", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_mono_last),

    PARAM_DESC_EX(PARAM_ARP_HOLD, "Hold", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_arp_hold),
    PARAM_DESC_EX(PARAM_ARP_RATE, "Rate", PARAM_TYPE_ENUM, 0.0f, 7.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_arp_rate_labels, apply_arp_rate),
    PARAM_DESC_EX(PARAM_ARP_OCT, "Oct", PARAM_TYPE_INT, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_oct),
    PARAM_DESC_EX(PARAM_ARP_PATTERN, "Pattern", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_pattern_labels, apply_arp_pattern),
    PARAM_DESC_EX(PARAM_ARP_GATE, "Gate", PARAM_TYPE_INT, 1.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_gate),
    PARAM_DESC_EX(PARAM_ARP_SWING, "Swing", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_arp_swing),
    PARAM_DESC_EX(PARAM_ARP_ACCENT, "Accent", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_accent_labels, apply_arp_accent),
    PARAM_DESC_EX(PARAM_ARP_VEL_ACC, "VelAcc", PARAM_TYPE_INT, 0.0f, 64.0f, 1.0f, 24.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_vel_acc),
    PARAM_DESC_EX(PARAM_ARP_STRUM, "Strum", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_strum_labels, apply_arp_strum),
    PARAM_DESC_EX(PARAM_ARP_OFFSET, "Offset", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_offset),
    PARAM_DESC_EX(PARAM_ARP_TRANS, "Trans", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_trans),
    PARAM_DESC_EX(PARAM_ARP_SPREAD, "Spread", PARAM_TYPE_INT, 0.0f, 12.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_spread),
    PARAM_DESC_EX(PARAM_ARP_DIR, "Dir", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_dir_labels, apply_arp_dir),
    PARAM_DESC_EX(PARAM_ARP_SYNC, "Sync", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_sync_labels, apply_arp_sync),

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
    PARAM_DESC_EX(PARAM_MIX_REVERB_SIZE, "Size", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_size),
    PARAM_DESC_EX(PARAM_MIX_REVERB_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_decay),
    PARAM_DESC_EX(PARAM_MIX_REVERB_PRED, "PreD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_pred),
    PARAM_DESC_EX(PARAM_MIX_REVERB_HPF, "HPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_hpf),
    PARAM_DESC_EX(PARAM_MIX_REVERB_LPF, "LPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_lpf),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TYPE, "TYPE", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_delay_type_labels, apply_mix_delay_type),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TIME, "TIME", PARAM_TYPE_ENUM, 0.0f, 12.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_delay_time_labels, apply_mix_delay_time),
    PARAM_DESC_EX(PARAM_MIX_DELAY_PINGPONG, "X", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_delay_pingpong),
    PARAM_DESC_EX(PARAM_MIX_DELAY_MODE, "MODE", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_delay_mode_labels, apply_mix_delay_mode),
    PARAM_DESC_EX(PARAM_MIX_DELAY_TIME_R, "TIME_R", PARAM_TYPE_ENUM, 0.0f, 12.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_delay_time_labels, apply_mix_delay_time_r),
    PARAM_DESC_EX(PARAM_MIX_DELAY_WIDTH, "WID", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_mix_delay_width),
    PARAM_DESC_EX(PARAM_MIX_DELAY_FEEDBACK, "FDBK", PARAM_TYPE_FLOAT, 0.0f, 1.20f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_feedback),
    PARAM_DESC_EX(PARAM_MIX_DELAY_HPF, "HPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_hpf),
    PARAM_DESC_EX(PARAM_MIX_DELAY_LPF, "LPF", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_delay_lpf),
    PARAM_DESC_EX(PARAM_MIX_DELAY_FBW, "FBW", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_mix_delay_feedback_width),
    PARAM_DESC_EX(PARAM_MIX_DELAY_SWING, "RSV", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MIX_DELAY_ACCENT, "RSV", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
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
    PARAM_DESC_EX(PARAM_MASTER_FX1_TYPE, "FX1", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_master_fx_type_labels, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX1_LEVEL, "LVL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX1_A, "A", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX1_B, "B", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX2_TYPE, "FX2", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_master_fx_type_labels, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX2_LEVEL, "LVL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX2_A, "A", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX2_B, "B", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX3_TYPE, "FX3", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_master_fx_type_labels, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX3_LEVEL, "LVL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX3_A, "A", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX3_B, "B", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX4_TYPE, "FX4", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_master_fx_type_labels, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX4_LEVEL, "LVL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX4_A, "A", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MASTER_FX4_B, "B", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_ARM, "ARM", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_arm_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_LEN, "LEN", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_len_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_PLAY, "PLAY", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_looper_play_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_XFADE, "XFade", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_STRETCH, "Stretch", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_stretch_mode_labels, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -12.0f, 12.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_LOOPER_GRAIN, "Grain", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 4.0f, PARAM_DISPLAY_ENUM, "", g_sampler_clip_grain_labels, NULL),
    PARAM_DESC_EX(PARAM_SAMPLER_MULTI_LOOP, "Loop", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_sampler_multi_loop),
    PARAM_DESC_EX(PARAM_SAMPLER_LOOP_START, "Loop", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_loop_start),
    PARAM_DESC_EX(PARAM_PRISM_EDIT, "MODEL", PARAM_TYPE_ENUM, 0.0f, 38.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_prism_edit_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_FINE, "Fine", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_COARSE, "TUNE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_FM, "FM AMT", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_TIMBRE, "PARAM1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_MODULATION, "AMOD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_COLOR, "PARAM2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_PHASE_RESET, "PHASE", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_PRISM_LEVEL, "LVL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_PRISM_OSC2_EDIT, "MODEL", PARAM_TYPE_ENUM, 0.0f, 38.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_prism_edit_labels, NULL),
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
    PARAM_DESC_EX(PARAM_STACK_OSC1_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC1_PARAM3, "Param3", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC2_PARAM3, "Param3", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_MODEL, "Model", PARAM_TYPE_ENUM, 0.0f, 10.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_stack_model_labels, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.01f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_TIMBRE, "Timbre", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_COLOR, "Color", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC3_PARAM3, "Param3", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_OSC_DETUNE, "OSC DETUNE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_STACK_PHASE_RESET, "RESET", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_stack_reset_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_TABLE, "TABLE", PARAM_TYPE_INT, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U), 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_POS, "POS", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_START, "START", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_END, "END", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_LEVEL, "LEVEL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_TUNE, "TUNE", PARAM_TYPE_BIPOLAR, -60.0f, 60.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_PHASE, "PHASE", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_wave_phase_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC1_FLIP, "FLIP", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_wave_flip_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_TABLE, "TABLE", PARAM_TYPE_INT, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U), 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_POS, "POS", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_START, "START", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_END, "END", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_LEVEL, "LEVEL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_TUNE, "TUNE", PARAM_TYPE_BIPOLAR, -60.0f, 60.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_PHASE, "PHASE", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_wave_phase_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_OSC2_FLIP, "FLIP", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_wave_flip_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_FRAME_INTERP, "FRAME", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_SAMPLE_INTERP, "SAMPLE", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_POS_UPDATE, "POSUPD", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_wave_pos_update_labels, NULL),
    PARAM_DESC_EX(PARAM_WAVE_POS_SMOOTH, "SMOOTH", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_MODEL, "MODEL", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_deluge_model_labels, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_LEVEL, "LEVEL", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_TUNE, "TUNE", PARAM_TYPE_FLOAT, -48.0f, 48.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_FINE, "FINE", PARAM_TYPE_FLOAT, -100.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "ct", NULL, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_WIDTH, "WIDTH", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.02f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_PHASE, "PHASE", PARAM_TYPE_FLOAT, 0.0f, 360.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "deg", NULL, NULL),
    PARAM_DESC_EX(PARAM_DELUGE_RETRIG, "RETRIG", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_CFG_GROUP_SPREAD_KEYTRK, "KEYTRK", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_cfg_group_link_labels, NULL),
    PARAM_DESC_EX(PARAM_CFG_GROUP_SEQ_LINK, "SEQ LINK", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_cfg_group_link_labels, NULL),

    PARAM_DESC_EX(PARAM_HYBRID_GATE, "Gate", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
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
};
