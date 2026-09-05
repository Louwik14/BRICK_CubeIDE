#include "usb_device.h"

#include <stddef.h>

#include "main.h"
#include "midi.h"
#include "tusb.h"
#include "usb_audio.h"

#define USB_DEVICE_RHPORT       0U
#define USB_DEVICE_MIDI_EP_OUT  0x01U
#define USB_DEVICE_MIDI_EP_IN   0x81U
#define USB_DEVICE_MIDI_EP_SIZE 64U
#define USB_DEVICE_AUDIO_EP_OUT 0x02U
#define USB_DEVICE_AUDIO_EP_IN  0x82U
#define USB_DEVICE_AUDIO_EP_FB  0x83U
#define USB_DEVICE_AUDIO_EP_SIZE \
    TUD_AUDIO_EP_SIZE(false, 48000U, 4U, 2U)

enum {
    USB_DEVICE_AUDIO_AC_ITF = 0U,
    USB_DEVICE_AUDIO_OUT_ITF,
    USB_DEVICE_AUDIO_IN_ITF,
    USB_DEVICE_AUDIO_ITF_COUNT,
    USB_DEVICE_MIDI_AC_ITF = USB_DEVICE_AUDIO_ITF_COUNT,
    USB_DEVICE_MIDI_MS_ITF,
    USB_DEVICE_ITF_COUNT
};

#define USB_DEVICE_DWC2_FS_FIFO_WORDS      1024U
#define USB_DEVICE_DWC2_FS_RX_FIFO_WORDS \
    (13U + 1U + 2U * ((USB_DEVICE_AUDIO_EP_SIZE / 4U) + 1U) + 2U * 2U)
#define USB_DEVICE_DWC2_FS_TX_FIFO_WORDS \
    (64U / 4U + USB_DEVICE_AUDIO_EP_SIZE / 4U + 4U / 4U + 64U / 4U)

#define USB_DEVICE_AUDIO_DESC_LEN (TUD_AUDIO20_DESC_IAD_LEN \
    + TUD_AUDIO20_DESC_STD_AC_LEN \
    + TUD_AUDIO20_DESC_CS_AC_LEN \
    + TUD_AUDIO20_DESC_CLK_SRC_LEN \
    + 2U * TUD_AUDIO20_DESC_INPUT_TERM_LEN \
    + 2U * TUD_AUDIO20_DESC_OUTPUT_TERM_LEN \
    + 4U * TUD_AUDIO20_DESC_STD_AS_LEN \
    + 2U * TUD_AUDIO20_DESC_CS_AS_INT_LEN \
    + 2U * TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN \
    + 2U * TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN \
    + 2U * TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN \
    + TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN)

_Static_assert(USB_DEVICE_AUDIO_EP_SIZE == 392U,
               "USB Audio FS endpoint maximum packet size changed");
_Static_assert(USB_DEVICE_DWC2_FS_RX_FIFO_WORDS
                   + USB_DEVICE_DWC2_FS_TX_FIFO_WORDS
                   < USB_DEVICE_DWC2_FS_FIFO_WORDS,
               "DWC2 FS FIFO allocation exceeds the 1024-word budget");

#define USB_VID                 1155U
#define USB_PID                 22315U

#define USB_STR_LANGID          0U
#define USB_STR_MANUFACTURER    1U
#define USB_STR_PRODUCT         2U
#define USB_STR_SERIAL          3U
#define USB_STR_MIDI            4U

static uint8_t g_usb_device_started;
static volatile uint8_t g_usb_device_mounted;

static const uint8_t g_usb_device_descriptor[] = {
    0x12U, TUSB_DESC_DEVICE,
    0x00U, 0x02U,
    0xEFU, 0x02U, 0x01U,
    64U,
    (uint8_t)(USB_VID & 0xFFU), (uint8_t)(USB_VID >> 8),
    (uint8_t)(USB_PID & 0xFFU), (uint8_t)(USB_PID >> 8),
    0x00U, 0x02U,
    USB_STR_MANUFACTURER, USB_STR_PRODUCT, USB_STR_SERIAL,
    0x01U
};

static const uint8_t g_usb_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1U, USB_DEVICE_ITF_COUNT, 0U,
                          TUD_CONFIG_DESC_LEN + USB_DEVICE_AUDIO_DESC_LEN
                          + TUD_MIDI_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100U),
    /* UAC2 Audio Control + OUT/IN streaming interfaces. */
    TUD_AUDIO20_DESC_IAD(USB_DEVICE_AUDIO_AC_ITF,
                         USB_DEVICE_AUDIO_ITF_COUNT, 0U),
    TUD_AUDIO20_DESC_STD_AC(USB_DEVICE_AUDIO_AC_ITF, 0U, 0U),
    TUD_AUDIO20_DESC_CS_AC(
        0x0200U,
        AUDIO20_FUNC_PRO_AUDIO,
        TUD_AUDIO20_DESC_CLK_SRC_LEN
            + 2U * TUD_AUDIO20_DESC_INPUT_TERM_LEN
            + 2U * TUD_AUDIO20_DESC_OUTPUT_TERM_LEN,
        AUDIO20_CS_AS_INTERFACE_CTRL_LATENCY_POS),
    TUD_AUDIO20_DESC_CLK_SRC(
        0x10U,
        AUDIO20_CLOCK_SOURCE_ATT_INT_VAR_CLK,
        (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS)
            | (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_VAL_POS),
        0U,
        0U),
    TUD_AUDIO20_DESC_INPUT_TERM(
        0x01U,
        AUDIO_TERM_TYPE_IN_GENERIC_MIC,
        0x03U,
        0x10U,
        2U,
        AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT,
        0U,
        AUDIO20_CTRL_NONE,
        0U),
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        0x03U,
        AUDIO_TERM_TYPE_USB_STREAMING,
        0x01U,
        0x01U,
        0x10U,
        AUDIO20_CTRL_NONE,
        0U),
    TUD_AUDIO20_DESC_INPUT_TERM(
        0x04U,
        AUDIO_TERM_TYPE_USB_STREAMING,
        0x06U,
        0x10U,
        2U,
        AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT,
        0U,
        AUDIO20_CTRL_NONE,
        0U),
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        0x06U,
        AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER,
        0x04U,
        0x04U,
        0x10U,
        AUDIO20_CTRL_NONE,
        0U),

    /* Host to BRICK: asynchronous OUT data plus explicit feedback. */
    TUD_AUDIO20_DESC_STD_AS_INT(USB_DEVICE_AUDIO_OUT_ITF, 0U, 0U, 0U),
    TUD_AUDIO20_DESC_STD_AS_INT(USB_DEVICE_AUDIO_OUT_ITF, 1U, 2U, 0U),
    TUD_AUDIO20_DESC_CS_AS_INT(
        0x04U, AUDIO20_CTRL_NONE, AUDIO20_FORMAT_TYPE_I,
        AUDIO20_DATA_FORMAT_TYPE_I_PCM, 2U,
        AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT,
        0U),
    TUD_AUDIO20_DESC_TYPE_I_FORMAT(4U, 24U),
    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        USB_DEVICE_AUDIO_EP_OUT,
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS
                  | TUSB_ISO_EP_ATT_DATA),
        USB_DEVICE_AUDIO_EP_SIZE,
        1U),
    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        AUDIO20_CTRL_NONE,
        AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
        0U),
    TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP(USB_DEVICE_AUDIO_EP_FB, 4U, 1U),

    /* BRICK to host: asynchronous IN data. */
    TUD_AUDIO20_DESC_STD_AS_INT(USB_DEVICE_AUDIO_IN_ITF, 0U, 0U, 0U),
    TUD_AUDIO20_DESC_STD_AS_INT(USB_DEVICE_AUDIO_IN_ITF, 1U, 1U, 0U),
    TUD_AUDIO20_DESC_CS_AS_INT(
        0x03U, AUDIO20_CTRL_NONE, AUDIO20_FORMAT_TYPE_I,
        AUDIO20_DATA_FORMAT_TYPE_I_PCM, 2U,
        AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT,
        0U),
    TUD_AUDIO20_DESC_TYPE_I_FORMAT(4U, 24U),
    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        USB_DEVICE_AUDIO_EP_IN,
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS
                  | TUSB_ISO_EP_ATT_DATA),
        USB_DEVICE_AUDIO_EP_SIZE,
        1U),
    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        AUDIO20_CTRL_NONE,
        AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
        0U),

    TUD_MIDI_DESCRIPTOR(USB_DEVICE_MIDI_AC_ITF, USB_STR_MIDI,
                        USB_DEVICE_MIDI_EP_OUT, USB_DEVICE_MIDI_EP_IN,
                        USB_DEVICE_MIDI_EP_SIZE)
};

_Static_assert(sizeof(g_usb_configuration_descriptor) == 310U,
               "USB MIDI + UAC2 configuration descriptor size changed");

static uint16_t g_usb_string_descriptor[32U];

static void usb_device_string_from_ascii(const char *text)
{
    size_t i = 0U;

    if (text == NULL) {
        return;
    }

    while ((text[i] != '\0')
           && (i + 1U < (sizeof(g_usb_string_descriptor) / sizeof(g_usb_string_descriptor[0])))) {
        g_usb_string_descriptor[1U + i] = (uint8_t)text[i];
        ++i;
    }
    g_usb_string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U + 2U * i));
}

static void usb_device_string_serial(void)
{
    const uint32_t uid[3] = {
        *(const uint32_t *)(UID_BASE + 0x00U),
        *(const uint32_t *)(UID_BASE + 0x04U),
        *(const uint32_t *)(UID_BASE + 0x08U)
    };
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 1U;

    for (size_t word = 0U; word < 3U; ++word) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            g_usb_string_descriptor[pos++] = (uint16_t)hex[(uid[word] >> shift) & 0x0FU];
        }
    }
    g_usb_string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U + 2U * 24U));
}

static uint8_t usb_device_hw_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clocks = {0};

    clocks.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clocks.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&clocks) != HAL_OK) {
        return 0U;
    }

    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF10_OTG1_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    return 1U;
}

uint8_t usb_device_start(void)
{
    const tusb_rhport_init_t init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    if (g_usb_device_started != 0U) {
        return 1U;
    }

    usb_audio_transport_reset();
    midi_usb_transport_quiesce_end();
    g_usb_device_mounted = 0U;
    if (usb_device_hw_init() == 0U) {
        return 0U;
    }

    if (!tud_rhport_init(USB_DEVICE_RHPORT, &init)) {
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
        return 0U;
    }

    g_usb_device_started = 1U;
    return 1U;
}

uint8_t usb_device_stop(void)
{
    if (g_usb_device_started == 0U) {
        usb_audio_transport_reset();
        midi_usb_transport_reset();
        return 1U;
    }

    midi_usb_transport_quiesce_begin();
    if (midi_usb_transport_quiesce_ready() == 0U) {
        return 0U;
    }

    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    g_usb_device_mounted = 0U;
    if (tud_inited()) {
        (void)tud_deinit(USB_DEVICE_RHPORT);
    }
    usb_audio_transport_reset();
    g_usb_device_started = 0U;
    midi_usb_transport_reset();
    midi_usb_transport_quiesce_end();
    return 1U;
}

uint8_t usb_device_is_started(void)
{
    return g_usb_device_started;
}

uint8_t usb_device_is_ready(void)
{
    return (g_usb_device_started != 0U) && (g_usb_device_mounted != 0U);
}

void usb_device_process(void)
{
    if ((g_usb_device_started != 0U)
        && (midi_usb_transport_quiesce_requested() == 0U)) {
        tud_task_ext(0U, false);
        usb_audio_transport_process();
    }
}

void usb_device_irq(void)
{
    if (g_usb_device_started != 0U) {
        tud_int_handler(USB_DEVICE_RHPORT);
    }
}

uint16_t usb_device_send_packets(const uint8_t *packets, uint16_t bytes_len)
{
    const uint32_t packet_count = bytes_len / 4U;

    if ((packets == NULL) || (bytes_len == 0U) || ((bytes_len % 4U) != 0U)
        || (usb_device_is_ready() == 0U)) {
        return 0U;
    }

    return (uint16_t)tud_midi_n_packet_write_n(0U, packets, packet_count);
}

const uint8_t *tud_descriptor_device_cb(void)
{
    return g_usb_device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return g_usb_configuration_descriptor;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    if (index == USB_STR_LANGID) {
        g_usb_string_descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4U);
        g_usb_string_descriptor[1] = 0x0409U;
        return g_usb_string_descriptor;
    }
    if (index == USB_STR_SERIAL) {
        usb_device_string_serial();
        return g_usb_string_descriptor;
    }

    switch (index) {
    case USB_STR_MANUFACTURER:
        usb_device_string_from_ascii("STMicroelectronics");
        break;
    case USB_STR_PRODUCT:
        usb_device_string_from_ascii("STM32 USB MIDI");
        break;
    case USB_STR_MIDI:
        usb_device_string_from_ascii("MIDI Interface");
        break;
    default:
        return NULL;
    }
    return g_usb_string_descriptor;
}

void tud_mount_cb(void)
{
    g_usb_device_mounted = 1U;
}

void tud_umount_cb(void)
{
    g_usb_device_mounted = 0U;
    usb_audio_transport_reset();
}

void tud_midi_rx_cb(uint8_t itf)
{
    uint8_t packet[4];

    while (tud_midi_n_packet_read(itf, packet)) {
        midi_usb_rx_submit_from_isr(packet, sizeof(packet));
    }
}
