#include "usb_device.h"

#include <stddef.h>

#include "main.h"
#include "midi.h"
#include "tusb.h"

#define USB_DEVICE_RHPORT       0U
#define USB_DEVICE_MIDI_EP_OUT  0x01U
#define USB_DEVICE_MIDI_EP_IN   0x81U
#define USB_DEVICE_MIDI_EP_SIZE 64U

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
    0x00U, 0x00U, 0x00U,
    64U,
    (uint8_t)(USB_VID & 0xFFU), (uint8_t)(USB_VID >> 8),
    (uint8_t)(USB_PID & 0xFFU), (uint8_t)(USB_PID >> 8),
    0x00U, 0x02U,
    USB_STR_MANUFACTURER, USB_STR_PRODUCT, USB_STR_SERIAL,
    0x01U
};

static const uint8_t g_usb_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1U, 2U, 0U,
                          TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100U),
    TUD_MIDI_DESCRIPTOR(1U, USB_STR_MIDI,
                        USB_DEVICE_MIDI_EP_OUT, USB_DEVICE_MIDI_EP_IN,
                        USB_DEVICE_MIDI_EP_SIZE)
};

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

void MX_USB_DEVICE_Init(void)
{
    (void)usb_device_start();
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
        tud_task();
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
}

void tud_midi_rx_cb(uint8_t itf)
{
    uint8_t packet[4];

    while (tud_midi_n_packet_read(itf, packet)) {
        midi_usb_rx_submit_from_isr(packet, sizeof(packet));
    }
}
