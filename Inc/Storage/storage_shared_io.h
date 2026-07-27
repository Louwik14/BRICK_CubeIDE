#ifndef STORAGE_SHARED_IO_H
#define STORAGE_SHARED_IO_H

#include <stdint.h>

#define STORAGE_SHARED_IO_BYTES (512U * 126U)

extern uint8_t g_storage_shared_io[STORAGE_SHARED_IO_BYTES];

#endif /* STORAGE_SHARED_IO_H */
