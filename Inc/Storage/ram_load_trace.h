#pragma once

#include <stdint.h>

#define RAM_LOAD_TRACE_MAGIC       (0x524C5432UL)
#define RAM_LOAD_TRACE_VERSION     (2UL)
#define RAM_LOAD_TRACE_CAPACITY    (64UL)

typedef enum
{
    RAM_LOAD_TRACE_UI_REQUEST = 1,
    RAM_LOAD_TRACE_PC_BEGIN_ENTER = 2,
    RAM_LOAD_TRACE_PC_REJECT_PENDING = 3,
    RAM_LOAD_TRACE_PC_REJECT_BANK = 4,
    RAM_LOAD_TRACE_POOL_BEGIN_ENTER = 5,
    RAM_LOAD_TRACE_POOL_BEGIN_REJECT = 6,
    RAM_LOAD_TRACE_POOL_LOAD_STARTED = 7,
    RAM_LOAD_TRACE_REQUEST_ACCEPTED = 8,
    RAM_LOAD_TRACE_PC_SERVICE = 9,
    RAM_LOAD_TRACE_POOL_STATE = 10,
    RAM_LOAD_TRACE_SLOT_INSTALLED = 11,
    RAM_LOAD_TRACE_POOL_COMPLETED_SUCCESS = 12,
    RAM_LOAD_TRACE_POOL_COMPLETED_ERROR = 13,
    RAM_LOAD_TRACE_POOL_RESULT_TAKEN = 14,
    RAM_LOAD_TRACE_PC_COMPLETED_SUCCESS = 15,
    RAM_LOAD_TRACE_PC_COMPLETED_ERROR = 16,
    RAM_LOAD_TRACE_RESULT_PENDING = 17,
    RAM_LOAD_TRACE_RESULT_CONSUMED = 18,
    RAM_LOAD_TRACE_RESULT_CLEARED = 19,
    RAM_LOAD_TRACE_UI_BEGIN_RESULT = 20,
    RAM_LOAD_TRACE_UI_BUSY_REJECT = 21,
    RAM_LOAD_TRACE_POOL_CANCELLED = 22,
    RAM_LOAD_TRACE_PC_REJECT_POOL = 23
} ram_load_trace_event_id_t;

typedef enum
{
    RAM_LOAD_TRACE_POOL_REJECT_TRANSPORT = 1,
    RAM_LOAD_TRACE_POOL_REJECT_RECORDER = 2,
    RAM_LOAD_TRACE_POOL_REJECT_WAVETABLE = 3,
    RAM_LOAD_TRACE_POOL_REJECT_MULTI = 4,
    RAM_LOAD_TRACE_POOL_REJECT_JOB_STATE = 5,
    RAM_LOAD_TRACE_POOL_REJECT_SLOT = 6,
    RAM_LOAD_TRACE_POOL_REJECT_PATH = 7,
    RAM_LOAD_TRACE_POOL_REJECT_RETIRING = 8,
    RAM_LOAD_TRACE_POOL_REJECT_PATH_LONG = 9
} ram_load_trace_pool_reject_t;

typedef enum
{
    RAM_LOAD_TRACE_BANK_REJECT_INVALID_BACKEND = 1,
    RAM_LOAD_TRACE_BANK_REJECT_INVALID_PATH = 2,
    RAM_LOAD_TRACE_BANK_REJECT_EXISTING_RUNTIME = 3,
    RAM_LOAD_TRACE_BANK_REJECT_EXISTING_PENDING = 4,
    RAM_LOAD_TRACE_BANK_REJECT_FULL = 5,
    RAM_LOAD_TRACE_BANK_REJECT_UNKNOWN = 6
} ram_load_trace_bank_reject_t;

enum
{
    RAM_LOAD_TRACE_FLAG_PC_PENDING = (1UL << 0),
    RAM_LOAD_TRACE_FLAG_PC_RESULT_VALID = (1UL << 1),
    RAM_LOAD_TRACE_FLAG_POOL_BUSY = (1UL << 2),
    RAM_LOAD_TRACE_FLAG_POOL_DONE = (1UL << 3),
    RAM_LOAD_TRACE_FLAG_WAVETABLE_BUSY = (1UL << 4),
    RAM_LOAD_TRACE_FLAG_MULTI_PENDING = (1UL << 5),
    RAM_LOAD_TRACE_FLAG_TRANSPORT = (1UL << 6),
    RAM_LOAD_TRACE_FLAG_START_PENDING = (1UL << 7),
    RAM_LOAD_TRACE_FLAG_RECORDER = (1UL << 8),
    RAM_LOAD_TRACE_FLAG_SLOT_RETIRING = (1UL << 9),
    RAM_LOAD_TRACE_FLAG_PATH_VALID = (1UL << 10),
    RAM_LOAD_TRACE_FLAG_BANK_RUNTIME = (1UL << 11),
    RAM_LOAD_TRACE_FLAG_BANK_PENDING = (1UL << 12),
    RAM_LOAD_TRACE_FLAG_RESULT_SUCCESS = (1UL << 13)
};

typedef struct
{
    uint32_t tick_ms;
    uint32_t event;
    uint32_t sequence;
    uint32_t project_state;
    uint32_t pool_state;
    uint32_t owner_flags;
    uint32_t arg0;
    uint32_t arg1;
} ram_load_trace_entry_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t entry_size;
    uint32_t capacity;
    uint32_t write_count;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    ram_load_trace_entry_t entries[RAM_LOAD_TRACE_CAPACITY];
} ram_load_trace_block_t;

extern volatile ram_load_trace_block_t g_ram_load_trace;

void ram_load_trace_write(uint32_t event,
                          uint32_t sequence,
                          uint32_t project_state,
                          uint32_t pool_state,
                          uint32_t flags,
                          uint32_t arg0,
                          uint32_t arg1);
uint32_t ram_load_trace_next_sequence(void);
void ram_load_trace_set_active_sequence(uint32_t sequence);
uint32_t ram_load_trace_active_sequence(void);
uint32_t ram_load_trace_path_hash(const char *path);
