
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
#include "wav_loader.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define BRICK6_HAS_FATFS 1
#  endif
#endif
#ifndef BRICK6_HAS_FATFS
#define BRICK6_HAS_FATFS 0
#endif

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

static uint16_t wav_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t wav_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool stream_wav_start_from_sd(void)
{
#if BRICK6_HAS_FATFS
    char wav_path[64];
    FIL fp;
    UINT br = 0U;
    uint8_t riff[12];
    uint32_t data_offset = 0U;
    uint32_t data_size = 0U;

    if(!wav_loader_find_first_wav(wav_path, sizeof(wav_path)))
        return false;

    if(f_open(&fp, wav_path, FA_READ) != FR_OK)
        return false;

    if((f_read(&fp, riff, sizeof(riff), &br) != FR_OK) || (br != sizeof(riff)))
    {
        (void)f_close(&fp);
        return false;
    }

    if((memcmp(&riff[0], "RIFF", 4) != 0) || (memcmp(&riff[8], "WAVE", 4) != 0))
    {
        (void)f_close(&fp);
        return false;
    }

    while(f_tell(&fp) + 8U <= f_size(&fp))
    {
        uint8_t chunk_header[8];
        uint32_t chunk_size;

        if((f_read(&fp, chunk_header, sizeof(chunk_header), &br) != FR_OK) || (br != sizeof(chunk_header)))
        {
            (void)f_close(&fp);
            return false;
        }

        chunk_size = wav_le32(&chunk_header[4]);

        if(memcmp(&chunk_header[0], "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            uint16_t format;
            uint16_t channels;
            uint32_t sample_rate;

            if(chunk_size < 16U)
            {
                (void)f_close(&fp);
                return false;
            }

            if((f_read(&fp, fmt, sizeof(fmt), &br) != FR_OK) || (br != sizeof(fmt)))
            {
                (void)f_close(&fp);
                return false;
            }

            format = wav_le16(&fmt[0]);
            channels = wav_le16(&fmt[2]);
            sample_rate = wav_le32(&fmt[4]);

            if((format != 1U) || (channels != 2U) || (sample_rate != 48000U))
            {
                (void)f_close(&fp);
                return false;
            }

            if(chunk_size > sizeof(fmt))
            {
                if(f_lseek(&fp, f_tell(&fp) + (chunk_size - sizeof(fmt))) != FR_OK)
                {
                    (void)f_close(&fp);
                    return false;
                }
            }
        }
        else if(memcmp(&chunk_header[0], "data", 4) == 0)
        {
            data_offset = f_tell(&fp);
            data_size = chunk_size;
            break;
        }
        else
        {
            if(f_lseek(&fp, f_tell(&fp) + chunk_size) != FR_OK)
            {
                (void)f_close(&fp);
                return false;
            }
        }

        if((chunk_size & 1U) != 0U)
        {
            if(f_lseek(&fp, f_tell(&fp) + 1U) != FR_OK)
            {
                (void)f_close(&fp);
                return false;
            }
        }
    }

    (void)f_close(&fp);

    if(data_size == 0U)
        return false;

    {
        uint32_t start_block = data_offset / SD_STREAM_BLOCK_SIZE_BYTES;
        uint32_t intra = data_offset % SD_STREAM_BLOCK_SIZE_BYTES;
        uint32_t total_blocks = (intra + data_size + (SD_STREAM_BLOCK_SIZE_BYTES - 1U)) / SD_STREAM_BLOCK_SIZE_BYTES;

        DBG("[STREAM] start wav streaming\r\n");
        if(sd_stream_start_read(start_block, total_blocks) != HAL_OK)
            return false;

        DBG("[STREAM] reading blocks...\r\n");
        return true;
    }
#else
    return false;
#endif
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

        if(!stream_wav_start_from_sd())
            DBG("[STREAM] start wav streaming error\r\n");
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
    DBG("[SAMPLER] init\r\n");
    g_sampler_voice.gainL = 0.35f;
    g_sampler_voice.gainR = 0.35f;
    g_sampler_voice.loop = true;
    g_sampler_voice.loop_start = 0U;

    {
        wav_info_t wav_info;
        char wav_path[64];

        if(wav_loader_find_first_wav(wav_path, sizeof(wav_path)))
        {
            DBG("[WAV] found: %s\r\n", wav_path);

            if(wav_loader_load_to_sdram(wav_path, &wav_info))
            {
                const float *buffer = wav_loader_get_interleaved_buffer();

                DBG("[WAV] load ok frames=%lu\r\n", (unsigned long)wav_info.frames_loaded);
                DBG("[WAV] first L=%f R=%f\r\n", buffer[0], buffer[1]);

                g_sampler_voice.loop_end = wav_info.frames_loaded;

                sample_voice_trigger(&g_sampler_voice,
                                     buffer,
                                     wav_info.frames_loaded);

                DBG("[SAMPLER] trigger active=%d len=%lu\r\n",
                    g_sampler_voice.active,
                    (unsigned long)g_sampler_voice.length);
            }
            else
            {
                DBG("[WAV] load failed\r\n");
            }
        }
        else
        {
            DBG("[WAV] no WAV found\r\n");
        }
    }

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
