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

typedef enum
{
    CRASH_WRITER_UNINITIALIZED = 0x00000000UL,
    CRASH_WRITER_READY         = 0x00000001UL,
    CRASH_WRITER_OK            = 0x00001000UL,
    CRASH_WRITER_NO_DESTINATION= 0x0000E001UL,
    CRASH_WRITER_UNLOCK_FAILED = 0x0000E002UL,
    CRASH_WRITER_PROGRAM_FAILED= 0x0000E003UL,
    CRASH_WRITER_COMMIT_FAILED = 0x0000E004UL,
    CRASH_WRITER_ALREADY_ACTIVE= 0x0000E005UL,
    CRASH_CLEAR_OK             = 0x0000C100UL,
    CRASH_CLEAR_UNLOCK_FAILED  = 0x0000C101UL,
    CRASH_CLEAR_ERASE_A_FAILED = 0x0000C102UL,
    CRASH_CLEAR_ERASE_B_FAILED = 0x0000C103UL,
    CRASH_CLEAR_VERIFY_FAILED  = 0x0000C104UL
} crash_writer_status_t;

void crash_library_init(void);
void crash_library_capture_and_persist(const brick_fatal_record_t *fatal);

/* Optional, allocation-free extension point. Modules may publish up to 128
 * diagnostic words without making the normal path maintain a logger. */
uint32_t crash_library_capture_extra(uint32_t *words, uint32_t capacity);

#endif
