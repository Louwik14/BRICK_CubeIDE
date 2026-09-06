#ifndef BRICK6_STORAGE_IO_WAKEUP_H
#define BRICK6_STORAGE_IO_WAKEUP_H

#include <stdint.h>

/* Doorbells only. Storage state and request mailboxes remain authoritative. */
#define STORAGE_IO_WAKE_SD       (1UL << 0)
#define STORAGE_IO_WAKE_RUNNABLE (1UL << 1)

typedef enum
{
    STORAGE_OWNER_STREAM = 0,
    STORAGE_OWNER_RECORDER,
    STORAGE_OWNER_PROJECT,
    STORAGE_OWNER_PATTERN,
    STORAGE_OWNER_PATCH,
    STORAGE_OWNER_SAMPLE_RAM,
    STORAGE_OWNER_WAVETABLE,
    STORAGE_OWNER_MULTI,
    STORAGE_OWNER_CATALOG,
    STORAGE_OWNER_WAV_CONVERT,
    STORAGE_OWNER_WAVEFORM_CACHE,
    STORAGE_OWNER_PREVIEW,
    STORAGE_OWNER_COUNT
} storage_io_owner_t;

void storage_io_wakeup(uint32_t flags);
void storage_io_owner_wakeup(storage_io_owner_t owner);
void storage_io_init(void);

void storage_io_owner_set(storage_io_owner_t owner);
void storage_io_owner_clear(storage_io_owner_t owner);
void storage_io_owner_wait_resource(storage_io_owner_t owner);
void storage_io_resource_available(void);
uint8_t storage_io_owner_test(storage_io_owner_t owner);
uint32_t storage_io_owner_snapshot(void);
uint8_t storage_io_next_deadline_ms(uint32_t now_ms, uint32_t *out_deadline_ms);
void storage_io_schedule_owner_deadline_ms(storage_io_owner_t owner,
                                           uint32_t deadline_ms);
void storage_io_clear_owner_deadline_ms(storage_io_owner_t owner);

/* Audio-clock doorbell for sample-timed Storage continuations. */
void storage_io_schedule_sample_wakeup(storage_io_owner_t owner,
                                       uint64_t sample_time);
void storage_io_sample_event(uint64_t sample_time);

#endif /* BRICK6_STORAGE_IO_WAKEUP_H */
