#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "IPC/live_parameter_event.h"

#define LIVE_PARAMETER_EVENT_QUEUE_CAPACITY 64U

_Static_assert((LIVE_PARAMETER_EVENT_QUEUE_CAPACITY
                & (LIVE_PARAMETER_EVENT_QUEUE_CAPACITY - 1U)) == 0U,
               "live parameter event queue capacity must be a power of two");

void live_parameter_event_init(void);
bool live_parameter_event_submit(const live_parameter_event_t *event);
bool live_parameter_event_peek(live_parameter_event_t *out_event);
void live_parameter_event_consume(void);
bool live_parameter_event_pop(live_parameter_event_t *out_event);
uint16_t live_parameter_event_depth(void);
uint32_t live_parameter_event_drop_count(void);
