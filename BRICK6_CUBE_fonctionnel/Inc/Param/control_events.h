#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @file control_events.h
 * @brief File d'événements de contrôle lock-free simple.
 *
 * Rôle du module:
 * - Exposer les types d'événements échangés entre tasklets et DSP.
 * - Fournir l'API push/pop de la queue événements.
 */

typedef enum {
    CONTROL_EVT_NONE = 0,
    CONTROL_EVT_FX_RESET,
    CONTROL_EVT_SMOOTHER_RESET,
    CONTROL_EVT_SEQ_TRIG
} control_evt_type_t;

typedef struct {
    control_evt_type_t type;
    uint16_t param0;
    float value;
} control_event_t;

bool control_event_push(const control_event_t *evt);
bool control_event_pop(control_event_t *evt);
void control_event_init(void);
