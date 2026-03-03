
/**
 * @file brick6_app_init.c
 */

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "brick6_app_init.h"

#include "engine_tasklet.h"
#include "midi.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_uart.h"
#include "usb_host.h"
#include "usb_device.h"
#include "audio.h"
#include "audio_float.h"
#include "cs42448.h"
#include "mixer.h"
#include "fx_pool.h"
#include "param_store.h"
#include "control_events.h"
#include "sampler.h"
#include "sampler_stream.h"

#define DBG(...) printf(__VA_ARGS__)
#define FORCE_TONE_TEST 0

static sample_voice_t g_sampler_voice;
static UART_HandleTypeDef huart1;
static float g_master_gain = 1.0f;

/* ============================================================
   UART DEBUG (hardcoded)
   ============================================================ */

static void debug_uart_init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // PA9 = USART1_TX
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart1);
}

/* printf → UART */
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* ============================================================
   DSP CALLBACK
   ============================================================ */

static void my_dsp(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames)
{

#if FORCE_TONE_TEST
    for(uint32_t i = 0U; i < frames; i++)
    {
        tracks[0].L[i] = 0.2f;
        tracks[0].R[i] = 0.2f;
    }
    return;
#endif

    if((track_count > 0U) && (tracks[0].enabled != 0U))
    {
        sample_voice_process(&g_sampler_voice, tracks[0].L, tracks[0].R, frames);

        for(uint32_t i = 0U; i < frames; i++)
        {
            float l = tracks[0].L[i] * g_master_gain;
            float r = tracks[0].R[i] * g_master_gain;

            /* Safety mute on invalid floating-point samples. */
            if(!isfinite(l) || !isfinite(r))
            {
                l = 0.0f;
                r = 0.0f;
            }

            tracks[0].L[i] = l;
            tracks[0].R[i] = r;
        }
    }

    mixer_process(tracks, track_count, frames);
}

/* ============================================================
   INIT APP
   ============================================================ */

void brick6_app_init(void)
{
    debug_uart_init();
    printf("\r\n[BOOT] UART OK\r\n");

    SDRAM_Init();
    MX_USB_DEVICE_Init();
    MX_USB_HOST_Init();

    if(sd_stream_init(&hsd1) == HAL_OK)
    {
        DBG("[SD] init ok\r\n");

        {
            uint32_t start_block = 0U;
            uint32_t total_blocks = 2048U;

            DBG("[STREAM] start wav streaming\r\n");

            if(sd_stream_start_read(start_block, total_blocks) == HAL_OK)
                DBG("[STREAM] reading blocks...\r\n");
            else
                DBG("[STREAM] start wav streaming error\r\n");
        }
    }
    else
    {
        DBG("[SD] init error\r\n");
    }

    CS42448_Init(0x48);

    mixer_init();
    fx_pool_init();
    (void)fx_pool_activate_slot(0U, FX_EQ3);
    (void)fx_pool_activate_slot(1U, FX_SAT);
    (void)fx_pool_activate_slot(2U, FX_DAISY_COMP);
    mixer_set_track_insert_slot(0U, 0U, 2);
    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    audio_tracks_init();

    sample_voice_init(&g_sampler_voice);
    sampler_stream_init();
    DBG("[SAMPLER] init\r\n");
    g_sampler_voice.gainL = 0.35f;
    g_sampler_voice.gainR = 0.35f;
    g_sampler_voice.loop = true;
    g_sampler_voice.loop_start = 0U;
    g_sampler_voice.active = true;

    mixer_set_master(2.0f);

    track_enable(0, 1U);
    track_enable(1, 1U);
    track_enable(2, 1U);

    track_set_gain(0, 1.0f);
    track_set_gain(1, 1.0f);
    track_set_gain(2, 1.0f);

    audio_init(&hsai_BlockA1, &hsai_BlockB1);
    audio_set_float_callback(my_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    control_event_init();

    audio_start();

    HAL_Delay(200);

    midi_init();
}
