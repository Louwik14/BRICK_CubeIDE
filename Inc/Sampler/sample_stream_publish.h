#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_io.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t sample_stream_publish_result(const sample_stream_io_result_t *result);

#ifdef __cplusplus
}
#endif
