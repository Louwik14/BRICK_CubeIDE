#ifndef BRICK6_CONTROL_RT_WAKEUP_H
#define BRICK6_CONTROL_RT_WAKEUP_H

#include <stdint.h>

/* CONTROL_RT doorbells are hints only; the application FIFOs remain the
 * source of truth and one bit may represent several pending messages. */
#define CONTROL_RT_WAKE_HALL       (1UL << 0)
#define CONTROL_RT_WAKE_ENCODER    (1UL << 1)
#define CONTROL_RT_WAKE_MIDI       (1UL << 2)
#define CONTROL_RT_WAKE_UI         (1UL << 3)
#define CONTROL_RT_WAKE_STORAGE    (1UL << 4)

void control_rt_wakeup(uint32_t flags);

#endif /* BRICK6_CONTROL_RT_WAKEUP_H */
