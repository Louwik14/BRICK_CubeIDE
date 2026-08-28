#ifndef CONTROL_AUDIO_PROGRAM_H
#define CONTROL_AUDIO_PROGRAM_H

#include <stdint.h>

#define CONTROL_AUDIO_PROGRAM_CAPACITY 256U
#define CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER   (1U << 0)
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER (1U << 6)
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD  (1U << 7)

typedef struct
{
    uint8_t family;
    uint8_t type;
    uint8_t topology_flags;
    uint8_t reserved;
} control_audio_program_descriptor_t;

_Static_assert(sizeof(control_audio_program_descriptor_t) == 4U,
               "prepared program descriptor layout changed");

void control_audio_program_init(void);
uint32_t control_audio_program_prepare(
    const control_audio_program_descriptor_t *descriptor);
/* AUDIO receives a value copy. No address into the CONTROL-owned registry is
 * part of the inter-core contract. */
uint8_t control_audio_program_resolve(
    uint32_t program_id, control_audio_program_descriptor_t *out_descriptor);
/* M4 cancel for a descriptor that was never published. */
void control_audio_program_cancel(uint32_t program_id);
/* M7 memory-lifecycle credit, emitted only after the FIFO tail passed PROGRAM. */
void control_audio_program_consumer_release(uint32_t program_id);

#endif
