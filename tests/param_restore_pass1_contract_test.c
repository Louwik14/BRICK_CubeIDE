#include <assert.h>
#include <stdint.h>

#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"

const param_desc_t param_registry[PARAM_COUNT] = {
    [PARAM_MIX_LEVEL] = {
        .id = PARAM_MIX_LEVEL,
        .min = -1.0f,
        .max = 1.0f
    }
};

int main(void)
{
    param_registry_prepared_value_t value = {0};
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 2.0f, &value) == 1U);
    assert(value.id == PARAM_MIX_LEVEL);
    assert(value.value == 1.0f);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, -2.0f, &value) == 1U);
    assert(value.value == -1.0f);
    assert(param_registry_prepare_value(PARAM_RESERVED_000, 0.0f, &value) == 0U);
    assert(param_registry_prepare_value(PARAM_MIX_LEVEL, 0.0f, NULL) == 0U);

    param_registry_control_shadow_init();
    param_registry_control_shadow_set(2U, PARAM_MIX_LEVEL, 0.25f);
    param_registry_runtime_ui_value_t shadow = {0};
    assert(param_registry_control_shadow_ui_value_get(
        2U, PARAM_MIX_LEVEL, &shadow) == 1U);
    assert(shadow.base_value == 0.25f);
    assert(shadow.flags == PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID);
    param_registry_control_shadow_set(2U, PARAM_MIX_LEVEL, 0.5f);
    assert(param_registry_control_shadow_ui_value_get(
        2U, PARAM_MIX_LEVEL, &shadow) == 1U);
    assert(shadow.base_value == 0.5f);
    assert(shadow.flags == PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID);
    return 0;
}
