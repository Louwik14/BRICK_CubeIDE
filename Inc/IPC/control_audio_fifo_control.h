#pragma once

#include <stdint.h>
#include "IPC/control_audio_fifo_layout.h"

typedef struct
{
    uint32_t head;
    uint16_t count;
    uint16_t written;
    uint64_t floor;
    uint8_t active;
} control_audio_fifo_batch_writer_t;

typedef enum
{
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_NONE = 0U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_PUBLISH_ARGUMENT = 1U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_BATCH_BEGIN_ARGUMENT = 2U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_BATCH_BEGIN_CAPACITY = 3U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_STATE = 4U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_COMMAND = 5U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_RETROGRADE_TIME = 6U,
    CONTROL_AUDIO_FIFO_DEBUG_REJECT_COMMIT_STATE = 7U
} control_audio_fifo_debug_reject_reason_t;

extern volatile uint32_t g_control_audio_fifo_debug_reject_reason;
extern volatile uint32_t g_control_audio_fifo_debug_index;
extern volatile uint32_t g_control_audio_fifo_debug_head;
extern volatile uint32_t g_control_audio_fifo_debug_tail;
extern volatile uint64_t g_control_audio_fifo_debug_floor;
extern volatile uint64_t g_control_audio_fifo_debug_command_time;
extern volatile uint32_t g_control_audio_fifo_debug_opcode;
extern volatile uint32_t g_control_audio_fifo_debug_entity;
extern volatile uint32_t g_control_audio_fifo_debug_param_id;
extern volatile uint64_t g_fifo_debug_rejected_time;
extern volatile uint64_t g_fifo_debug_floor;
extern volatile uint16_t g_fifo_debug_param_id;
extern volatile uint8_t g_fifo_debug_entity;
extern volatile uint8_t g_fifo_debug_opcode;

void control_audio_fifo_control_init(void);
uint16_t control_audio_fifo_control_free(void);
uint64_t control_audio_fifo_control_floor(void);
uint8_t control_audio_fifo_publish(const control_audio_command_t *command);
uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count);
uint8_t control_audio_fifo_batch_begin(control_audio_fifo_batch_writer_t *writer,
                                       uint16_t count);
uint8_t control_audio_fifo_batch_append(control_audio_fifo_batch_writer_t *writer,
                                        const control_audio_command_t *command);
uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer);
void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer);
