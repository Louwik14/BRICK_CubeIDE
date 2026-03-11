#include <string.h>
#include "tusb.h"
#include "App/tinyusb_app.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// AUDIO STATE
//--------------------------------------------------------------------+

bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint32_t sampFreq;
uint8_t clkValid;

audio20_control_range_2_n_t(1) volumeRng[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
audio20_control_range_4_n_t(1) sampleFreqRng;

#define SPEAKER_RING_BUFFER_SIZE (16 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)
#define CAPTURE_RING_BUFFER_SIZE (16 * CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)

static uint8_t speaker_ring[SPEAKER_RING_BUFFER_SIZE];
static volatile uint32_t speaker_ring_head;
static volatile uint32_t speaker_ring_tail;

static uint8_t capture_ring[CAPTURE_RING_BUFFER_SIZE];
static volatile uint32_t capture_ring_head;
static volatile uint32_t capture_ring_tail;

static volatile tinyusb_audio_debug_stats_t g_tinyusb_audio_stats;

//--------------------------------------------------------------------+
// RING BUFFER HELPERS
//--------------------------------------------------------------------+

static uint32_t ring_available(uint32_t head, uint32_t tail, uint32_t size)
{
  if (head >= tail)
  {
    return head - tail;
  }
  return size - (tail - head);
}

static uint32_t ring_free(uint32_t head, uint32_t tail, uint32_t size)
{
  return (size - 1U) - ring_available(head, tail, size);
}

static uint32_t ring_write(uint8_t *ring,
                           volatile uint32_t *head,
                           volatile uint32_t *tail,
                           uint32_t size,
                           const uint8_t *data,
                           uint32_t len)
{
  uint32_t wr = len;
  uint32_t free_bytes = ring_free(*head, *tail, size);
  if (wr > free_bytes)
  {
    wr = free_bytes;
  }

  for (uint32_t i = 0; i < wr; i++)
  {
    ring[*head] = data[i];
    *head = (*head + 1U) % size;
  }

  return wr;
}

static uint32_t ring_read(uint8_t *ring,
                          volatile uint32_t *head,
                          volatile uint32_t *tail,
                          uint32_t size,
                          uint8_t *data,
                          uint32_t len)
{
  uint32_t rd = len;
  uint32_t available = ring_available(*head, *tail, size);
  if (rd > available)
  {
    rd = available;
  }

  for (uint32_t i = 0; i < rd; i++)
  {
    data[i] = ring[*tail];
    *tail = (*tail + 1U) % size;
  }

  return rd;
}

static uint16_t tinyusb_audio_write_available(void)
{
  tu_fifo_t *ff = tud_audio_get_ep_in_ff();
  if (ff == NULL)
  {
    return 0U;
  }

  return tu_fifo_remaining(ff);
}

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

  speaker_ring_head = 0U;
  speaker_ring_tail = 0U;
  capture_ring_head = 0U;
  capture_ring_tail = 0U;
  memset((void*)&g_tinyusb_audio_stats, 0, sizeof(g_tinyusb_audio_stats));
}

static void audio_task(void)
{
  static uint8_t tx_chunk[CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX];
  uint32_t available = ring_available(capture_ring_head,
                                      capture_ring_tail,
                                      CAPTURE_RING_BUFFER_SIZE);

  while (available != 0U)
  {
    uint16_t write_available = tinyusb_audio_write_available();
    if (write_available == 0U)
    {
      break;
    }

    uint32_t chunk = available;
    if (chunk > sizeof(tx_chunk))
    {
      chunk = sizeof(tx_chunk);
    }
    if (chunk > write_available)
    {
      chunk = write_available;
    }

    uint32_t read_count = ring_read(capture_ring,
                                    &capture_ring_head,
                                    &capture_ring_tail,
                                    CAPTURE_RING_BUFFER_SIZE,
                                    tx_chunk,
                                    chunk);

    if (read_count == 0U)
    {
      break;
    }

    uint16_t write_count = tud_audio_write(tx_chunk, (uint16_t)read_count);
    if (write_count < read_count)
    {
      uint32_t pushback = (uint32_t)(read_count - write_count);
      uint32_t rewrote = ring_write(capture_ring,
                                    &capture_ring_head,
                                    &capture_ring_tail,
                                    CAPTURE_RING_BUFFER_SIZE,
                                    tx_chunk + write_count,
                                    pushback);
      g_tinyusb_audio_stats.capture_drop_bytes += (pushback - rewrote);
      break;
    }

    available = ring_available(capture_ring_head,
                               capture_ring_tail,
                               CAPTURE_RING_BUFFER_SIZE);
  }
}

void tinyusb_app_task(void)
{
  audio_task();
}

uint32_t tinyusb_capture_write_stereo_s16(const int16_t *interleaved, uint32_t frames)
{
  uint32_t bytes = frames * sizeof(int16_t) * 2U;
  uint32_t written = ring_write(capture_ring,
                                &capture_ring_head,
                                &capture_ring_tail,
                                CAPTURE_RING_BUFFER_SIZE,
                                (const uint8_t *)interleaved,
                                bytes);
  g_tinyusb_audio_stats.capture_overflow_bytes += (bytes - written);
  return written;
}

void tinyusb_app_note_playback_underrun(uint32_t missing_bytes)
{
  g_tinyusb_audio_stats.playback_underrun_bytes += missing_bytes;
}

void tinyusb_app_get_debug_stats(tinyusb_audio_debug_stats_t *out_stats)
{
  if (out_stats == NULL)
  {
    return;
  }

  out_stats->speaker_overflow_bytes = g_tinyusb_audio_stats.speaker_overflow_bytes;
  out_stats->capture_overflow_bytes = g_tinyusb_audio_stats.capture_overflow_bytes;
  out_stats->capture_drop_bytes = g_tinyusb_audio_stats.capture_drop_bytes;
  out_stats->playback_underrun_bytes = g_tinyusb_audio_stats.playback_underrun_bytes;
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
  return true;
}

//--------------------------------------------------------------------+
// USB AUDIO RX (SPEAKER OUT)
//--------------------------------------------------------------------+

uint32_t speaker_ring_read(uint8_t *data, uint32_t len)
{
  return ring_read(speaker_ring,
                   &speaker_ring_head,
                   &speaker_ring_tail,
                   SPEAKER_RING_BUFFER_SIZE,
                   data,
                   len);
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
    uint32_t written = ring_write(speaker_ring,
                                 &speaker_ring_head,
                                 &speaker_ring_tail,
                                 SPEAKER_RING_BUFFER_SIZE,
                                 rx_buffer,
                                 read_count);
    g_tinyusb_audio_stats.speaker_overflow_bytes += (uint32_t)read_count - written;
    remaining = (uint16_t)(remaining - read_count);
  }

  return true;
}
