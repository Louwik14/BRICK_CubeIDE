#include "usb_audio.h"

#include <string.h>

#include "IPC/usb_audio_pcm_ring.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"
#include "tusb.h"

#define USB_AUDIO_AS_OUT_INTERFACE       1U
#define USB_AUDIO_AS_IN_INTERFACE        2U
#define USB_AUDIO_CLOCK_SOURCE_ID        0x10U
#define USB_AUDIO_SAMPLE_RATE_HZ         48000U
#define USB_AUDIO_CHANNELS               2U
#define USB_AUDIO_BYTES_PER_SAMPLE       4U
#define USB_AUDIO_BYTES_PER_FRAME        (USB_AUDIO_CHANNELS * USB_AUDIO_BYTES_PER_SAMPLE)
#define USB_AUDIO_SERVICE_MAX_FRAMES     96U
#define USB_AUDIO_SAMPLES_PER_USB_FRAME  (USB_AUDIO_SAMPLE_RATE_HZ / 1000U)
#define USB_AUDIO_FEEDBACK_NOMINAL       (USB_AUDIO_SAMPLES_PER_USB_FRAME << 16)
#define USB_AUDIO_FEEDBACK_GAIN_DENOM    256U
#define USB_AUDIO_FEEDBACK_CORRECTION_LIMIT (3U << 14) /* +/-0.75 sample/frame */
#define USB_AUDIO_FEEDBACK_MIN           (USB_AUDIO_FEEDBACK_NOMINAL \
                                         - USB_AUDIO_FEEDBACK_CORRECTION_LIMIT)
#define USB_AUDIO_FEEDBACK_MAX           (USB_AUDIO_FEEDBACK_NOMINAL \
                                         + USB_AUDIO_FEEDBACK_CORRECTION_LIMIT)

static volatile uint8_t g_usb_audio_out_active;
static volatile uint8_t g_usb_audio_in_active;
static uint8_t g_usb_audio_out_ready;
static uint8_t g_usb_audio_in_ready;
static ALIGN32 int32_t g_usb_audio_scratch[USB_AUDIO_SERVICE_MAX_FRAMES * USB_AUDIO_CHANNELS];

static void usb_audio_feedback_update(void)
{
    const uint32_t fill = usb_audio_pcm_pc_to_brick_available();
    int32_t error = (int32_t)USB_AUDIO_PCM_RING_TARGET_FRAMES - (int32_t)fill;
    int64_t feedback = (int64_t)USB_AUDIO_FEEDBACK_NOMINAL;

    feedback += ((int64_t)error * 65536LL) /
                (int64_t)USB_AUDIO_FEEDBACK_GAIN_DENOM;
    if (feedback < (int64_t)USB_AUDIO_FEEDBACK_MIN) {
        feedback = (int64_t)USB_AUDIO_FEEDBACK_MIN;
    }
    if (feedback > (int64_t)USB_AUDIO_FEEDBACK_MAX) {
        feedback = (int64_t)USB_AUDIO_FEEDBACK_MAX;
    }
    (void)tud_audio_n_fb_set(0U, (uint32_t)feedback);
}

static void usb_audio_reset_cursors(void)
{
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_usb_audio_out_ready = 0U;
    g_usb_audio_in_ready = 0U;
    usb_audio_pcm_reset();
    __DMB();
    __set_PRIMASK(primask);
}

void usb_audio_transport_reset(void)
{
    g_usb_audio_out_active = 0U;
    g_usb_audio_in_active = 0U;
    __DMB();
    usb_audio_reset_cursors();
}

void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting)
{
    if ((interface_number != USB_AUDIO_AS_OUT_INTERFACE)
        && (interface_number != USB_AUDIO_AS_IN_INTERFACE)) {
        return;
    }

    if ((g_usb_audio_out_active == 0U) && (g_usb_audio_in_active == 0U)) {
        usb_audio_reset_cursors();
    }

    if (interface_number == USB_AUDIO_AS_OUT_INTERFACE) {
        g_usb_audio_out_active = (alternate_setting != 0U) ? 1U : 0U;
    } else {
        g_usb_audio_in_active = (alternate_setting != 0U) ? 1U : 0U;
    }
    __DMB();

    if (alternate_setting != 0U) {
        usb_audio_feedback_update();
    }
}

void usb_audio_transport_close_interface(uint8_t interface_number)
{
    if (interface_number == USB_AUDIO_AS_OUT_INTERFACE) {
        g_usb_audio_out_active = 0U;
    } else if (interface_number == USB_AUDIO_AS_IN_INTERFACE) {
        g_usb_audio_in_active = 0U;
    } else {
        return;
    }

    __DMB();
    usb_audio_reset_cursors();
}

uint8_t usb_audio_audio_input_active(void)
{
    return g_usb_audio_out_active;
}

uint8_t usb_audio_audio_output_active(void)
{
    return g_usb_audio_in_active;
}

uint32_t usb_audio_audio_read(int32_t *interleaved, uint32_t frames)
{
    uint32_t available;
    uint32_t read_frames;

    if ((interleaved == NULL) || (frames == 0U)
        || (g_usb_audio_out_active == 0U)) {
        return 0U;
    }

    available = usb_audio_pcm_pc_to_brick_available();
    if (g_usb_audio_out_ready == 0U) {
        if (available < USB_AUDIO_PCM_RING_START_FRAMES) {
            return 0U;
        }
        g_usb_audio_out_ready = 1U;
    }

    read_frames = usb_audio_pcm_read_pc_to_brick(interleaved, frames);
    if (read_frames < frames) {
        g_usb_audio_out_ready = 0U;
        memset(interleaved, 0, frames * USB_AUDIO_BYTES_PER_FRAME);
    }
    return frames;
}

uint32_t usb_audio_audio_write(const int32_t *interleaved, uint32_t frames)
{
    if ((interleaved == NULL) || (frames == 0U)
        || (g_usb_audio_in_active == 0U)) {
        return 0U;
    }
    return usb_audio_pcm_write_brick_to_pc(interleaved, frames);
}

void usb_audio_transport_process(void)
{
    if (g_usb_audio_out_active != 0U) {
        uint16_t available = tud_audio_n_available(0U);
        uint32_t bytes = available;

        if (bytes > (USB_AUDIO_SERVICE_MAX_FRAMES * USB_AUDIO_BYTES_PER_FRAME)) {
            bytes = USB_AUDIO_SERVICE_MAX_FRAMES * USB_AUDIO_BYTES_PER_FRAME;
        }
        bytes -= bytes % USB_AUDIO_BYTES_PER_FRAME;
        if (bytes != 0U) {
            const uint16_t read_bytes = tud_audio_n_read(0U,
                                                         g_usb_audio_scratch,
                                                         (uint16_t)bytes);
            const uint32_t read_frames = read_bytes / USB_AUDIO_BYTES_PER_FRAME;
            if ((read_bytes % USB_AUDIO_BYTES_PER_FRAME) == 0U) {
                (void)usb_audio_pcm_write_pc_to_brick(g_usb_audio_scratch,
                                                      read_frames);
            }
        }
        usb_audio_feedback_update();
    }

    if (g_usb_audio_in_active != 0U) {
        const uint32_t available = usb_audio_pcm_brick_to_pc_available();

        if (g_usb_audio_in_ready == 0U) {
            if (available >= USB_AUDIO_PCM_RING_START_FRAMES) {
                g_usb_audio_in_ready = 1U;
            }
        }

        if ((g_usb_audio_in_ready != 0U) && (available != 0U)) {
            uint32_t frames = available;
            uint16_t fifo_remaining;
            uint16_t written_bytes;

            if (frames > USB_AUDIO_SERVICE_MAX_FRAMES) {
                frames = USB_AUDIO_SERVICE_MAX_FRAMES;
            }
            fifo_remaining = tu_fifo_remaining(tud_audio_n_get_ep_in_ff(0U));
            if (fifo_remaining < (frames * USB_AUDIO_BYTES_PER_FRAME)) {
                frames = fifo_remaining / USB_AUDIO_BYTES_PER_FRAME;
            }
            if (frames == 0U) {
                return;
            }
            frames = usb_audio_pcm_peek_brick_to_pc(g_usb_audio_scratch, frames);
            written_bytes = tud_audio_n_write(0U,
                                               g_usb_audio_scratch,
                                               (uint16_t)(frames * USB_AUDIO_BYTES_PER_FRAME));
            if ((written_bytes % USB_AUDIO_BYTES_PER_FRAME) == 0U) {
                usb_audio_pcm_discard_brick_to_pc(written_bytes /
                                                  USB_AUDIO_BYTES_PER_FRAME);
            }
        }
    }
}

bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t n_bytes_sent,
                           uint8_t func_id, uint8_t ep_in,
                           uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_sent;
    (void)func_id;
    (void)ep_in;
    (void)cur_alt_setting;
    return true;
}

bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received,
                           uint8_t func_id, uint8_t ep_out,
                           uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_received;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;
    return true;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf,
                                  audio_feedback_params_t *feedback_param)
{
    (void)func_id;
    (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_DISABLED;
    feedback_param->sample_freq = USB_AUDIO_SAMPLE_RATE_HZ;
}

bool tud_audio_set_itf_cb(uint8_t rhport,
                          tusb_control_request_t const *p_request)
{
    (void)rhport;
    usb_audio_transport_set_interface(TU_U16_LOW(p_request->wIndex),
                                       TU_U16_LOW(p_request->wValue));
    return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                   tusb_control_request_t const *p_request)
{
    (void)rhport;
    usb_audio_transport_close_interface(TU_U16_LOW(p_request->wIndex));
    return true;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request)
{
    static uint32_t sample_rate = USB_AUDIO_SAMPLE_RATE_HZ;
    static uint8_t clock_valid = 1U;
    static audio20_control_range_4_n_t(1) sample_rate_range = {
        .wNumSubRanges = 1U,
        .subrange = {{ USB_AUDIO_SAMPLE_RATE_HZ,
                       USB_AUDIO_SAMPLE_RATE_HZ,
                       1U }}
    };
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    const uint8_t control = TU_U16_HIGH(p_request->wValue);

    if (entity_id != USB_AUDIO_CLOCK_SOURCE_ID) {
        return false;
    }
    if (control == AUDIO20_CS_CTRL_SAM_FREQ) {
        if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
            return tud_control_xfer(rhport, p_request, &sample_rate,
                                    sizeof(sample_rate));
        }
        if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
            return tud_control_xfer(rhport, p_request, &sample_rate_range,
                                    sizeof(sample_rate_range));
        }
    } else if ((control == AUDIO20_CS_CTRL_CLK_VALID)
               && (p_request->bRequest == AUDIO20_CS_REQ_CUR)) {
        return tud_control_xfer(rhport, p_request, &clock_valid,
                                sizeof(clock_valid));
    }
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request,
                                 uint8_t *pBuff)
{
    const uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    const uint8_t control = TU_U16_HIGH(p_request->wValue);

    (void)rhport;
    if ((entity_id != USB_AUDIO_CLOCK_SOURCE_ID)
        || (control != AUDIO20_CS_CTRL_SAM_FREQ)
        || (p_request->bRequest != AUDIO20_CS_REQ_CUR)
        || (p_request->wLength != sizeof(uint32_t))) {
        return false;
    }
    return tu_unaligned_read32(pBuff) == USB_AUDIO_SAMPLE_RATE_HZ;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *p_request,
                             uint8_t *pBuff)
{
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *p_request,
                              uint8_t *pBuff)
{
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport,
                             tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport,
                              tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;
    return false;
}
