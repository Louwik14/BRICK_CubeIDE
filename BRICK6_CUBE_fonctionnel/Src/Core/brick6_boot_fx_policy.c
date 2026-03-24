#include "brick6_boot_fx_policy.h"

#include "fx_pool.h"
#include "mixer.h"

void brick6_boot_fx_policy_init(void)
{
    fx_pool_init();

    (void)fx_pool_activate_slot(0U, FX_EQ3);
    (void)fx_pool_activate_slot(1U, FX_SAT);
    (void)fx_pool_activate_slot(2U, FX_DAISY_COMP);

    mixer_set_track_insert_slot(0U, 0U, 2);
}
