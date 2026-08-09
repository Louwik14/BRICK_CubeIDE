#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Core/stream_calibration.h"

int brick6_stream_calibration_format_csv_row(
    char *out,
    size_t out_size,
    const brick6_stream_calibration_result_t *result,
    uint16_t scenario,
    uint8_t saved);
