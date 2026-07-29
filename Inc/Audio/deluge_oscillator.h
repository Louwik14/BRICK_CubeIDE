/*
 * Scalar Cortex-M7 adaptation of the Synthstrom Deluge basic oscillator.
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams.
 * Upstream reference: 0d9cbf0440f0555e2544cc1eb019b31675637008.
 * GPL-3.0; see LICENSES/DelugeFirmware-GPL-3.0.txt.
 */
#pragma once

#include <stdint.h>

typedef enum
{
    DELUGE_OSC_SINE = 0,
    DELUGE_OSC_TRIANGLE,
    DELUGE_OSC_SQUARE,
    DELUGE_OSC_ANALOG_SQUARE,
    DELUGE_OSC_SAW,
    DELUGE_OSC_ANALOG_SAW
} deluge_osc_type_t;

void deluge_oscillator_render(deluge_osc_type_t type,
                              int32_t *output,
                              uint32_t sample_count,
                              uint32_t phase_increment,
                              uint32_t pulse_width,
                              uint32_t *phase);
