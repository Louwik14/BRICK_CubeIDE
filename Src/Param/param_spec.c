#include "Param/param_spec.h"

#include <math.h>
#include <stddef.h>
#include "Mod/mod_lfo_types.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_types.h"
#include "Track/track_types.h"
#include "Param/engine_model_catalog.h"
#include "Sampler/sample_page_cache_config.h"
#define SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS

#define PARAM_TYPE_FLOAT PARAM_SPEC_FLOAT
#define PARAM_TYPE_INT PARAM_SPEC_INT
#define PARAM_TYPE_ENUM PARAM_SPEC_ENUM
#define PARAM_TYPE_BOOL PARAM_SPEC_BOOL
#define PARAM_TYPE_BIPOLAR PARAM_SPEC_BIPOLAR
#define PARAM_DESC_POLICY_EX(_id,_name,_type,_min,_max,_step,_default,_display,_unit,_labels,_apply,_to_display,_to_canonical,_normal,_fine,_automation) [(_id)]={.id=(_id),.type=(_type),.min=(_min),.max=(_max),.default_value=(_default)}
#define PARAM_DESC_EX(_id,_name,_type,_min,_max,_step,_default,_display,_unit,_labels,_apply) PARAM_DESC_POLICY_EX(_id,_name,_type,_min,_max,_step,_default,_display,_unit,_labels,_apply,0,0,0,0,0)
#define PARAM_DESC_CONTINUOUS PARAM_DESC_EX
#define PARAM_DESC(_id,_name,_type,_min,_max,_step,_default,_unit,_apply) PARAM_DESC_EX(_id,_name,_type,_min,_max,_step,_default,0,_unit,0,_apply)
#define PARAM_DESC_LFO(_rate,_shape,_trig,_phase) PARAM_DESC_EX(_rate,"Rate",PARAM_TYPE_FLOAT,-LFO_FREE_MAX_HZ,(float)MOD_LFO_SYNC_RATE_COUNT,0.01f,0.0f,0,"",0,0), PARAM_DESC_EX(_shape,"Shape",PARAM_TYPE_ENUM,0.0f,8.0f,1.0f,0.0f,0,"",0,0), PARAM_DESC_EX(_trig,"Trig",PARAM_TYPE_ENUM,0.0f,6.0f,1.0f,0.0f,0,"",0,0), PARAM_DESC_EX(_phase,"Phase",PARAM_TYPE_FLOAT,0.0f,360.0f,1.0f,0.0f,0,"",0,0)

const param_spec_t param_spec[PARAM_COUNT] = {
#include "param_spec_catalog.inc"
};

uint8_t param_spec_value_is_valid(param_id_t id, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U)
            || !isfinite(value)) return 0U;
    return ((value >= param_spec[id].min) && (value <= param_spec[id].max))
        ? 1U : 0U;
}
