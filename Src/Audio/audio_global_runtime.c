#include "Audio/audio_global_runtime.h"

#include <stddef.h>

#include "Audio/audio_float.h"
#include "Audio/audio_transport_runtime.h"
#include "Audio/fx_comp_lab.h"
#include "Audio/fx_modfx_global.h"
#include "Audio/mixer.h"
#include "Param/param_ids.h"

static int8_t audio_global_slot(float value)
{
    return (int8_t)value;
}

uint8_t audio_global_runtime_apply(uint16_t parameter_id, float value)
{
    fx_comp_lab_t *comp;

    switch ((param_id_t)parameter_id)
    {
        case PARAM_MIX_SEND0_FX: mixer_set_send_fx_slot(0U, audio_global_slot(value)); return 1U;
        case PARAM_MIX_SEND1_FX: mixer_set_send_fx_slot(1U, audio_global_slot(value)); return 1U;

        case PARAM_MIX_REVERB_WET: mixer_set_reverb_wet(value); return 1U;
        case PARAM_MIX_REVERB_ROOM_SIZE: mixer_set_reverb_room_size(value); return 1U;
        case PARAM_MIX_REVERB_DAMPING: mixer_set_reverb_damping(value); return 1U;
        case PARAM_MIX_REVERB_WIDTH: mixer_set_reverb_width(value); return 1U;
        case PARAM_MIX_REVERB_HPF: mixer_set_reverb_hpf(value); return 1U;
        case PARAM_MIX_REVERB_LPF: mixer_set_reverb_lpf(value); return 1U;
        case PARAM_MIX_REVERB_DELAYS: mixer_set_reverb_delays((uint8_t)value); return 1U;

        case PARAM_MIX_DELAY_TYPE: mixer_set_delay_type((uint8_t)value); return 1U;
        case PARAM_MIX_DELAY_MODE: mixer_set_delay_mode((uint8_t)value); return 1U;
        case PARAM_MIX_DELAY_TIME:
            mixer_set_delay_division(value,
                audio_transport_runtime_get()->tempo_effective_bpm_milli);
            return 1U;
        case PARAM_MIX_DELAY_TIME_R:
            mixer_set_delay_division_r(value,
                audio_transport_runtime_get()->tempo_effective_bpm_milli);
            return 1U;
        case PARAM_MIX_DELAY_PINGPONG: mixer_set_delay_pingpong((uint8_t)value); return 1U;
        case PARAM_MIX_DELAY_WIDTH: mixer_set_delay_width(value); return 1U;
        case PARAM_MIX_DELAY_FEEDBACK: mixer_set_delay_feedback(value); return 1U;
        case PARAM_MIX_DELAY_SPECTRAL_POSITION: mixer_set_delay_spectral_position(value); return 1U;
        case PARAM_MIX_DELAY_SPECTRAL_WIDTH: mixer_set_delay_spectral_width(value); return 1U;
        case PARAM_MIX_DELAY_FBW: mixer_set_delay_feedback_width(value); return 1U;
        case PARAM_MIX_DELAY_MOD: mixer_set_delay_mod_depth(value); return 1U;
        case PARAM_MIX_DELAY_MOD_RATE: mixer_set_delay_mod_rate(value); return 1U;
        case PARAM_MIX_DELAY_REV: mixer_set_delay_reverb_send(value); return 1U;
        case PARAM_MIX_DELAY_VOL: mixer_set_delay_volume(value); return 1U;

        case PARAM_MODFX_MODEL: fx_modfx_global_set_model((uint8_t)value); return 1U;
        case PARAM_MODFX_RATE: fx_modfx_global_set_rate(value); return 1U;
        case PARAM_MODFX_DEPTH: fx_modfx_global_set_depth(value); return 1U;
        case PARAM_MODFX_FEEDBACK: fx_modfx_global_set_feedback(value); return 1U;
        case PARAM_MODFX_OFFSET: fx_modfx_global_set_offset(value); return 1U;
        case PARAM_MODFX_RATE_B: fx_modfx_global_set_rate_b(value); return 1U;
        case PARAM_MODFX_DELAY_B: fx_modfx_global_set_offset_b(value); return 1U;
        case PARAM_MODFX_DEPTH_B: fx_modfx_global_set_depth_b(value); return 1U;
        case PARAM_MODFX_WIDTH: fx_modfx_global_set_width(value); return 1U;

        case PARAM_EQ_LOW_DB: audio_float_set_dj_eq_low_db(value); return 1U;
        case PARAM_EQ_MID_DB: audio_float_set_dj_eq_mid_db(value); return 1U;
        case PARAM_EQ_HIGH_DB: audio_float_set_dj_eq_high_db(value); return 1U;
        case PARAM_SAT_TONE: audio_float_set_saturation_tone(value); return 1U;
        case PARAM_SAT_BIAS: audio_float_set_saturation_bias(value); return 1U;
        case PARAM_SAT_DRIVE: audio_float_set_saturation_drive(value); return 1U;
        case PARAM_SAT_MIX: audio_float_set_saturation_mix(value); return 1U;
        case PARAM_POST_GAIN: audio_float_set_postgain(value); return 1U;
        case PARAM_OUTPUT_COMP: audio_float_set_output_compensation(value); return 1U;

        case PARAM_COMP_MODEL:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_model(comp, (uint8_t)value);
            return 1U;
        case PARAM_COMP_DETECT:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_detector_rms(comp, (uint8_t)value);
            return 1U;
        case PARAM_COMP_KNEE_DB:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_knee_db(comp, value);
            return 1U;
        case PARAM_COMP_DELUGE_SAT:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_deluge_saturation(comp, (uint8_t)value);
            return 1U;
        case PARAM_BUS_COMP_THRESHOLD_DB: audio_float_set_bus_comp_threshold_db(value); return 1U;
        case PARAM_BUS_COMP_RATIO: audio_float_set_bus_comp_ratio(value); return 1U;
        case PARAM_BUS_COMP_ATTACK_INDEX:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_attack_s(comp, value);
            return 1U;
        case PARAM_BUS_COMP_RELEASE_INDEX:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_release_s(comp, value);
            return 1U;
        case PARAM_BUS_COMP_MAKEUP_DB: audio_float_set_bus_comp_makeup_db(value); return 1U;
        case PARAM_BUS_COMP_AUTO_MAKEUP: audio_float_set_bus_comp_auto_makeup((uint8_t)value); return 1U;
        case PARAM_BUS_COMP_DRYWET:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_mix(comp, value);
            return 1U;
        case PARAM_BUS_COMP_HPF_HZ:
            comp = fx_comp_lab_get_instance();
            if (comp != NULL) fx_comp_lab_set_sc_hpf_hz(comp, value);
            return 1U;
        default:
            return 0U;
    }
}
