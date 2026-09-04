#include "fusb302.h"

#include "App/usb_service_wakeup.h"
#include "main.h"

#include <string.h>

#define FUSB302_I2C_ADDR_7BIT                 0x22U
#define FUSB302_I2C_ADDR_HAL                  (FUSB302_I2C_ADDR_7BIT << 1U)
#define FUSB302_I2C_TIMEOUT_MS                10U
#define FUSB302_READY_TRIALS                  2U

#define FUSB302_REG_DEVICE_ID                 0x01U
#define FUSB302_REG_SWITCHES0                 0x02U
#define FUSB302_REG_SWITCHES0_PU_EN2          0x80U
#define FUSB302_REG_SWITCHES0_PU_EN1          0x40U
#define FUSB302_REG_SWITCHES0_VCONN_CC2       0x20U
#define FUSB302_REG_SWITCHES0_VCONN_CC1       0x10U
#define FUSB302_REG_SWITCHES0_MEAS_CC2        0x08U
#define FUSB302_REG_SWITCHES0_MEAS_CC1        0x04U
#define FUSB302_REG_SWITCHES0_PDWN2           0x02U
#define FUSB302_REG_SWITCHES0_PDWN1           0x01U

#define FUSB302_REG_SWITCHES1                 0x03U
#define FUSB302_REG_SWITCHES1_POWERROLE       0x80U
#define FUSB302_REG_SWITCHES1_SPECREV1        0x40U
#define FUSB302_REG_SWITCHES1_SPECREV0        0x20U
#define FUSB302_REG_SWITCHES1_DATAROLE        0x10U
#define FUSB302_REG_SWITCHES1_AUTO_CRC        0x04U
#define FUSB302_REG_SWITCHES1_TXCC2           0x02U
#define FUSB302_REG_SWITCHES1_TXCC1           0x01U

#define FUSB302_REG_CONTROL0                  0x06U
#define FUSB302_REG_CONTROL0_TX_FLUSH         0x40U
#define FUSB302_REG_CONTROL0_INT_MASK         0x20U
#define FUSB302_REG_CONTROL0_HOST_CUR_USB     0x04U

#define FUSB302_REG_CONTROL1                  0x07U
#define FUSB302_REG_CONTROL1_RX_FLUSH         0x04U

#define FUSB302_REG_CONTROL2                  0x08U
#define FUSB302_REG_CONTROL2_MODE_MASK        0x06U
#define FUSB302_REG_CONTROL2_MODE_DRP         0x02U
#define FUSB302_REG_CONTROL2_TOGGLE           0x01U

#define FUSB302_REG_MASK                      0x0AU
#define FUSB302_REG_MASK_VBUSOK               0x80U
#define FUSB302_REG_MASK_ACTIVITY             0x40U
#define FUSB302_REG_MASK_COMP_CHNG            0x20U
#define FUSB302_REG_MASK_CRC_CHK              0x10U
#define FUSB302_REG_MASK_ALERT                0x08U
#define FUSB302_REG_MASK_WAKE                 0x04U
#define FUSB302_REG_MASK_COLLISION            0x02U
#define FUSB302_REG_MASK_BC_LVL               0x01U

#define FUSB302_REG_POWER                     0x0BU
#define FUSB302_REG_POWER_ALL                 0x0FU

#define FUSB302_REG_RESET                     0x0CU
#define FUSB302_REG_RESET_PD_RESET            0x02U
#define FUSB302_REG_RESET_SW_RESET            0x01U

#define FUSB302_REG_MASKA                     0x0EU
#define FUSB302_REG_MASKA_OCP_TEMP            0x80U
#define FUSB302_REG_MASKA_TOGDONE             0x40U
#define FUSB302_REG_MASKA_SOFTFAIL            0x20U
#define FUSB302_REG_MASKA_RETRYFAIL           0x10U
#define FUSB302_REG_MASKA_HARDSENT            0x08U
#define FUSB302_REG_MASKA_TXSENT              0x04U
#define FUSB302_REG_MASKA_SOFTRST             0x02U
#define FUSB302_REG_MASKA_HARDRST             0x01U

#define FUSB302_REG_MASKB                     0x0FU
#define FUSB302_REG_MASKB_GCRCSENT            0x01U

#define FUSB302_REG_STATUS1A                  0x3DU
#define FUSB302_REG_STATUS1A_TOGSS_POS        3U
#define FUSB302_REG_STATUS1A_TOGSS_MASK       0x07U
#define FUSB302_TOGSS_RUNNING                 0x00U
#define FUSB302_TOGSS_SRC1                    0x01U
#define FUSB302_TOGSS_SRC2                    0x02U
#define FUSB302_TOGSS_SNK1                    0x05U
#define FUSB302_TOGSS_SNK2                    0x06U
#define FUSB302_TOGSS_AUDIO_ACCESSORY         0x07U

#define FUSB302_REG_INTERRUPTA                0x3EU
#define FUSB302_REG_INTERRUPTB                0x3FU
#define FUSB302_REG_STATUS0                   0x40U
#define FUSB302_REG_INTERRUPT                 0x42U

typedef struct {
    I2C_HandleTypeDef *hi2c;
    bool present;
    volatile bool irq_pending;
    fusb302_role_t role;
    fusb302_state_t state;
    uint8_t device_id;
} fusb302_ctx_t;

static fusb302_ctx_t g_fusb302;

static fusb302_status_t fusb302_from_hal(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return FUSB302_STATUS_OK;
    }
    if (status == HAL_TIMEOUT) {
        return FUSB302_STATUS_TIMEOUT;
    }
    return FUSB302_STATUS_ERROR;
}

static fusb302_status_t fusb302_read_reg(uint8_t reg, uint8_t *value)
{
    if ((g_fusb302.hi2c == NULL) || (value == NULL)) {
        return FUSB302_STATUS_BAD_ARG;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(g_fusb302.hi2c,
                                                FUSB302_I2C_ADDR_HAL,
                                                reg,
                                                I2C_MEMADD_SIZE_8BIT,
                                                value,
                                                1U,
                                                FUSB302_I2C_TIMEOUT_MS);
    return fusb302_from_hal(status);
}

static fusb302_status_t fusb302_write_reg(uint8_t reg, uint8_t value)
{
    if (g_fusb302.hi2c == NULL) {
        return FUSB302_STATUS_BAD_ARG;
    }

    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(g_fusb302.hi2c,
                                                 FUSB302_I2C_ADDR_HAL,
                                                 reg,
                                                 I2C_MEMADD_SIZE_8BIT,
                                                 &value,
                                                 1U,
                                                 FUSB302_I2C_TIMEOUT_MS);
    return fusb302_from_hal(status);
}

static fusb302_status_t fusb302_update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0U;
    fusb302_status_t status = fusb302_read_reg(reg, &current);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    current = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
    return fusb302_write_reg(reg, current);
}

static fusb302_role_t fusb302_role_from_status1a(uint8_t status1a, fusb302_cc_state_t *cc)
{
    const uint8_t togss = (uint8_t)((status1a >> FUSB302_REG_STATUS1A_TOGSS_POS)
                                   & FUSB302_REG_STATUS1A_TOGSS_MASK);

    /*
     * STATUS1A.TOGSS is the FUSB302 hardware DRP result:
     * - SRC1/SRC2: FUSB302 stopped as Source on CC1/CC2 after detecting Rd.
     *   The attached cable partner is a sink/peripheral, so this product must
     *   become USB Host later.
     * - SNK1/SNK2: FUSB302 stopped as Sink on CC1/CC2 after detecting Rp.
     *   The cable partner sources VBUS, typically a PC/USB host, so this
     *   product must become USB Device later.
     * - RUNNING: no settled CC attachment yet.
     * - AUDIO_ACCESSORY/other: not a supported USB data role here.
     */
    if (cc != NULL) {
        *cc = FUSB302_CC_UNKNOWN;
    }

    switch (togss) {
    case FUSB302_TOGSS_RUNNING:
        if (cc != NULL) {
            *cc = FUSB302_CC_OPEN;
        }
        return FUSB302_ROLE_NONE;

    case FUSB302_TOGSS_SRC1:
        if (cc != NULL) {
            *cc = FUSB302_CC_ACTIVE_CC1;
        }
        return FUSB302_ROLE_HOST;

    case FUSB302_TOGSS_SRC2:
        if (cc != NULL) {
            *cc = FUSB302_CC_ACTIVE_CC2;
        }
        return FUSB302_ROLE_HOST;

    case FUSB302_TOGSS_SNK1:
        if (cc != NULL) {
            *cc = FUSB302_CC_ACTIVE_CC1;
        }
        return FUSB302_ROLE_DEVICE;

    case FUSB302_TOGSS_SNK2:
        if (cc != NULL) {
            *cc = FUSB302_CC_ACTIVE_CC2;
        }
        return FUSB302_ROLE_DEVICE;

    case FUSB302_TOGSS_AUDIO_ACCESSORY:
        if (cc != NULL) {
            *cc = FUSB302_CC_AUDIO_ACCESSORY;
        }
        return FUSB302_ROLE_UNKNOWN;

    default:
        return FUSB302_ROLE_UNKNOWN;
    }
}

static fusb302_status_t fusb302_read_state(bool clear_interrupts)
{
    fusb302_state_t state = g_fusb302.state;
    fusb302_status_t status;

    if (clear_interrupts) {
        status = fusb302_read_reg(FUSB302_REG_INTERRUPT, &state.interrupt);
        if (status != FUSB302_STATUS_OK) {
            return status;
        }
        status = fusb302_read_reg(FUSB302_REG_INTERRUPTA, &state.interrupta);
        if (status != FUSB302_STATUS_OK) {
            return status;
        }
        status = fusb302_read_reg(FUSB302_REG_INTERRUPTB, &state.interruptb);
        if (status != FUSB302_STATUS_OK) {
            return status;
        }
    }

    status = fusb302_read_reg(FUSB302_REG_STATUS0, &state.status0);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    status = fusb302_read_reg(FUSB302_REG_STATUS1A, &state.status1a);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    g_fusb302.role = fusb302_role_from_status1a(state.status1a, &state.cc);
    g_fusb302.state = state;
    return FUSB302_STATUS_OK;
}

static fusb302_status_t fusb302_clear_interrupts(void)
{
    uint8_t discard = 0U;
    fusb302_status_t status = fusb302_read_reg(FUSB302_REG_INTERRUPT, &discard);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    status = fusb302_read_reg(FUSB302_REG_INTERRUPTA, &discard);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    return fusb302_read_reg(FUSB302_REG_INTERRUPTB, &discard);
}

static fusb302_status_t fusb302_probe_device(void)
{
    uint8_t device_id = 0U;

    if (g_fusb302.hi2c == NULL) {
        return FUSB302_STATUS_BAD_ARG;
    }

    HAL_StatusTypeDef ready = HAL_I2C_IsDeviceReady(g_fusb302.hi2c,
                                                    FUSB302_I2C_ADDR_HAL,
                                                    FUSB302_READY_TRIALS,
                                                    FUSB302_I2C_TIMEOUT_MS);
    if (ready != HAL_OK) {
        g_fusb302.present = false;
        return fusb302_from_hal(ready);
    }

    fusb302_status_t status = fusb302_read_reg(FUSB302_REG_DEVICE_ID, &device_id);
    if (status != FUSB302_STATUS_OK) {
        g_fusb302.present = false;
        return status;
    }

    if ((device_id == 0x00U) || (device_id == 0xFFU)) {
        g_fusb302.present = false;
        return FUSB302_STATUS_NOT_PRESENT;
    }

    g_fusb302.device_id = device_id;
    g_fusb302.present = true;
    return FUSB302_STATUS_OK;
}

static fusb302_status_t fusb302_start_drp(void)
{
    fusb302_status_t status;

    status = fusb302_write_reg(FUSB302_REG_POWER, FUSB302_REG_POWER_ALL);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_SWITCHES0, 0x00U);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_SWITCHES1, 0x00U);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_update_reg(FUSB302_REG_CONTROL0,
                                (uint8_t)(FUSB302_REG_CONTROL0_INT_MASK | FUSB302_REG_CONTROL0_HOST_CUR_USB),
                                FUSB302_REG_CONTROL0_HOST_CUR_USB);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_CONTROL0, (uint8_t)(FUSB302_REG_CONTROL0_TX_FLUSH
                                                               | FUSB302_REG_CONTROL0_HOST_CUR_USB));
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_CONTROL1, FUSB302_REG_CONTROL1_RX_FLUSH);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_MASK,
                               (uint8_t)(FUSB302_REG_MASK_CRC_CHK
                                         | FUSB302_REG_MASK_ALERT
                                         | FUSB302_REG_MASK_WAKE
                                         | FUSB302_REG_MASK_COLLISION));
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_MASKA,
                               (uint8_t)(FUSB302_REG_MASKA_OCP_TEMP
                                         | FUSB302_REG_MASKA_SOFTFAIL
                                         | FUSB302_REG_MASKA_RETRYFAIL
                                         | FUSB302_REG_MASKA_HARDSENT
                                         | FUSB302_REG_MASKA_TXSENT
                                         | FUSB302_REG_MASKA_SOFTRST
                                         | FUSB302_REG_MASKA_HARDRST));
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_MASKB, FUSB302_REG_MASKB_GCRCSENT);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_clear_interrupts();
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_CONTROL2, FUSB302_REG_CONTROL2_MODE_DRP);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    return fusb302_update_reg(FUSB302_REG_CONTROL2,
                              (uint8_t)(FUSB302_REG_CONTROL2_MODE_MASK | FUSB302_REG_CONTROL2_TOGGLE),
                              (uint8_t)(FUSB302_REG_CONTROL2_MODE_DRP | FUSB302_REG_CONTROL2_TOGGLE));
}

fusb302_status_t fusb302_init(I2C_HandleTypeDef *hi2c)
{
    memset(&g_fusb302, 0, sizeof(g_fusb302));
    g_fusb302.hi2c = hi2c;
    g_fusb302.role = FUSB302_ROLE_NONE;

    fusb302_status_t status = fusb302_probe_device();
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_write_reg(FUSB302_REG_RESET, FUSB302_REG_RESET_SW_RESET);
    if (status != FUSB302_STATUS_OK) {
        g_fusb302.present = false;
        return status;
    }
    HAL_Delay(2U);

    status = fusb302_probe_device();
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    status = fusb302_start_drp();
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    return fusb302_read_state(true);
}

fusb302_status_t fusb302_read_role(fusb302_role_t *role)
{
    if (role == NULL) {
        return FUSB302_STATUS_BAD_ARG;
    }
    if (!g_fusb302.present) {
        *role = FUSB302_ROLE_NONE;
        return FUSB302_STATUS_NOT_PRESENT;
    }

    fusb302_status_t status = fusb302_read_state(false);
    *role = g_fusb302.role;
    return status;
}

fusb302_status_t fusb302_handle_interrupt(void)
{
    if (!g_fusb302.present) {
        return FUSB302_STATUS_NOT_PRESENT;
    }

    const bool int_asserted = (HAL_GPIO_ReadPin(FUSB302_INT_N_GPIO_Port, FUSB302_INT_N_Pin) == GPIO_PIN_RESET);
    if (!g_fusb302.irq_pending && !int_asserted) {
        return FUSB302_STATUS_OK;
    }

    g_fusb302.irq_pending = false;
    return fusb302_read_state(true);
}

bool fusb302_is_present(void)
{
    return g_fusb302.present;
}

bool fusb302_irq_pending(void)
{
    return g_fusb302.irq_pending;
}

fusb302_role_t fusb302_cached_role(void)
{
    return g_fusb302.role;
}

fusb302_state_t fusb302_cached_state(void)
{
    return g_fusb302.state;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == FUSB302_INT_N_Pin) {
        g_fusb302.irq_pending = true;
        __DMB();
        usb_service_wakeup(USB_SERVICE_WAKE_WORK);
    }
}
