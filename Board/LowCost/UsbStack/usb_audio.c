#include "usb_audio.h"

#include <float.h>
#include <limits.h>
#include <string.h>

#include "Board/usb_audio_float_ring.h"
#include "Board/board_audio_format.h"
#include "Platform/brick_fatal.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "tusb.h"

#define USB_AUDIO_AS_OUT_INTERFACE       1U
#define USB_AUDIO_AS_IN_INTERFACE        2U
#define USB_AUDIO_CLOCK_SOURCE_ID        0x10U
#define USB_AUDIO_SAMPLE_RATE_HZ         48000U
#define USB_AUDIO_CHANNELS               2U
#define USB_AUDIO_BYTES_PER_SAMPLE       4U
#define USB_AUDIO_BYTES_PER_FRAME        (USB_AUDIO_CHANNELS * USB_AUDIO_BYTES_PER_SAMPLE)
#define USB_AUDIO_IRQ_PACKET_MAX_BYTES   CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
#define USB_AUDIO_CORRECTION_INTERVAL_FRAMES 5000U
#define USB_AUDIO_LOW_WATER_FRAMES       (USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES / 4U)
#define USB_AUDIO_HIGH_WATER_FRAMES      (USB_AUDIO_FLOAT_RING_CAPACITY_FRAMES * 3U / 4U)
#define USB_AUDIO_IN_PACKET_FRAMES       48U
#define USB_AUDIO_IN_PACKET_BYTES        (USB_AUDIO_IN_PACKET_FRAMES * USB_AUDIO_BYTES_PER_FRAME)

static volatile uint8_t g_usb_audio_out_active;
static volatile uint8_t g_usb_audio_in_active;
static uint8_t g_usb_audio_in_ready;
static uint32_t g_usb_audio_in_frames_since_correction;
static ALIGN32 float g_usb_audio_in_float_scratch[(USB_AUDIO_IN_PACKET_FRAMES + 1U) * USB_AUDIO_CHANNELS];
static ALIGN32 int32_t g_usb_audio_in_pcm_scratch[USB_AUDIO_IN_PACKET_FRAMES * USB_AUDIO_CHANNELS];
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
_Static_assert(USB_AUDIO_IN_PACKET_BYTES == 384U,
               "USB Audio IN nominal packet must contain 48 stereo PCM32 frames");
_Static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX >= USB_AUDIO_IN_PACKET_BYTES,
               "USB Audio IN endpoint must hold a nominal packet");
_Static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ >= 2U * USB_AUDIO_IN_PACKET_BYTES,
               "USB Audio IN FIFO must hold two nominal packets");
#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "USB Audio PCM32 requires a little-endian target"
#endif

static float usb_audio_pcm32_to_float(int32_t sample)
{
    return (float)sample * (1.0f / 2147483648.0f);
}

static int32_t usb_audio_float_to_pcm32(float sample)
{
    uint32_t bits;
    memcpy(&bits, &sample, sizeof(bits));
    if ((bits & UINT32_C(0x7F800000)) == UINT32_C(0x7F800000)) {
        if ((bits & UINT32_C(0x007FFFFF)) != 0U) return 0;
        return (bits & UINT32_C(0x80000000)) ? INT32_MIN : INT32_MAX;
    }
    if (sample <= -1.0f) return INT32_MIN;
    if (sample >= 1.0f) return INT32_MAX;
    return (int32_t)(sample * 2147483648.0f);
}

static void usb_audio_reset_in(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_usb_audio_in_ready = 0U;
    g_usb_audio_in_frames_since_correction = 0U;
    usb_audio_float_reset_brick_to_pc();
    __DMB();
    __set_PRIMASK(primask);
}

/* Called after TinyUSB has loaded the preceding packet into its IN endpoint. */
static void usb_audio_fill_in_fifo(void)
{
    uint32_t source_frames = USB_AUDIO_IN_PACKET_FRAMES;
    uint32_t fill;
    int8_t correction = 0;
    uint32_t read_frames = 0U;

    if (g_usb_audio_in_active == 0U) return;
    fill = usb_audio_float_brick_to_pc_available();
    if ((g_usb_audio_in_ready == 0U)
        && (fill >= USB_AUDIO_FLOAT_RING_START_FRAMES)) {
        g_usb_audio_in_ready = 1U;
    }
    if (g_usb_audio_in_ready != 0U) {
        if (g_usb_audio_in_frames_since_correction >= USB_AUDIO_CORRECTION_INTERVAL_FRAMES) {
            if (fill <= USB_AUDIO_LOW_WATER_FRAMES) {
                source_frames--;
                correction = -1;
            } else if (fill >= USB_AUDIO_HIGH_WATER_FRAMES) {
                source_frames++;
                correction = 1;
            }
        }
        if (fill >= source_frames) {
            read_frames = usb_audio_float_peek_brick_to_pc(
                g_usb_audio_in_float_scratch, source_frames);
        }
    }
    if (read_frames == source_frames) {
        for (uint32_t frame = 0U; frame < USB_AUDIO_IN_PACKET_FRAMES; ++frame) {
            const uint32_t source = (correction < 0 && frame == USB_AUDIO_IN_PACKET_FRAMES - 1U)
                ? frame - 1U : frame;
            for (uint32_t channel = 0U; channel < USB_AUDIO_CHANNELS; ++channel) {
                g_usb_audio_in_pcm_scratch[frame * USB_AUDIO_CHANNELS + channel] =
                    usb_audio_float_to_pcm32(
                        g_usb_audio_in_float_scratch[source * USB_AUDIO_CHANNELS + channel]);
            }
        }
        usb_audio_float_discard_brick_to_pc(source_frames);
        if (correction != 0) {
            g_usb_audio_in_frames_since_correction = 0U;
        } else if (g_usb_audio_in_frames_since_correction < USB_AUDIO_CORRECTION_INTERVAL_FRAMES) {
            g_usb_audio_in_frames_since_correction += USB_AUDIO_IN_PACKET_FRAMES;
        }
    } else {
        g_usb_audio_in_ready = 0U;
        g_usb_audio_in_frames_since_correction = 0U;
        memset(g_usb_audio_in_pcm_scratch, 0, sizeof(g_usb_audio_in_pcm_scratch));
    }
    (void)tud_audio_n_write(0U, g_usb_audio_in_pcm_scratch, USB_AUDIO_IN_PACKET_BYTES);
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
    usb_audio_float_reset_pc_to_brick();
    __DMB();
    __set_PRIMASK(primask);
}

void usb_audio_transport_reset(void)
{
    g_usb_audio_out_active = 0U;
    g_usb_audio_in_active = 0U;
    __DMB();
    usb_audio_reset_cursors();
    usb_audio_reset_in();
}

void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting)
{
    if (interface_number == USB_AUDIO_AS_IN_INTERFACE) {
        g_usb_audio_in_active = 0U;
        usb_audio_reset_in();
        g_usb_audio_in_active = (alternate_setting != 0U) ? 1U : 0U;
        if (g_usb_audio_in_active != 0U) usb_audio_fill_in_fifo();
        return;
    }
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
    if (interface_number == USB_AUDIO_AS_IN_INTERFACE) {
        g_usb_audio_in_active = 0U;
        usb_audio_reset_in();
        return;
    }
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
        BRICK_FATAL_CONTEXT("USB_AUDIO_RING_UNDERFLOW",
                            BRICK_FATAL_USB_AUDIO_RING_UNDERFLOW,
                            UINT32_MAX, usb_audio_float_pc_to_brick_available(),
                            source_frames, read_frames);
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

uint32_t usb_audio_audio_write(const float *left, const float *right,
                               uint32_t frames)
{
    if ((left == NULL) || (right == NULL) || (frames == 0U)
        || (g_usb_audio_in_active == 0U)) return 0U;
    return usb_audio_float_write_brick_to_pc(left, right, frames);
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
    usb_audio_fill_in_fifo();
    return true;
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
            BRICK_FATAL_CONTEXT("USB_AUDIO_PACKET_TOO_LARGE",
                                BRICK_FATAL_USB_AUDIO_PACKET, UINT32_MAX,
                                1U, bytes_to_read,
                                sizeof(g_usb_audio_out_pcm_scratch));
        }
        if ((bytes_to_read % USB_AUDIO_BYTES_PER_FRAME) != 0U) {
            BRICK_FATAL_CONTEXT("USB_AUDIO_PACKET_PARTIAL_FRAME",
                                BRICK_FATAL_USB_AUDIO_PACKET, UINT32_MAX,
                                2U, bytes_to_read, USB_AUDIO_BYTES_PER_FRAME);
        }
        read_bytes = tud_audio_n_read(0U, g_usb_audio_out_pcm_scratch,
                                      bytes_to_read);
        if (read_bytes != bytes_to_read) {
            BRICK_FATAL_CONTEXT("USB_AUDIO_FIFO_SHORT_READ",
                                BRICK_FATAL_USB_AUDIO_FIFO_READ, UINT32_MAX,
                                0U, bytes_to_read, read_bytes);
        }
        read_frames = read_bytes / USB_AUDIO_BYTES_PER_FRAME;
        if (read_frames != 0U) {
            for (uint32_t sample = 0U;
                 sample < read_frames * USB_AUDIO_CHANNELS; ++sample) {
                g_usb_audio_out_float_scratch[sample] =
                    usb_audio_pcm32_to_float(
                        g_usb_audio_out_pcm_scratch[sample]);
            }
            const uint32_t written = usb_audio_float_write_pc_to_brick(
                g_usb_audio_out_float_scratch, read_frames);
            if (written != read_frames) {
                BRICK_FATAL_CONTEXT("USB_AUDIO_RING_OVERFLOW",
                                    BRICK_FATAL_USB_AUDIO_RING_OVERFLOW,
                                    UINT32_MAX, 0U, read_frames, written);
            }
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
