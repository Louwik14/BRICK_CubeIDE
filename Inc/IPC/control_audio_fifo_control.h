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

void control_audio_fifo_control_init(void);
uint16_t control_audio_fifo_control_free(void);
uint8_t control_audio_fifo_publish(const control_audio_command_t *command);
uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count);
uint8_t control_audio_fifo_batch_begin(control_audio_fifo_batch_writer_t *writer,
                                       uint16_t count);
uint8_t control_audio_fifo_batch_append(control_audio_fifo_batch_writer_t *writer,
                                        const control_audio_command_t *command);
uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer);
void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer);
