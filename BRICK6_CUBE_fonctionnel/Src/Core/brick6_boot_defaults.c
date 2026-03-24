#include "brick6_boot_defaults.h"

#include "param_registry.h"

void brick6_boot_apply_param_defaults(void)
{
    param_reset(PARAM_MIX_TRACK3_GAIN);
    param_reset(PARAM_DX7_ALGORITHM);
    param_reset(PARAM_DX7_FEEDBACK);
    param_reset(PARAM_DX7_TRANSPOSE);
    param_reset(PARAM_DX7_LFO_SPEED);
    param_reset(PARAM_DX7_LFO_DELAY);
    param_reset(PARAM_DX7_LFO_PITCH_MOD_DEPTH);
    param_reset(PARAM_DX7_LFO_AMP_MOD_DEPTH);
    param_reset(PARAM_DX7_PITCH_BEND_RANGE);
    param_reset(PARAM_DX7_PORTAMENTO_TIME);
    param_reset(PARAM_DX7_MONO_MODE);
    param_reset(PARAM_DX7_OPERATOR_MASK);
    param_reset(PARAM_DX7_OPERATOR_1_LEVEL);
    param_reset(PARAM_DX7_OPERATOR_2_LEVEL);
    param_reset(PARAM_DX7_OPERATOR_3_LEVEL);
    param_reset(PARAM_DX7_OPERATOR_4_LEVEL);
    param_reset(PARAM_MONOB_FILTER_TYPE);
    param_reset(PARAM_MONOB_FILTER_CUTOFF);
    param_reset(PARAM_MONOB_FILTER_RESONANCE);
    param_reset(PARAM_MONOB_FILTER_EG_AMT);
    param_reset(PARAM_MONOB_FILTER_ATTACK);
    param_reset(PARAM_MONOB_FILTER_DECAY);
    param_reset(PARAM_MONOB_FILTER_SUSTAIN);
    param_reset(PARAM_MONOB_FILTER_RELEASE);
    param_reset(PARAM_MONOB_FILTER_KEYTRK);
    param_reset(PARAM_MONOB_FILTER_ENVRST);
    param_reset(PARAM_MONOB_FILTER_ENVDLY);
    param_reset(PARAM_MONOB_OSC1_WAVE);
    param_reset(PARAM_MONOB_OSC2_WAVE);
    param_reset(PARAM_MONOB_OSC3_WAVE);
    param_reset(PARAM_MONOB_SUB_WAVE);
    param_reset(PARAM_MONOB_OSC1_RANGE);
    param_reset(PARAM_MONOB_OSC2_RANGE);
    param_reset(PARAM_MONOB_OSC3_RANGE);
    param_reset(PARAM_MONOB_SUB_OCTAVE);
    param_reset(PARAM_MONOB_OSC1_DETUNE);
    param_reset(PARAM_MONOB_OSC2_DETUNE);
    param_reset(PARAM_MONOB_OSC3_DETUNE);
    param_reset(PARAM_MONOB_OSC1_MIX);
    param_reset(PARAM_MONOB_OSC2_MIX);
    param_reset(PARAM_MONOB_OSC3_MIX);
    param_reset(PARAM_MONOB_SUB_MIX);
    param_reset(PARAM_KBD_ROOT);
    param_reset(PARAM_KBD_SCALE);
    param_reset(PARAM_KBD_OMNICHORD);
    param_reset(PARAM_KBD_NOTE_ORDER);
    param_reset(PARAM_KBD_CHORD_OVERRIDE);
}
