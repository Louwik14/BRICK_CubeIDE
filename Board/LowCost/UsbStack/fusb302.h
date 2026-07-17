#ifndef FUSB302_H
#define FUSB302_H

#include "stm32h7xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FUSB302_STATUS_OK = 0,
    FUSB302_STATUS_ERROR,
    FUSB302_STATUS_TIMEOUT,
    FUSB302_STATUS_NOT_PRESENT,
    FUSB302_STATUS_BAD_ARG
} fusb302_status_t;

typedef enum {
    FUSB302_ROLE_NONE = 0,
    FUSB302_ROLE_DEVICE,
    FUSB302_ROLE_HOST,
    FUSB302_ROLE_UNKNOWN
} fusb302_role_t;

typedef enum {
    FUSB302_CC_OPEN = 0,
    FUSB302_CC_ACTIVE_CC1,
    FUSB302_CC_ACTIVE_CC2,
    FUSB302_CC_AUDIO_ACCESSORY,
    FUSB302_CC_UNKNOWN
} fusb302_cc_state_t;

typedef struct {
    uint8_t interrupt;
    uint8_t interrupta;
    uint8_t interruptb;
    uint8_t status0;
    uint8_t status1a;
    fusb302_cc_state_t cc;
} fusb302_state_t;

fusb302_status_t fusb302_init(I2C_HandleTypeDef *hi2c);
fusb302_status_t fusb302_read_role(fusb302_role_t *role);
fusb302_status_t fusb302_handle_interrupt(void);
bool fusb302_is_present(void);
bool fusb302_irq_pending(void);
fusb302_role_t fusb302_cached_role(void);
fusb302_state_t fusb302_cached_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FUSB302_H */
