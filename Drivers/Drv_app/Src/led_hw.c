#include "led_hw.h"

#include "Board/board_led_transport.h"

#include "stm32h7xx_hal.h"
#include "UI/ui_service_wakeup.h"

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (board_led_transport_handle_pwm_callback(htim) != 0U)
        ui_service_wakeup(UI_SERVICE_WAKE_LED);
}

void led_hw_init(void)
{
    board_led_transport_init();
}

bool led_hw_busy(void)
{
    return board_led_transport_busy();
}

void led_hw_send(const uint8_t *rgb, uint32_t count)
{
    board_led_transport_send(rgb, count);
}
