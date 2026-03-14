#include "drv_encoders.h"

void drv_encoders_init(void)
{
    encoders_init();
}

void drv_encoders_poll(void)
{
    encoders_update(0U);
}

int16_t drv_encoder_get_delta(uint8_t id)
{
    return encoder_consume_delta(id);
}

void drv_encoder_reset(uint8_t id)
{
    (void)encoder_consume_delta(id);
}
