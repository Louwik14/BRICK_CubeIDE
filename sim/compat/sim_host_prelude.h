#ifndef SIM_HOST_PRELUDE_H
#define SIM_HOST_PRELUDE_H

/* STM32 ABI guards target 32-bit pointers. Host-only state is never
 * serialized or exchanged with firmware, so those layout guards do not apply. */
#define _Static_assert(condition, message)
#define ALIGN32

#endif
