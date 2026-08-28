#ifndef CONTROL_AUDIO_FIFO_H
#define CONTROL_AUDIO_FIFO_H

#include <stdint.h>
#include "Audio/control_audio_command.h"

/* Contract burst: 1024 PARAM + 2*(233 internal + 128 admitted external NOTE)
 * + 35 PROGRAM/TRANSPORT/RECORD/PANIC = 1781.  2048 leaves 267 slots and
 * keeps mask-based monotone indexing on both H743 and future H747. */
#define CONTROL_AUDIO_FIFO_MAX_PARAM_BURST   1024U
#define CONTROL_AUDIO_FIFO_MAX_NOTE_BURST     722U
#define CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST   35U
#define CONTROL_AUDIO_FIFO_CONTRACT_BURST \
    (CONTROL_AUDIO_FIFO_MAX_PARAM_BURST \
     + CONTROL_AUDIO_FIFO_MAX_NOTE_BURST \
     + CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST)
#define CONTROL_AUDIO_FIFO_CAPACITY 2048U

_Static_assert((CONTROL_AUDIO_FIFO_CAPACITY
                & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)) == 0U,
               "functional FIFO capacity must be a power of two");
_Static_assert(CONTROL_AUDIO_FIFO_CAPACITY >= CONTROL_AUDIO_FIFO_CONTRACT_BURST,
               "functional FIFO cannot contain the contractual worst burst");

void control_audio_fifo_init(void);
uint16_t control_audio_fifo_control_free(void);
uint8_t control_audio_fifo_publish(const control_audio_command_t *command);
uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count);
typedef struct
{
    uint32_t head;
    uint16_t count;
    uint16_t written;
    uint64_t floor;
    uint8_t active;
} control_audio_fifo_batch_writer_t;
uint8_t control_audio_fifo_batch_begin(control_audio_fifo_batch_writer_t *writer,
                                       uint16_t count);
uint8_t control_audio_fifo_batch_append(control_audio_fifo_batch_writer_t *writer,
                                        const control_audio_command_t *command);
uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer);
void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer);
uint8_t control_audio_fifo_publish_fenced(const control_audio_command_t *command,
                                          uint32_t *out_consumer_fence);
uint8_t control_audio_fifo_control_fence_consumed(uint32_t consumer_fence);
uint8_t control_audio_fifo_audio_peek(control_audio_command_t *out_command);
uint8_t control_audio_fifo_audio_pop(void);
uint32_t control_audio_fifo_audio_head_snapshot(void);
uint8_t control_audio_fifo_audio_tail_before(uint32_t head_limit);
uint16_t control_audio_fifo_audio_frames_until_due(uint64_t sample_now,
                                                   uint16_t max_frames,
                                                   uint32_t head_limit);

#endif
