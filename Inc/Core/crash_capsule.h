#ifndef CRASH_CAPSULE_H
#define CRASH_CAPSULE_H

#include <stdint.h>

#define CRASH_CAPSULE_BREADCRUMB_CAPACITY 16U

typedef enum
{
    CRASH_CAPSULE_SESSION_NONE = 0,
    CRASH_CAPSULE_SESSION_RUNNING,
    CRASH_CAPSULE_SESSION_STOPPED,
    CRASH_CAPSULE_SESSION_FAULTED,
    CRASH_CAPSULE_SESSION_REPORTED
} crash_capsule_session_state_t;

typedef enum
{
    CRASH_CAPSULE_FAULT_NONE = 0,
    CRASH_CAPSULE_FAULT_HARD,
    CRASH_CAPSULE_FAULT_MEMMANAGE,
    CRASH_CAPSULE_FAULT_BUS,
    CRASH_CAPSULE_FAULT_USAGE,
    CRASH_CAPSULE_FAULT_WATCHDOG
} crash_capsule_fault_type_t;

typedef struct
{
    uint32_t index;
    uint32_t logical_tick;
    uint32_t delay_ticks;
    int16_t value;
    uint8_t type;
    uint8_t target;
} crash_capsule_breadcrumb_t;

typedef struct
{
    uint32_t session_state;
    uint32_t seed;
    uint32_t action_index;
    uint32_t logical_tick;
    uint32_t warning_count;
    uint32_t error_count;
    uint32_t crash_count;
    uint32_t reset_flags;
    uint32_t fault_type;
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
    uint32_t xpsr;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t exc_return;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t bfar;
    uint32_t mmfar;
    uint32_t icsr;
    uint32_t shcsr;
    uint32_t watchdog_armed;
    uint32_t heartbeat_count;
    uint32_t heartbeat_engine_tick;
    uint32_t breadcrumb_count;
    uint32_t breadcrumb_write_index;
    crash_capsule_breadcrumb_t breadcrumbs[CRASH_CAPSULE_BREADCRUMB_CAPACITY];
} crash_capsule_snapshot_t;

void crash_capsule_capture_reset_flags_early(void);
uint8_t crash_capsule_init(void);
uint8_t crash_capsule_is_ready(void);
uint8_t crash_capsule_get_latest(crash_capsule_snapshot_t *out_snapshot);
uint8_t crash_capsule_get_recovery(crash_capsule_snapshot_t *out_snapshot);
uint8_t crash_capsule_get_boot_recovery(
    crash_capsule_snapshot_t *out_snapshot);
uint8_t crash_capsule_recovery_mark_reported(void);
void crash_capsule_begin_session(uint32_t seed);
void crash_capsule_watchdog_arm(uint32_t engine_tick);
void crash_capsule_watchdog_checkpoint(uint32_t heartbeat_count,
                                       uint32_t engine_tick);
void crash_capsule_record_breadcrumb(
    const crash_capsule_breadcrumb_t *breadcrumb,
    uint32_t warning_count,
    uint32_t error_count,
    uint32_t crash_count);
void crash_capsule_end_session(uint32_t warning_count,
                               uint32_t error_count,
                               uint32_t crash_count);
__attribute__((noreturn))
void crash_capsule_fault_capture_and_reset(
    const uint32_t *stack_pointer,
    uint32_t exc_return,
    crash_capsule_fault_type_t fault_type);

#endif
