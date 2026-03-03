#include "sd_callbacks.h"

#include <stdio.h>

#include "bsp_driver_sd.h"
#include "sd_stream.h"
#include "sd_owner.h"

#define DBG(...) printf(__VA_ARGS__)

static volatile uint32_t g_sd_cb_rx_count = 0U;
static volatile uint32_t g_sd_cb_tx_count = 0U;
static volatile uint32_t g_sd_cb_err_count = 0U;
static uint32_t g_sd_cb_rx_last = 0U;
static uint32_t g_sd_cb_tx_last = 0U;
static uint32_t g_sd_cb_err_last = 0U;

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    switch(sd_get_owner())
    {
        case SD_OWNER_STREAM:
            sd_stream_rx_callback(hsd);
            break;

        case SD_OWNER_FATFS:
            BSP_SD_ReadCpltCallback();
            break;

        default:
            break;
    }

    g_sd_cb_rx_count++;
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    switch(sd_get_owner())
    {
        case SD_OWNER_STREAM:
            sd_stream_tx_callback(hsd);
            break;

        case SD_OWNER_FATFS:
            BSP_SD_WriteCpltCallback();
            break;

        default:
            break;
    }

    g_sd_cb_tx_count++;
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    switch(sd_get_owner())
    {
        case SD_OWNER_STREAM:
            sd_stream_error_callback(hsd);
            break;

        case SD_OWNER_FATFS:
        default:
            break;
    }

    g_sd_cb_err_count++;
}

void sd_callbacks_tasklet_poll(void)
{
    static uint32_t rx_throttle = 0U;
    static uint32_t tx_throttle = 0U;

    if(g_sd_cb_rx_count != g_sd_cb_rx_last)
    {
        g_sd_cb_rx_last = g_sd_cb_rx_count;
        if((rx_throttle++ % 500U) == 0U)
            DBG("[SD CALLBACK] RX\r\n");
    }

    if(g_sd_cb_tx_count != g_sd_cb_tx_last)
    {
        g_sd_cb_tx_last = g_sd_cb_tx_count;
        if((tx_throttle++ % 500U) == 0U)
            DBG("[SD CALLBACK] TX\r\n");
    }

    if(g_sd_cb_err_count != g_sd_cb_err_last)
    {
        g_sd_cb_err_last = g_sd_cb_err_count;
        DBG("[SD CALLBACK] ERR\r\n");
    }
}
