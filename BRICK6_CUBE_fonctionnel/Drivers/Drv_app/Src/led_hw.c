#include "led_hw.h"

#include "tim.h"
#include "usart.h"

#include <stddef.h>
#include <string.h>

#define LED_HW_BITS_PER_LED 24U
#define LED_HW_RESET_SLOTS 100U

#define WS2812_0 84U
#define WS2812_1 175U

#define LED_HW_BUFFER_SIZE ((LED_HW_COUNT * LED_HW_BITS_PER_LED) + LED_HW_RESET_SLOTS)

static uint32_t pwm_buffer[LED_HW_BUFFER_SIZE] __attribute__((aligned(32)));
static volatile uint8_t dma_busy = 0U;

static void led_hw_debug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100U);
}

static void led_hw_encode(const uint8_t *rgb, uint32_t count)
{
    uint32_t idx = 0U;

    for (uint32_t i = 0U; i < count; i++)
    {
        const uint8_t colors[3] =
        {
            rgb[(i * 3U) + 1U],
            rgb[(i * 3U) + 0U],
            rgb[(i * 3U) + 2U]
        };

        for (uint32_t c = 0U; c < 3U; c++)
        {
            uint8_t byte = colors[c];

            for (int32_t bit = 7; bit >= 0; bit--)
            {
                if ((byte & (1U << bit)) != 0U)
                {
                    pwm_buffer[idx++] = WS2812_1;
                }
                else
                {
                    pwm_buffer[idx++] = WS2812_0;
                }
            }
        }
    }

    while (idx < LED_HW_BUFFER_SIZE)
    {
        pwm_buffer[idx++] = 0U;
    }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);
        dma_busy = 0U;
    }
}

void led_hw_init(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_4);
    memset(pwm_buffer, 0, sizeof(pwm_buffer));
    dma_busy = 0U;
    led_hw_debug("LED HW init\n");
}

bool led_hw_busy(void)
{
    return (dma_busy != 0U);
}

void led_hw_send(uint8_t *rgb, uint32_t count)
{
    if ((rgb == NULL) || (count == 0U) || (count > LED_HW_COUNT) || led_hw_busy())
    {
        return;
    }

    led_hw_encode(rgb, count);

#if (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanDCache_by_Addr((uint32_t *)pwm_buffer, (int32_t)sizeof(pwm_buffer));
    }
#endif

    dma_busy = 1U;

    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);

    if (HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_4, pwm_buffer, LED_HW_BUFFER_SIZE) != HAL_OK)
    {
        dma_busy = 0U;
    }
}
