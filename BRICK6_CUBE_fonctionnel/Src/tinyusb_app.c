#include <string.h>
#include "tusb.h"
#include "tinyusb_app.h"
#include "usb_descriptors.h"
#include "stm32h7xx_hal.h"

//--------------------------------------------------------------------+
// AUDIO STATE
//--------------------------------------------------------------------+

bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint32_t sampFreq;
uint8_t clkValid;

audio20_control_range_2_n_t(1) volumeRng[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
audio20_control_range_4_n_t(1) sampleFreqRng;

// 1 ms audio frame buffer
uint16_t test_buffer_audio[
  CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE / 1000 *
  CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX *
  CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX / 2
];


static uint16_t startVal = 0;

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

static void audio_task(void)
{
  static uint32_t last_ms = 0;
  uint32_t now = HAL_GetTick();
  if (now == last_ms) return;
  last_ms = now;

  for (uint32_t i = 0; i < TU_ARRAY_SIZE(test_buffer_audio); i++)
  {
    test_buffer_audio[i] = startVal++;
  }

  tud_audio_write((uint8_t*)test_buffer_audio,
                  sizeof(test_buffer_audio));
}

void tinyusb_app_task(void)
{
  audio_task();
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

  if (entityID == 2) // Feature Unit
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

  if (entityID == 2) // Feature Unit
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
  startVal = 0;
  return true;
}
