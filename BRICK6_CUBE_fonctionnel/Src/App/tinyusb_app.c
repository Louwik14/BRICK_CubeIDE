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

#define SPEAKER_RING_BUFFER_SIZE (4 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)

// 1 ms audio frame buffer
uint16_t test_buffer_audio[
  CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE / 1000 *
  CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX *
  CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX / 2
];

static uint8_t speaker_ring[SPEAKER_RING_BUFFER_SIZE];
static volatile uint32_t speaker_ring_head;
static volatile uint32_t speaker_ring_tail;

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

  uint32_t frames = CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE / 1000;
  for (uint32_t i = 0; i < frames; i++)
  {
    uint16_t val = startVal++;
    test_buffer_audio[2 * i] = val;
    test_buffer_audio[2 * i + 1] = val;
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
  startVal = 0;
  return true;
}

//--------------------------------------------------------------------+
// USB AUDIO RX (SPEAKER OUT)
//--------------------------------------------------------------------+

static uint32_t speaker_ring_available(void)
{
  uint32_t head = speaker_ring_head;
  uint32_t tail = speaker_ring_tail;
  if (head >= tail)
  {
    return head - tail;
  }
  return SPEAKER_RING_BUFFER_SIZE - (tail - head);
}

static uint32_t speaker_ring_free(void)
{
  return (SPEAKER_RING_BUFFER_SIZE - 1) - speaker_ring_available();
}

static void speaker_ring_write(const uint8_t *data, uint32_t len)
{
  uint32_t free_bytes = speaker_ring_free();
  if (len > free_bytes)
  {
    len = free_bytes;
  }

  while (len--)
  {
    speaker_ring[speaker_ring_head] = *data++;
    speaker_ring_head = (speaker_ring_head + 1) % SPEAKER_RING_BUFFER_SIZE;
  }
}

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

  static uint8_t rx_buffer[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];
  uint16_t remaining = n_bytes_received;

  while (remaining)
  {
    uint16_t chunk = tu_min16(remaining, sizeof(rx_buffer));
    uint16_t read_count = tud_audio_read(rx_buffer, chunk);
    if (read_count == 0)
    {
      break;
    }
    speaker_ring_write(rx_buffer, read_count);
    remaining = (uint16_t)(remaining - read_count);
  }

  return true;
}
