#ifndef BRICK6_ENCODER_CONTROL_DISPATCHER_H
#define BRICK6_ENCODER_CONTROL_DISPATCHER_H

#include <stdint.h>

#include "UI/ui_param.h"

void encoder_control_dispatcher_init(void);
uint8_t encoder_control_dispatcher_service(const ui_param_encoder_context_t *context);

#endif /* BRICK6_ENCODER_CONTROL_DISPATCHER_H */
