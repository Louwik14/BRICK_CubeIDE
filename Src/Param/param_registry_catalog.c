#include "param_registry_catalog.h"
#include "Param/engine_model_catalog.h"
#include "Param/param_filter.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Sampler/sample_global_pool.h"
#include "Seq/seq_types.h"
#include "Seq/seq_division_catalog.h"
#include "Track/track_types.h"
#include <stddef.h>
static const char *const g_modfx_model_labels[] = {
    "Off", "-", "-", "Daisy Stereo", "Junologue", NULL
};
#define PARAM_POLICY_TRANSFORM_TO_DISPLAY(_display)                                      \
    (((_display) == PARAM_DISPLAY_PERCENT) ? param_value_percent127_to_display           \
     : (((_display) == PARAM_DISPLAY_TIME_MS) ? param_value_seconds_to_milliseconds      \
                                               : param_value_identity))
#define PARAM_POLICY_TRANSFORM_TO_CANONICAL(_display)                                    \
    (((_display) == PARAM_DISPLAY_PERCENT) ? param_value_percent127_to_canonical         \
     : (((_display) == PARAM_DISPLAY_TIME_MS) ? param_value_milliseconds_to_seconds      \
                                               : param_value_identity))
#define PARAM_POLICY_DEFAULT_AUTOMATION(_type, _step)                                    \
    ((((_type) == PARAM_TYPE_FLOAT)                                                      \
      || (((_type) == PARAM_TYPE_BIPOLAR) && ((_step) < 1.0f)))                          \
         ? PARAM_AUTOMATION_LINEAR_U16 : PARAM_AUTOMATION_DISCRETE_STEP)
#define PARAM_POLICY_DEFAULT_NORMAL_STEP(_type, _step)                                   \
    ((PARAM_POLICY_DEFAULT_AUTOMATION((_type), (_step)) == PARAM_AUTOMATION_LINEAR_U16)  \
         ? 1.0f : (_step))
#define PARAM_POLICY_DEFAULT_FINE_STEP(_type, _step)                                     \
    ((PARAM_POLICY_DEFAULT_AUTOMATION((_type), (_step)) == PARAM_AUTOMATION_LINEAR_U16)  \
         ? 0.01f : (_step))

#define PARAM_DESC_POLICY_EX(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels, _to_display, _to_canonical, _normal, _fine, _automation) \
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
        .value_policy = {                                                        \
            .canonical_to_display = (_to_display),                               \
            .display_to_canonical = (_to_canonical),                             \
            .normal_step_display = (_normal),                                    \
            .fine_step_display = (_fine),                                        \
            .automation = (_automation),                                         \
        },                                                                       \
    }

#define PARAM_DESC_EX(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels) \
    PARAM_DESC_POLICY_EX((_id), (_name), (_type), (_min), (_max), (_step), (_default), (_display),       \
                         (_unit), (_labels),                                                             \
                         PARAM_POLICY_TRANSFORM_TO_DISPLAY(_display),                                   \
                         PARAM_POLICY_TRANSFORM_TO_CANONICAL(_display),                                 \
                         PARAM_POLICY_DEFAULT_NORMAL_STEP((_type), (_step)),               \
                         PARAM_POLICY_DEFAULT_FINE_STEP((_type), (_step)),                 \
                         PARAM_POLICY_DEFAULT_AUTOMATION((_type), (_step)))

#define PARAM_DESC_CONTINUOUS(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels) \
    PARAM_DESC_POLICY_EX((_id), (_name), (_type), (_min), (_max), (_step), (_default), (_display),                 \
                         (_unit), (_labels),                                                                         \
                         PARAM_POLICY_TRANSFORM_TO_DISPLAY(_display),                                                \
                         PARAM_POLICY_TRANSFORM_TO_CANONICAL(_display),                                              \
                         1.0f, 0.01f, PARAM_AUTOMATION_LINEAR_U16)

#define PARAM_DESC(_id, _name, _type, _min, _max, _step, _default, _unit)                                   \
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
                  NULL)

static const char *const g_bool_labels[] = {"Off", "On", NULL};
static const char *const g_comp_model_labels[] = {"OFF", "DELUGE", "BRICK", NULL};
static const char *const g_comp_detect_labels[] = {"PEAK", "RMS", NULL};
static const char *const g_stack_reset_labels[] = {"FREE", "RESET", NULL};
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


static const char *const g_prism_edit_labels[] = {"CSAW", "Morph", "SawSq", "SinTri", "Buzz", "SqSub", "SawSub", "SqSync", "SawSync", "TriSaw", "TriSq", "TriTri", "TriSin", "Ring", "Swarm", "Toy", "Vosim", "Vowel", "FOF", "FM", "FB FM", "Chaos", "WTbl", "WMap", "WLine", "WPara", "Noise", "TwinPk", "Clock", "Cloud", "Particle", "DigiMod", "????", NULL};
_Static_assert((sizeof(g_prism_edit_labels) / sizeof(g_prism_edit_labels[0])) - 1U == BRICK6_PRISM_MODEL_COUNT,
               "Prism labels and active model count must stay aligned");
static const char *const g_stack_model_labels[] = {"SINE", "TRI", "SQUARE", "SAW", "SHAPE", "TRIPLE SAW", NULL};
static const char *const g_md_model_labels[] = {"TRX-BD", "TRX-SD", "TRX-CH", "EFM-BD", "EFM-SD", "EFM-CB", NULL};
static const char *const g_midi_fx_model_labels[] = {"OFF", "ARP", NULL};
/* ID 4 is intentionally a retired hole: persisted COMP values resolve OFF. */
static const char *const g_audio_fx_model_labels[] = {"OFF", "LOFI", "FOLD", "DRIVE", "-", "POINT", "-", "-", "SUB", "-", "RING", "SUB LIGHT", "VIBE", "DRIFT", NULL};
static const char *const g_filter_mode_labels[] = {"OFF", "LOW", "HIGH", NULL};
static const char *const g_fm_operator_mode_labels[] = {"RATIO", "FIXED", NULL};
static const char *const g_fm_algorithm_labels[] = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16",
    "17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32",NULL
};
static const char *const g_midi_fx_style_labels[] = {"ORDER", "UP", "DOWN", "UP/DOWN", "RANDOM", NULL};
static const char *const g_lfo_shape_labels[] = {"SIN", "TRI", "SAW", "SQR", "RND", "SIN+", "TRI+", "SQR+", "RSAW", NULL};
static const char *const g_lfo_trig_labels[] = {
    "FREE", "TRIG", "HOLD", "ONE", "P.Trig", "P.Hold", "P.One", NULL
};

#define PARAM_DESC_LFO(_rate, _shape, _trig, _phase) \
    PARAM_DESC_EX((_rate), "Rate", PARAM_TYPE_FLOAT, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL), \
    PARAM_DESC_EX((_shape), "Shape", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_shape_labels), \
    PARAM_DESC_EX((_trig), "Trig", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_trig_labels), \
    PARAM_DESC_EX((_phase), "Phase", PARAM_TYPE_FLOAT, 0.0f, 360.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "deg", NULL)

const param_desc_t param_registry[PARAM_COUNT] = {
#include "param_spec_catalog.inc"
};
