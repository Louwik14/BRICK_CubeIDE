#include "led_rgb.h"
#include "tim.h"
#include "usart.h"
#include <string.h>

/*
Timer config attendu (CubeMX):

TIM2
Prescaler = 0
ARR = 299

Timer clock = 240 MHz
PWM period = 1.25 µs (800 kHz)
*/

#define BITS_PER_LED 24
#define RESET_SLOTS 100

#define WS2812_0 84
#define WS2812_1 175

#define BUFFER_SIZE (LED_COUNT * BITS_PER_LED + RESET_SLOTS)
extern DMA_HandleTypeDef hdma_tim2_ch4;
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_t;

static led_t leds[LED_COUNT];

/* buffer DMA aligné cache H7 */
static uint32_t pwm_buffer[BUFFER_SIZE] __attribute__((aligned(32)));
static volatile uint8_t dma_busy = 0;

/* ------------------------------------------------ */

static void debug(const char *msg)
{
    HAL_UART_Transmit(&huart1,(uint8_t*)msg,strlen(msg),100);
}

/* ------------------------------------------------ */

static void encode(void)
{
    uint32_t idx = 0;

    for(int i=0;i<LED_COUNT;i++)
    {
        uint8_t colors[3] =
        {
            leds[i].g,
            leds[i].r,
            leds[i].b
        };

        for(int c=0;c<3;c++)
        {
            uint8_t byte = colors[c];

            for(int bit=7;bit>=0;bit--)
            {
                if(byte & (1<<bit))
                    pwm_buffer[idx++] = WS2812_1;
                else
                    pwm_buffer[idx++] = WS2812_0;
            }
        }
    }

    for(int i=0;i<RESET_SLOTS;i++)
        pwm_buffer[idx++] = 0;
}

/* ------------------------------------------------ */

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM2)
    {
        HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);
        dma_busy = 0;
    }
}
/* ------------------------------------------------ */

void led_init(void)
{
    memset(leds,0,sizeof(leds));

    /* assure timer stoppé */
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_4);

    debug("LED init\n");
}

/* ------------------------------------------------ */

void led_set(uint32_t id,uint8_t r,uint8_t g,uint8_t b)
{
    if(id >= LED_COUNT)
        return;

    leds[id].r = r;
    leds[id].g = g;
    leds[id].b = b;
}

/* ------------------------------------------------ */

void led_fill(uint8_t r,uint8_t g,uint8_t b)
{
    for(int i=0;i<LED_COUNT;i++)
    {
        leds[i].r = r;
        leds[i].g = g;
        leds[i].b = b;
    }
}

/* ------------------------------------------------ */

void led_clear(void)
{
    led_fill(0,0,0);
}

/* ------------------------------------------------ */

void led_show(void)
{
    if(dma_busy)
        return;

    encode();

#if (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanDCache_by_Addr(
            (uint32_t*)pwm_buffer,
            sizeof(pwm_buffer)
        );
    }
#endif

    dma_busy = 1;

    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);

    __HAL_TIM_SET_COUNTER(&htim2,0);

    HAL_StatusTypeDef status = HAL_TIM_PWM_Start_DMA(
        &htim2,
        TIM_CHANNEL_4,
        (uint32_t*)pwm_buffer,
        BUFFER_SIZE
    );

    if(status != HAL_OK)
    {
        dma_busy = 0;
    }
}
