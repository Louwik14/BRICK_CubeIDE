#ifndef CRASH_LIBRARY_H
#define CRASH_LIBRARY_H

#include <stdint.h>

#include "Platform/brick_fatal.h"

#define CRASH_LIBRARY_BASE       0x08180000UL
#define CRASH_LIBRARY_SECTOR_B   0x081A0000UL
#define CRASH_LIBRARY_SECTOR_SIZE (128UL * 1024UL)
#define CRASH_SLOT_SIZE          4096UL
#define CRASH_SLOT_COUNT         20U
#define CRASH_SLOT_AREA_OFFSET   4096UL
#define CRASH_GDB_VIEW_BASE      0x38800000UL
#define CRASH_GDB_CLEAR_ADDRESS  0x388001FCUL
#define CRASH_GDB_CLEAR_MAGIC    0x434C5243UL /* "CRLC" */

void crash_library_init(void);
void crash_library_capture_and_persist(const brick_fatal_record_t *fatal);

/* Optional, allocation-free extension point. Modules may publish up to 128
 * diagnostic words without making the normal path maintain a logger. */
uint32_t crash_library_capture_extra(uint32_t *words, uint32_t capacity);

#endif
