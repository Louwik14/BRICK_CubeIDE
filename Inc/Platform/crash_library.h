#ifndef CRASH_LIBRARY_H
#define CRASH_LIBRARY_H
#include <stdint.h>
#include "Platform/brick_fatal.h"
void crash_library_init(void);
void crash_library_capture_and_persist(const brick_fatal_record_t *fatal);
uint32_t crash_library_capture_extra(uint32_t *words, uint32_t capacity);
#endif
