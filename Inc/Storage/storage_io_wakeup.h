#ifndef BRICK6_STORAGE_IO_WAKEUP_H
#define BRICK6_STORAGE_IO_WAKEUP_H

#include <stdint.h>

/* Doorbells only. Storage state and request mailboxes remain authoritative. */
#define STORAGE_IO_WAKE_SD   (1UL << 0)
#define STORAGE_IO_WAKE_WORK (1UL << 1)

void storage_io_wakeup(uint32_t flags);

#endif /* BRICK6_STORAGE_IO_WAKEUP_H */
