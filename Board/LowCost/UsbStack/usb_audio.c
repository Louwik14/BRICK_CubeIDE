#include "usb_audio.h"

#include <float.h>
#include <limits.h>
#include <string.h>

#include "IPC/usb_audio_float_ring.h"
#include "Board/board_audio_format.h"
#include "Audio/audio_probe.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "tusb.h"

#define USB_AUDIO_AS_OUT_INTERFACE       1U
#define USB_AUDIO_CLOCK_SOURCE_ID        0x10U
#define USB_AUDIO_SAMPLE_RATE_HZ         48000U
#define USB_AUDIO_CHANNELS               2U
#define USB_AUDIO_BYTES_PER_SAMPLE       4U
#define USB_AUDIO_BYTES_PER_FRAME        (USB_AUDIO_CHANNELS * USB_AUDIO_BYTES_PER_SAMPLE)
#define USB_AUDIO_IRQ_PACKET_MAX_BYTES   CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
#define USB_AUDIO_CORRECTION_INTERVAL_FRAMES 5000U
#define USB_AUDIO_LOW_WATER_FRAMES       (USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES / 4U)
#define USB_AUDIO_HIGH_WATER_FRAMES      (USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES * 3U / 4U)

static volatile uint8_t g_usb_audio_out_active;
static uint8_t g_usb_audio_out_ready;
static uint32_t g_usb_audio_frames_since_correction;
static ALIGN32 float g_usb_audio_adapt_left[BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF + 1U];
static ALIGN32 float g_usb_audio_adapt_right[BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF + 1U];
static ALIGN32 int32_t g_usb_audio_out_pcm_scratch[
    USB_AUDIO_IRQ_PACKET_MAX_BYTES / sizeof(int32_t)];
static ALIGN32 float g_usb_audio_out_float_scratch[
    USB_AUDIO_IRQ_PACKET_MAX_BYTES / sizeof(float)];

_Static_assert(sizeof(float) == USB_AUDIO_BYTES_PER_SAMPLE,
               "USB Audio requires 32-bit float");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "USB Audio requires IEEE-754 binary32");
_Static_assert(sizeof(int32_t) == USB_AUDIO_BYTES_PER_SAMPLE,
               "USB Audio requires 32-bit PCM");
_Static_assert(sizeof(g_usb_audio_out_pcm_scratch) == USB_AUDIO_IRQ_PACKET_MAX_BYTES,
               "USB Audio OUT PCM scratch size changed");
_Static_assert(sizeof(g_usb_audio_out_float_scratch) == USB_AUDIO_IRQ_PACKET_MAX_BYTES,
               "USB Audio OUT float scratch size changed");
_Static_assert(CFG_TUSB_OS == OPT_OS_NONE,
               "USB Audio IRQ drain requires TinyUSB bare-metal mode");
_Static_assert((USB_AUDIO_IRQ_PACKET_MAX_BYTES % USB_AUDIO_BYTES_PER_FRAME) == 0U,
               "USB Audio OUT packet must contain complete PCM frames");
#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "USB Audio PCM24-in-32 requires a little-endian target"
#endif

/* UAC2 PCM24 occupies the most significant 24 bits of each 32-bit subslot. */
static float usb_audio_pcm24_in_32_to_float(int32_t sample)
{
    int32_t pcm24 = (int32_t)((uint32_t)sample >> 8);
    if ((pcm24 & INT32_C(0x800000)) != 0) {
        pcm24 -= INT32_C(0x1000000);
    }
    return (float)pcm24 * (1.0f / 8388608.0f);
}

void usb_audio_audio_boundary(void)
{
    if ((g_usb_audio_out_active != 0U) && (g_usb_audio_out_ready == 0U)
        && (usb_audio_float_pc_to_brick_available()
            >= USB_AUDIO_FLOAT_RING_START_FRAMES)) {
        g_usb_audio_out_ready = 1U;
    }
}

static void usb_audio_reset_cursors(void)
{
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_usb_audio_out_ready = 0U;
    g_usb_audio_frames_since_correction = 0U;
    usb_audio_float_reset();
    audio_probe_usb_reset();
    __DMB();
    __set_PRIMASK(primask);
}

void usb_audio_transport_reset(void)
{
    g_usb_audio_out_active = 0U;
    __DMB();
    usb_audio_reset_cursors();
}

void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting)
{
    if (interface_number != USB_AUDIO_AS_OUT_INTERFACE) {
        return;
    }

    if (g_usb_audio_out_active == 0U) {
        usb_audio_reset_cursors();
    }

    g_usb_audio_out_active = (alternate_setting != 0U) ? 1U : 0U;
    g_usb_audio_out_ready = 0U;
    g_usb_audio_frames_since_correction = 0U;
    __DMB();
}

void usb_audio_transport_close_interface(uint8_t interface_number)
{
    if (interface_number != USB_AUDIO_AS_OUT_INTERFACE) {
        return;
    }

    g_usb_audio_out_active = 0U;

    __DMB();
    usb_audio_reset_cursors();
}

uint32_t usb_audio_audio_read(float *left, float *right, uint32_t frames)
{
    uint32_t read_frames;
    uint32_t source_frames = frames;
    int8_t correction = 0;

    if ((left == NULL) || (right == NULL) || (frames == 0U)
        || (g_usb_audio_out_active == 0U)) {
        return 0U;
    }

    if (g_usb_audio_out_ready == 0U) return 0U;

    if ((frames >= 2U) && (frames <= BOARD_AUDIO_CONTRACT_FRAMES_PER_HALF)
        && (g_usb_audio_frames_since_correction >= USB_AUDIO_CORRECTION_INTERVAL_FRAMES)) {
        const uint32_t fill = usb_audio_float_pc_to_brick_available();
        if (fill <= USB_AUDIO_LOW_WATER_FRAMES) {
            source_frames = frames - 1U;
            correction = -1;
        } else if (fill >= USB_AUDIO_HIGH_WATER_FRAMES) {
            source_frames = frames + 1U;
            correction = 1;
        }
    }

    if (correction == 0) {
        read_frames = usb_audio_float_read_pc_to_brick(left, right, frames);
    } else {
        read_frames = usb_audio_float_read_pc_to_brick(
            g_usb_audio_adapt_left, g_usb_audio_adapt_right, source_frames);
    }
    if (read_frames < source_frames) {
        g_usb_audio_out_ready = 0U;
        g_usb_audio_frames_since_correction = 0U;
        memset(left, 0, frames * sizeof(float));
        memset(right, 0, frames * sizeof(float));
        return frames;
    }

    if (correction != 0) {
        const uint32_t copy_frames = (source_frames < frames) ? source_frames : frames;
        memcpy(left, g_usb_audio_adapt_left, copy_frames * sizeof(float));
        memcpy(right, g_usb_audio_adapt_right, copy_frames * sizeof(float));
        if (correction < 0) {
            left[frames - 1U] = left[frames - 2U];
            right[frames - 1U] = right[frames - 2U];
        }
        g_usb_audio_frames_since_correction = 0U;
    } else if (g_usb_audio_frames_since_correction < USB_AUDIO_CORRECTION_INTERVAL_FRAMES) {
        g_usb_audio_frames_since_correction += frames;
    }
    return frames;
}

bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received,
                           uint8_t func_id, uint8_t ep_out,
                           uint8_t cur_alt_setting)
{
    uint16_t bytes_to_read;
    uint16_t read_bytes;
    uint32_t read_frames;

    (void)rhport;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;
    if (g_usb_audio_out_active != 0U) {
        /* TinyUSB has already copied this isochronous OUT packet into its
         * software FIFO and rearmed the endpoint.  Move only this packet to
         * the SPSC AUDIO ring here. */
        bytes_to_read = n_bytes_received;
        if (bytes_to_read > (uint16_t)sizeof(g_usb_audio_out_pcm_scratch)) {
            bytes_to_read = (uint16_t)sizeof(g_usb_audio_out_pcm_scratch);
        }
        read_bytes = tud_audio_n_read(0U, g_usb_audio_out_pcm_scratch,
                                      bytes_to_read);
        if ((read_bytes != bytes_to_read)
            || ((read_bytes % USB_AUDIO_BYTES_PER_FRAME) != 0U)) {
            return true;
        }
        read_frames = read_bytes / USB_AUDIO_BYTES_PER_FRAME;
        if (read_frames != 0U) {
            for (uint32_t sample = 0U;
                 sample < read_frames * USB_AUDIO_CHANNELS; ++sample) {
                g_usb_audio_out_float_scratch[sample] =
                    usb_audio_pcm24_in_32_to_float(
                        g_usb_audio_out_pcm_scratch[sample]);
            }
            audio_probe_usb_receive(g_usb_audio_out_float_scratch, read_frames);
            (void)usb_audio_float_write_pc_to_brick(
                g_usb_audio_out_float_scratch, read_frames);
        }
    }
    return true;
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
                       0U }}
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
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return false;
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
