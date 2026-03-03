#include "sd_owner.h"

static volatile sd_owner_t g_sd_owner = SD_OWNER_NONE;

void sd_set_owner(sd_owner_t owner)
{
    g_sd_owner = owner;
}

sd_owner_t sd_get_owner(void)
{
    return g_sd_owner;
}
