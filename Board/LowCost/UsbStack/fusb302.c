#include "fusb302.h"

#include "main.h"
#include "usb_role_manager.h"

#include <string.h>

#define FUSB302_I2C_ADDR_7BIT                 0x22U
#define FUSB302_I2C_ADDR_HAL                  (FUSB302_I2C_ADDR_7BIT << 1U)
#define FUSB302_I2C_TIMEOUT_MS                2U
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
#define FUSB302_REG_MEASURE                   0x04U
#define FUSB302_REG_MEASURE_HOST_DEFAULT      0x26U

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
#define FUSB302_CONTROL2_DRP_RUNNING          \
    (FUSB302_REG_CONTROL2_MODE_DRP | FUSB302_REG_CONTROL2_TOGGLE)

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
#define FUSB302_REG_STATUS0_VBUSOK            0x80U
#define FUSB302_REG_STATUS0_COMP              0x20U
#define FUSB302_REG_INTERRUPT                 0x42U
#define FUSB302_REG_INTERRUPT_VBUSOK          0x80U
#define FUSB302_REG_INTERRUPT_COMP_CHNG       0x20U
#define FUSB302_REG_INTERRUPT_ALERT           0x08U
#define FUSB302_REG_INTERRUPT_BC_LVL          0x01U
#define FUSB302_REG_INTERRUPTA_OCP_TEMP       0x80U
#define FUSB302_REG_INTERRUPTA_TOGDONE        0x40U
#define FUSB302_REG_INTERRUPTA_ERROR_MASK     0xB0U

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

    /* TOGSS is authoritative only while DRP is looking for a partner, or in
     * the TOGDONE snapshot itself.  Once software stops TOGGLE and configures
     * the attached CC pin, TOGSS may read RUNNING/undefined even though the
     * attachment is still valid.  Keep the latched role until an explicit
     * detach decision restarts DRP. */
    if ((g_fusb302.role == FUSB302_ROLE_NONE)
        || (g_fusb302.role == FUSB302_ROLE_UNKNOWN))
    {
        g_fusb302.role = fusb302_role_from_status1a(state.status1a, &state.cc);
    }
    else
    {
        state.cc = g_fusb302.state.cc;
    }
    g_fusb302.state = state;
    return FUSB302_STATUS_OK;
}

static uint32_t fusb302_classify_state(fusb302_role_t previous_role,
                                       const fusb302_state_t *previous,
                                       uint8_t interrupt_valid)
{
    const fusb302_state_t *const current = &g_fusb302.state;
    uint32_t events = FUSB302_EVENT_NONE;

    if ((interrupt_valid != 0U)
        && ((current->interrupta & FUSB302_REG_INTERRUPTA_TOGDONE) != 0U)
        && ((g_fusb302.role == FUSB302_ROLE_DEVICE)
            || (g_fusb302.role == FUSB302_ROLE_HOST)))
    {
        events |= FUSB302_EVENT_ATTACH;
    }

    if (previous_role == FUSB302_ROLE_DEVICE)
    {
        const uint8_t vbus_fell = (uint8_t)(
            ((previous->status0 & FUSB302_REG_STATUS0_VBUSOK) != 0U)
            && ((current->status0 & FUSB302_REG_STATUS0_VBUSOK) == 0U));
        if (((interrupt_valid != 0U)
             && ((current->interrupt & FUSB302_REG_INTERRUPT_VBUSOK) != 0U)
             && ((current->status0 & FUSB302_REG_STATUS0_VBUSOK) == 0U))
            || (vbus_fell != 0U))
        {
            events |= FUSB302_EVENT_DETACH;
        }
    }
    else if (previous_role == FUSB302_ROLE_HOST)
    {
        const uint8_t comp_changed = (uint8_t)(
            ((previous->status0 ^ current->status0)
             & FUSB302_REG_STATUS0_COMP) != 0U);
        if (((interrupt_valid != 0U)
             && ((current->interrupt & FUSB302_REG_INTERRUPT_COMP_CHNG) != 0U))
            || (comp_changed != 0U))
        {
            events |= FUSB302_EVENT_DETACH;
        }
    }

    if ((previous_role != FUSB302_ROLE_NONE)
        && (previous_role != FUSB302_ROLE_UNKNOWN)
        && (g_fusb302.role == FUSB302_ROLE_NONE))
    {
        events |= FUSB302_EVENT_DETACH;
    }
    if (((previous_role == FUSB302_ROLE_NONE)
            || (previous_role == FUSB302_ROLE_UNKNOWN))
        && ((g_fusb302.role == FUSB302_ROLE_DEVICE)
            || (g_fusb302.role == FUSB302_ROLE_HOST)))
    {
        events |= FUSB302_EVENT_ATTACH;
    }
    if ((interrupt_valid != 0U)
        && ((current->interrupt
             & (FUSB302_REG_INTERRUPT_VBUSOK
                | FUSB302_REG_INTERRUPT_COMP_CHNG
                | FUSB302_REG_INTERRUPT_BC_LVL)) != 0U))
    {
        events |= FUSB302_EVENT_CC_CHANGE;
    }
    if ((interrupt_valid != 0U)
        && (((current->interrupt & FUSB302_REG_INTERRUPT_ALERT) != 0U)
            || ((current->interrupta
                 & FUSB302_REG_INTERRUPTA_ERROR_MASK) != 0U)))
    {
        events |= FUSB302_EVENT_ERROR;
    }
    if ((events & FUSB302_EVENT_DETACH) != 0U)
    {
        g_fusb302.role = FUSB302_ROLE_NONE;
        g_fusb302.state.cc = FUSB302_CC_OPEN;
    }
    return events;
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

static fusb302_status_t fusb302_configure_attached_role(void)
{
    fusb302_status_t status = fusb302_write_reg(
        FUSB302_REG_CONTROL2, FUSB302_REG_CONTROL2_MODE_DRP);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }

    uint8_t switches0;
    if (g_fusb302.role == FUSB302_ROLE_HOST) {
        switches0 = (g_fusb302.state.cc == FUSB302_CC_ACTIVE_CC2)
            ? (uint8_t)(FUSB302_REG_SWITCHES0_PU_EN2
                        | FUSB302_REG_SWITCHES0_MEAS_CC2)
            : (uint8_t)(FUSB302_REG_SWITCHES0_PU_EN1
                        | FUSB302_REG_SWITCHES0_MEAS_CC1);
        status = fusb302_write_reg(
            FUSB302_REG_MEASURE, FUSB302_REG_MEASURE_HOST_DEFAULT);
        if (status != FUSB302_STATUS_OK) {
            return status;
        }
    } else if (g_fusb302.role == FUSB302_ROLE_DEVICE) {
        switches0 = (uint8_t)(FUSB302_REG_SWITCHES0_PDWN1
                              | FUSB302_REG_SWITCHES0_PDWN2
                              | ((g_fusb302.state.cc == FUSB302_CC_ACTIVE_CC2)
                                     ? FUSB302_REG_SWITCHES0_MEAS_CC2
                                     : FUSB302_REG_SWITCHES0_MEAS_CC1));
        status = fusb302_write_reg(FUSB302_REG_MEASURE, 0U);
        if (status != FUSB302_STATUS_OK) {
            return status;
        }
    } else {
        return FUSB302_STATUS_BAD_ARG;
    }

    status = fusb302_write_reg(FUSB302_REG_SWITCHES0, switches0);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    /* Establish the post-configuration comparator/VBUS baseline and discard
     * transitions caused by changing the measurement path itself. */
    status = fusb302_read_state(true);
    return status;
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

    status = fusb302_read_state(true);
    if ((status == FUSB302_STATUS_OK)
        && ((g_fusb302.role == FUSB302_ROLE_DEVICE)
            || (g_fusb302.role == FUSB302_ROLE_HOST))) {
        status = fusb302_configure_attached_role();
    }
    return status;
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

fusb302_status_t fusb302_refresh_state(uint32_t *events)
{
    if (events != NULL) {
        *events = FUSB302_EVENT_NONE;
    }
    if (!g_fusb302.present) {
        return FUSB302_STATUS_NOT_PRESENT;
    }

    const fusb302_role_t previous_role = g_fusb302.role;
    const fusb302_state_t previous = g_fusb302.state;
    const fusb302_status_t status = fusb302_read_state(false);
    if (status == FUSB302_STATUS_OK) {
        const uint32_t classified =
            fusb302_classify_state(previous_role, &previous, 0U);
        if ((classified & FUSB302_EVENT_ATTACH) != 0U) {
            const fusb302_status_t configure_status =
                fusb302_configure_attached_role();
            if (configure_status != FUSB302_STATUS_OK) {
                return configure_status;
            }
        }
        if (events != NULL) {
            *events = classified;
        }
    }
    return status;
}

fusb302_status_t fusb302_handle_interrupt(uint32_t *events)
{
    if (events != NULL) {
        *events = FUSB302_EVENT_NONE;
    }
    if (!g_fusb302.present) {
        return FUSB302_STATUS_NOT_PRESENT;
    }

    const uint32_t primask = __get_PRIMASK();
    bool irq_pending;
    __disable_irq();
    irq_pending = g_fusb302.irq_pending;
    g_fusb302.irq_pending = false;
    __DMB();
    __set_PRIMASK(primask);
    const bool int_asserted = (HAL_GPIO_ReadPin(FUSB302_INT_N_GPIO_Port,
                                                FUSB302_INT_N_Pin)
                               == GPIO_PIN_RESET);
    if (!irq_pending && !int_asserted) {
        return FUSB302_STATUS_OK;
    }

    /* The latch was taken atomically before the reads, so a new EXTI arriving
     * during the transaction remains visible.  Failed acknowledgement is
     * retried by the role manager on its explicit deadline. */
    const fusb302_role_t previous_role = g_fusb302.role;
    const fusb302_state_t previous = g_fusb302.state;
    const fusb302_status_t status = fusb302_read_state(true);
    if (status != FUSB302_STATUS_OK) {
        g_fusb302.irq_pending = true;
        __DMB();
    } else {
        const uint32_t classified =
            fusb302_classify_state(previous_role, &previous, 1U);
        if ((classified & FUSB302_EVENT_ATTACH) != 0U) {
            const fusb302_status_t configure_status =
                fusb302_configure_attached_role();
            if (configure_status != FUSB302_STATUS_OK) {
                g_fusb302.irq_pending = true;
                __DMB();
                return configure_status;
            }
        }
        if (events != NULL) {
            *events = classified;
        }
    }
    return status;
}

fusb302_status_t fusb302_restart_drp(void)
{
    if (!g_fusb302.present) {
        return FUSB302_STATUS_NOT_PRESENT;
    }
    const fusb302_status_t status = fusb302_start_drp();
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    g_fusb302.role = FUSB302_ROLE_NONE;
    g_fusb302.state.cc = FUSB302_CC_OPEN;
    g_fusb302.irq_pending = false;
    __DMB();
    return FUSB302_STATUS_OK;
}

fusb302_status_t fusb302_watchdog(uint32_t *events)
{
    uint8_t device_id = 0U;
    uint8_t control2 = 0U;
    if (events != NULL) {
        *events = FUSB302_EVENT_NONE;
    }
    if (!g_fusb302.present) {
        return FUSB302_STATUS_NOT_PRESENT;
    }
    fusb302_status_t status = fusb302_read_reg(FUSB302_REG_DEVICE_ID,
                                                &device_id);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    status = fusb302_read_reg(FUSB302_REG_CONTROL2, &control2);
    if (status != FUSB302_STATUS_OK) {
        return status;
    }
    const uint8_t expected_control2 =
        ((g_fusb302.role == FUSB302_ROLE_DEVICE)
         || (g_fusb302.role == FUSB302_ROLE_HOST))
            ? FUSB302_REG_CONTROL2_MODE_DRP
            : FUSB302_CONTROL2_DRP_RUNNING;
    if ((device_id != g_fusb302.device_id)
        || ((control2 & (FUSB302_REG_CONTROL2_MODE_MASK
                         | FUSB302_REG_CONTROL2_TOGGLE))
            != expected_control2))
    {
        if (events != NULL) {
            *events = FUSB302_EVENT_RESET;
            if ((g_fusb302.role == FUSB302_ROLE_DEVICE)
                || (g_fusb302.role == FUSB302_ROLE_HOST))
            {
                *events |= FUSB302_EVENT_DETACH;
            }
        }
        g_fusb302.role = FUSB302_ROLE_NONE;
        g_fusb302.state.cc = FUSB302_CC_OPEN;
        return FUSB302_STATUS_OK;
    }
    return fusb302_refresh_state(events);
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
    } else if (GPIO_Pin == HOST_FLAG_Pin) {
        usb_role_manager_host_flag_irq();
    }
}
