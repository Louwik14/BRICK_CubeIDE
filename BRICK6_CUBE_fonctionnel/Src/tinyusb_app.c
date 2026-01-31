#include <string.h>
#include "tusb.h"
#include "tinyusb_app.h"
#include "usb_descriptors.h"
#include "stm32h7xx_hal.h"
#include "audio_io_usb.h"

//--------------------------------------------------------------------+
// AUDIO STATE
//--------------------------------------------------------------------+

bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint32_t sampFreq;
uint8_t clkValid;

audio20_control_range_2_n_t(1) volumeRng[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
audio20_control_range_4_n_t(1) sampleFreqRng;

//--------------------------------------------------------------------+
// INIT / TASK
//--------------------------------------------------------------------+

void tinyusb_app_init(void)
{
  sampFreq = CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE;
  clkValid = 1;

  sampleFreqRng.wNumSubRanges = 1;
  sampleFreqRng.subrange[0].bMin = sampFreq;
  sampleFreqRng.subrange[0].bMax = sampFreq;
  sampleFreqRng.subrange[0].bRes = 0;
}

void tinyusb_app_task(void)
{
  audio_io_usb_task();
}

//--------------------------------------------------------------------+
// USB AUDIO CALLBACKS (UAC1 SAFE)
//--------------------------------------------------------------------+

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request,
                                 uint8_t *pBuff)
{
  (void) rhport;

  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

  TU_VERIFY(p_request->bRequest == AUDIO20_CS_REQ_CUR);

  if (entityID == 2 || entityID == 5) // Feature Units
  {
    switch (ctrlSel)
    {
      case AUDIO20_FU_CTRL_MUTE:
        mute[channelNum] = ((audio20_control_cur_1_t*)pBuff)->bCur;
        return true;

      case AUDIO20_FU_CTRL_VOLUME:
        volume[channelNum] =
          (uint16_t)((audio20_control_cur_2_t*)pBuff)->bCur;
        return true;

      default:
        return false;
    }
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request)
{
  (void) rhport;

  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel    = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID   = TU_U16_HIGH(p_request->wIndex);

  if (entityID == 2 || entityID == 5) // Feature Units
  {
    switch (ctrlSel)
    {
      case AUDIO20_FU_CTRL_MUTE:
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &mute[channelNum], 1);

      case AUDIO20_FU_CTRL_VOLUME:
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &volume[channelNum], sizeof(uint16_t));

      default:
        return false;
    }
  }

  if (entityID == 4) // Clock Source
  {
    switch (ctrlSel)
    {
      case AUDIO20_CS_CTRL_SAM_FREQ:
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &sampFreq, sizeof(sampFreq));

      case AUDIO20_CS_CTRL_CLK_VALID:
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &clkValid, sizeof(clkValid));

      default:
        return false;
    }
  }

  return false;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                  tusb_control_request_t const *p_request)
{
  (void) rhport;
  (void) p_request;
  audio_io_usb_reset();
  return true;
}

//--------------------------------------------------------------------+
// USB AUDIO RX (SPEAKER OUT)
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
  return audio_io_usb_rx_done_isr(n_bytes_received);
}
