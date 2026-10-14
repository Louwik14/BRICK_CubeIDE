#include <stdint.h>

/* Optional firmware domains outside the UI simulator resolve here. */
uintptr_t sim_unimplemented(void)
{
    return 0u;
}
