#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FX_DELAY_SHARED_CAPACITY 288008U

typedef enum
{
    FX_DELAY_SHARED_OWNER_NONE = 0,
    FX_DELAY_SHARED_OWNER_CLASSIC,
    FX_DELAY_SHARED_OWNER_DUAL
} fx_delay_shared_owner_t;

float *fx_delay_shared_pool_left(void);
float *fx_delay_shared_pool_right(void);
uint32_t fx_delay_shared_pool_capacity(fx_delay_shared_owner_t owner);
fx_delay_shared_owner_t fx_delay_shared_pool_owner(void);
void fx_delay_shared_pool_acquire(fx_delay_shared_owner_t owner, uint8_t clear);
void fx_delay_shared_pool_clear(fx_delay_shared_owner_t owner);

#ifdef __cplusplus
}
#endif
