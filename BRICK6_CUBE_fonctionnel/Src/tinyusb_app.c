/**
 * @file tinyusb_app.c
 * @brief TinyUSB UAC1 Speaker backend (RX only) + FIFO feedback + HID debug
 */
#include <stdio.h>
#include <string.h>
#include "audio_io_usb.h"
#include "audio_test_pcm4104.h"
#include "tusb.h"
#include "tinyusb_app.h"
#include "usb_descriptors.h"
#include "stm32h7xx_hal.h"
#include "common_types.h"
#include "diagnostics_tasklet.h"

//--------------------------------------------------------------------+
// AUDIO STATE (UAC1 RX)
//--------------------------------------------------------------------+

uint8_t  mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];
int16_t  volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];
uint32_t sampFreq = CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE_FS;

//--------------------------------------------------------------------+
// RX DIAGNOSTICS
//--------------------------------------------------------------------+

static volatile uint32_t usb_rx_done_count     = 0;
static volatile uint32_t usb_rx_bytes_total   = 0;
static volatile uint32_t usb_rx_samples_total = 0;
static volatile uint32_t usb_rx_zero_reads    = 0;

//--------------------------------------------------------------------+
// FIFO DEBUG / FEEDBACK
//--------------------------------------------------------------------+

static volatile uint16_t fifo_count     = 0;
static volatile uint32_t fifo_count_avg = 0;
static uint8_t current_alt_settings     = 0;

//--------------------------------------------------------------------+
// INIT / TASK
//--------------------------------------------------------------------+

void tinyusb_app_init(void)
{
	  diagnostics_log("tinyusb_app_init() called\r\n");
	  audio_io_usb_init();
}

void tinyusb_app_task(void)
{
  // ARMEMENT OBLIGATOIRE DE L'EP OUT AUDIO
  uint8_t tmp[64];

  while (tud_audio_available())
  {
    tud_audio_read(tmp, sizeof(tmp));
  }
}


//--------------------------------------------------------------------+
// UAC1 FEATURE UNIT CALLBACKS
//--------------------------------------------------------------------+

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request,
                                 uint8_t *pBuff)
{
  (void) rhport;

  uint8_t channelNum = TU_U16_LOW (p_request->wValue);
  uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

  if (entityID != UAC1_ENTITY_FEATURE_UNIT)
    return false;

  switch (ctrlSel)
  {
    case AUDIO10_FU_CTRL_MUTE:
      mute[channelNum] = pBuff[0];
      return true;

    case AUDIO10_FU_CTRL_VOLUME:
      volume[channelNum] =
        (int16_t)(tu_unaligned_read16(pBuff) / 256);
      return true;

    default:
      return false;
  }
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request)
{
  uint8_t channelNum = TU_U16_LOW (p_request->wValue);
  uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

  if (entityID != UAC1_ENTITY_FEATURE_UNIT)
    return false;

  switch (ctrlSel)
  {
    case AUDIO10_FU_CTRL_MUTE:
      return tud_audio_buffer_and_schedule_control_xfer(
        rhport, p_request, &mute[channelNum], 1);

    case AUDIO10_FU_CTRL_VOLUME:
    {
      int16_t vol = volume[channelNum] * 256;
      return tud_audio_buffer_and_schedule_control_xfer(
        rhport, p_request, &vol, sizeof(vol));
    }

    default:
      return false;
  }
}

//--------------------------------------------------------------------+
// INTERFACE CALLBACK
//--------------------------------------------------------------------+

bool tud_audio_set_itf_cb(uint8_t rhport,
                          tusb_control_request_t const *p_request)
{
  (void) rhport;

  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  // Log dynamique comme l’exemple
  diagnostics_logf("SET_INTERFACE itf=%u alt=%u\r\n", itf, alt);

  if (itf == ITF_NUM_AUDIO_STREAMING)
  {
#if CFG_AUDIO_DEBUG
    current_alt_settings = alt;
#endif

    if (alt == 1U)
    {
      diagnostics_log("AUDIO STREAM START\r\n");
      audio_test_pcm4104_start();
    }
    else
    {
      diagnostics_log("AUDIO STREAM STOP\r\n");
      audio_test_pcm4104_stop();
    }
  }

  return true;
}



//--------------------------------------------------------------------+
// USB AUDIO RX (REAL DATA PATH)
//--------------------------------------------------------------------+

bool tud_audio_rx_done_isr(uint8_t rhport,
                           uint16_t n_bytes_received,
                           uint8_t func_id,
                           uint8_t ep_out,
                           uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) func_id;
  (void) ep_out;
  (void) cur_alt_setting;

  static int16_t rx_buffer[
    CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX / sizeof(int16_t)];
  static int32_t rx_converted[
    CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX / sizeof(int16_t)];

  uint16_t remaining = n_bytes_received;

  usb_rx_done_count++;
  usb_rx_bytes_total += n_bytes_received;

  while (remaining)
  {
    uint16_t chunk = tu_min16(remaining, sizeof(rx_buffer));
    uint16_t read  = tud_audio_read((uint8_t*)rx_buffer, chunk);

    if (read == 0)
    {
      usb_rx_zero_reads++;
      break;
    }

    uint32_t samples = read / sizeof(int16_t);
    usb_rx_samples_total += samples;

    for (uint32_t i = 0; i < samples; i++)
    {
      rx_converted[i] = ((int32_t)rx_buffer[i]) << 8;
    }

    audio_io_usb_on_rx_samples(rx_converted, samples);
    remaining -= read;
  }

  fifo_count = tud_audio_available();
  fifo_count_avg =
    (uint32_t)(((uint64_t)fifo_count_avg * 63 +
               ((uint32_t)fifo_count << 16)) >> 6);

  return true;
}

//--------------------------------------------------------------------+
// FIFO FEEDBACK (AUTO)
//--------------------------------------------------------------------+

void tud_audio_feedback_params_cb(uint8_t func_id,
                                  uint8_t alt_itf,
                                  audio_feedback_params_t *feedback_param)
{
  (void) func_id;
  (void) alt_itf;

  feedback_param->method      = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  feedback_param->sample_freq = sampFreq;
}

//--------------------------------------------------------------------+
// HID AUDIO DEBUG
//--------------------------------------------------------------------+

void tinyusb_app_audio_debug_task(void)
{
  audio_debug_info_t info;

  info.sample_rate    = sampFreq;
  info.alt_settings   = current_alt_settings;
  info.fifo_size      = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ;
  info.fifo_count     = fifo_count;
  info.fifo_count_avg = (uint16_t)(fifo_count_avg >> 16);

  for (int i = 0; i < CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1; i++)
  {
    info.mute[i]   = mute[i];
    info.volume[i] = volume[i];
  }

  if (tud_hid_ready())
    tud_hid_report(0, &info, sizeof(info));
}

uint16_t tud_hid_get_report_cb(uint8_t itf,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t itf,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;
}


//--------------------------------------------------------------------+
// DIAGNOSTICS GETTERS
//--------------------------------------------------------------------+

uint32_t tinyusb_app_get_rx_done_count(void)     { return usb_rx_done_count; }
uint32_t tinyusb_app_get_rx_bytes_total(void)   { return usb_rx_bytes_total; }
uint32_t tinyusb_app_get_rx_samples_total(void) { return usb_rx_samples_total; }
uint32_t tinyusb_app_get_rx_zero_reads(void)    { return usb_rx_zero_reads; }
