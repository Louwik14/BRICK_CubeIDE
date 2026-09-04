#include "usb_host.h"

#include "main.h"
#include "midi_host.h"
#include "tusb.h"
#include "usb_clock_select.h"

#define USB_HOST_RHPORT 0U

static uint8_t g_usb_host_started;
static uint8_t g_usb_host_power_prepared;
static volatile uint8_t g_usb_host_midi_mounted;

static uint8_t usb_host_hw_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clocks = {0};

    clocks.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clocks.UsbClockSelection = usb_stack_get_rcc_usb_clock_source();
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

void usb_host_power_on(void)
{
    /* AP2161 enable is active low. */
    HAL_GPIO_WritePin(HOST_EN_GPIO_Port, HOST_EN_Pin, GPIO_PIN_RESET);
}

void usb_host_power_off(void)
{
    HAL_GPIO_WritePin(HOST_EN_GPIO_Port, HOST_EN_Pin, GPIO_PIN_SET);
    g_usb_host_power_prepared = 0U;
}

uint8_t usb_host_prepare(void)
{
    if (g_usb_host_started != 0U) {
        return 1U;
    }

    if (g_usb_host_power_prepared != 0U) {
        return 1U;
    }

    g_usb_host_midi_mounted = 0U;
    midi_host_transport_reset();
    if (usb_host_hw_init() == 0U) {
        return 0U;
    }

    usb_host_power_on();
    g_usb_host_power_prepared = 1U;
    return 1U;
}

uint8_t usb_host_start(void)
{
    const tusb_rhport_init_t init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL
    };

    if (g_usb_host_started != 0U) {
        return 1U;
    }

    if (g_usb_host_power_prepared == 0U) {
        return 0U;
    }

    if (!tuh_rhport_init(USB_HOST_RHPORT, &init)) {
        usb_host_power_off();
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
        return 0U;
    }

    g_usb_host_started = 1U;
    return 1U;
}

uint8_t usb_host_stop(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);

    if (tuh_rhport_is_active(USB_HOST_RHPORT)) {
        (void)tuh_deinit(USB_HOST_RHPORT);
    }

    g_usb_host_started = 0U;
    g_usb_host_midi_mounted = 0U;
    midi_host_transport_reset();
    usb_host_power_off();
    return 1U;
}

uint8_t usb_host_is_started(void)
{
    return g_usb_host_started;
}

uint8_t usb_host_is_ready(void)
{
    return (g_usb_host_started != 0U)
        && (g_usb_host_midi_mounted != 0U)
        && tuh_midi_mounted(0U);
}

void usb_host_process(void)
{
    if (g_usb_host_started != 0U) {
        tuh_task_ext(0U, false);
    }
}

void usb_host_irq(void)
{
    if (g_usb_host_started != 0U) {
        tuh_int_handler(USB_HOST_RHPORT);
    }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
    (void)mount_cb_data;
    if (idx == 0U) {
        g_usb_host_midi_mounted = 1U;
    }
}

void tuh_midi_umount_cb(uint8_t idx)
{
    if (idx == 0U) {
        g_usb_host_midi_mounted = 0U;
        midi_host_rx_discard_pending();
    }
}
