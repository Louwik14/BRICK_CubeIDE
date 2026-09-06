#ifndef BRICK6_IPC_CONTROL_RT_WAKEUP_H
#define BRICK6_IPC_CONTROL_RT_WAKEUP_H

#include <stdint.h>

#define CONTROL_RT_WAKE_HALL       (1UL << 0)
#define CONTROL_RT_WAKE_ENCODER    (1UL << 1)
#define CONTROL_RT_WAKE_MIDI       (1UL << 2)
#define CONTROL_RT_WAKE_UI         (1UL << 3)
#define CONTROL_RT_WAKE_STORAGE    (1UL << 4)
#define CONTROL_RT_WAKE_STREAM_RELEASE (1UL << 5)
#define CONTROL_RT_WAKE_DEADLINE   (1UL << 6)
#define CONTROL_RT_WAKE_LATEST     (1UL << 7)
#define CONTROL_RT_WAKE_INPUT      (1UL << 8)

/* Doorbell only; payloads remain in their owning queues/mailboxes. */
void control_rt_wakeup(uint32_t flags);

#endif
