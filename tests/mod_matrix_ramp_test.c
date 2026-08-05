#include "Mod/mod_ramp.h"

#include <assert.h>
#include <math.h>

static void assert_close(float a, float b)
{
    assert(fabsf(a - b) < 0.00001f);
}

static void test_linear_endpoint_ramp(void)
{
    mod_destination_ramp_t ramp;
    mod_destination_ramp_prepare(-1.0f, 1.0f, 5U, 0U, &ramp);
    assert_close(ramp.current, -1.0f);
    assert_close(ramp.step, 0.5f);
    assert_close(mod_destination_ramp_value_at(&ramp, 0U), -1.0f);
    assert_close(mod_destination_ramp_value_at(&ramp, 2U), 0.0f);
    assert_close(mod_destination_ramp_value_at(&ramp, 4U), 1.0f);
}

static void test_discontinuous_ramp_is_constant(void)
{
    mod_destination_ramp_t ramp;
    mod_destination_ramp_prepare(0.75f, -0.75f, 64U, 1U, &ramp);
    assert(ramp.discontinuous != 0U);
    assert_close(ramp.step, 0.0f);
    assert_close(ramp.end, 0.75f);
    assert_close(mod_destination_ramp_value_at(&ramp, 63U), 0.75f);
}

static void test_short_ramps(void)
{
    mod_destination_ramp_t ramp;
    mod_destination_ramp_prepare(0.25f, 0.9f, 1U, 0U, &ramp);
    assert_close(ramp.step, 0.0f);
    assert_close(mod_destination_ramp_value_at(&ramp, 0U), 0.9f);

    mod_destination_ramp_prepare(0.0f, 1.0f, 0U, 0U, &ramp);
    assert_close(mod_destination_ramp_value_at(&ramp, 0U), 0.0f);
}

int main(void)
{
    test_linear_endpoint_ramp();
    test_discontinuous_ramp_is_constant();
    test_short_ramps();
    return 0;
}
