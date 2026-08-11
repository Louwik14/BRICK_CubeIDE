#pragma once

#include "Storage/generic_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

generic_recorder_reservation_t generic_recorder_fatfs_reservation_adapter(
    recorder_file_reservation_t *session);
generic_recorder_transport_t generic_recorder_sd_block_device_adapter(void);

#ifdef __cplusplus
}
#endif
