#include "led_rgb.h"
#include "tim.h"
#include "usart.h"

#include <string.h>

/*
TIM2 clock = 240 MHz
WS2812 = 800 kHz

Period = 240 MHz / 800 kHz = 300
ARR = 299
*/

#define LED_COUNT 25
#define BITS_PER_LED 24
#define RESET_SLOTS 80

#define WS2812_0 84
#define WS2812_1 168

#define LED_BUFFER_SIZE (LED_COUNT * BITS_PER_LED + RESET_SLOTS)

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_t;

static led_t leds[LED_COUNT];

static uint16_t pwm_buffer[LED_BUFFER_SIZE];

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

    debug("LED encode\n");
}


/* ------------------------------------------------ */

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM2)
    {
        HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_4);

        dma_busy = 0;

        debug("LED DMA done\n");
    }
}


/* ------------------------------------------------ */

void led_init(void)
{
    memset(leds,0,sizeof(leds));

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
    {
        debug("LED busy\n");
        return;
    }

    encode();

    dma_busy = 1;

    debug("LED DMA start\n");

    HAL_StatusTypeDef status;

    status = HAL_TIM_PWM_Start_DMA(
        &htim2,
        TIM_CHANNEL_4,
        (uint32_t*)pwm_buffer,
        LED_BUFFER_SIZE
    );

    if(status != HAL_OK)
    {
        debug("LED DMA ERROR\n");
        dma_busy = 0;
    }
}
