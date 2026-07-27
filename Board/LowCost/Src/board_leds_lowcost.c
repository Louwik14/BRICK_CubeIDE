#include "Board/board_led_transport.h"

#include "Storage/cache_maintenance.h"
#include "Storage/memory_layout.h"
#include "tim.h"

#include <stddef.h>
#include <string.h>

#define LED_BITS_PER_LED 24U
#define LED_RESET_SLOTS 100U
#define WS2812_0 84U
#define WS2812_1 175U
#define LOWCOST_WS2812_BRIGHTNESS_PERCENT 1U
#define LED_BUFFER_SIZE ((BOARD_LED_TRANSPORT_COUNT * LED_BITS_PER_LED) + LED_RESET_SLOTS)

static DMA_BUFFER uint32_t pwm_buffer[LED_BUFFER_SIZE];
static volatile uint8_t dma_busy;

static void led_transport_encode(const uint8_t *rgb, uint32_t count)
{
    uint32_t idx = 0U;

    for (uint32_t i = 0U; i < count; i++)
    {
        const uint8_t colors[3] = {
            (uint8_t)(((uint16_t)rgb[(i * 3U) + 1U] * LOWCOST_WS2812_BRIGHTNESS_PERCENT) / 100U),
            (uint8_t)(((uint16_t)rgb[(i * 3U) + 0U] * LOWCOST_WS2812_BRIGHTNESS_PERCENT) / 100U),
            (uint8_t)(((uint16_t)rgb[(i * 3U) + 2U] * LOWCOST_WS2812_BRIGHTNESS_PERCENT) / 100U)
        };

        for (uint32_t c = 0U; c < 3U; c++)
        {
            uint8_t byte = colors[c];
            for (int32_t bit = 7; bit >= 0; bit--)
            {
                pwm_buffer[idx++] = ((byte & (1U << bit)) != 0U) ? WS2812_1 : WS2812_0;
            }
        }
    }

    while (idx < LED_BUFFER_SIZE)
    {
        pwm_buffer[idx++] = 0U;
    }
}

void board_led_transport_init(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_4);
    memset(pwm_buffer, 0, sizeof(pwm_buffer));
    dma_busy = 0U;
}

bool board_led_transport_busy(void)
{
    return (dma_busy != 0U);
}

void board_led_transport_send(const uint8_t *rgb, uint32_t count)
{
    if ((rgb == NULL) || (count == 0U) || (count > BOARD_LED_TRANSPORT_COUNT) || board_led_transport_busy())
    {
        return;
    }

    led_transport_encode(rgb, count);
    dcache_clean_by_addr_aligned(pwm_buffer, sizeof(pwm_buffer));
    dma_busy = 1U;

    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);

    if (HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_4, pwm_buffer, LED_BUFFER_SIZE) != HAL_OK)
    {
        dma_busy = 0U;
    }
}

uint8_t board_led_transport_handle_pwm_callback(void *handle)
{
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)handle;
    if ((htim == NULL) || (htim->Instance != TIM2))
    {
        return 0U;
    }

    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);
    dma_busy = 0U;
    return 1U;
}
