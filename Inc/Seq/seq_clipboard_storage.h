#ifndef SEQ_CLIPBOARD_STORAGE_H
#define SEQ_CLIPBOARD_STORAGE_H

#include <stdint.h>

#include "Seq/seq_step_snapshot.h"

#define SEQ_CLIPBOARD_DENSE_PLOCK_BYTES_PER_LANE \
    (SEQ_MAX_STEPS * SEQ_STEP_MAX_LOCKS * sizeof(seq_step_snapshot_plock_t))
#define SEQ_CLIPBOARD_COMPACT_PLOCK_BYTES_PER_LANE \
    (SEQ_PLOCK_POOL_CAP_PER_TRACK * sizeof(seq_clipboard_plock_t))

typedef struct
{
    seq_value16_t value16;
    seq_plock_key_t key;
    uint8_t flags;
} seq_clipboard_plock_t;

typedef struct
{
    uint16_t lock_offset;
    uint8_t valid;
    uint8_t trig;
    uint8_t roll;
    uint8_t lock_count;
    uint8_t reserved[2];
    seq_play_snapshot_t play;
} seq_clipboard_step_t;

_Static_assert(sizeof(seq_clipboard_plock_t) == 4U,
               "clipboard P-lock storage size changed");
_Static_assert(sizeof(seq_clipboard_step_t) == 28U,
               "clipboard step metadata size changed");
_Static_assert((SEQ_CLIPBOARD_DENSE_PLOCK_BYTES_PER_LANE
                - SEQ_CLIPBOARD_COMPACT_PLOCK_BYTES_PER_LANE) == 10240U,
               "clipboard per-lane RAM gain changed");

uint8_t seq_clipboard_storage_pack_step(
    const seq_step_snapshot_t *snapshot,
    seq_clipboard_step_t *out_step,
    seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK],
    uint16_t *io_lock_count);

uint8_t seq_clipboard_storage_unpack_step(
    const seq_clipboard_step_t *step,
    const seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK],
    seq_step_snapshot_t *out_snapshot);

#endif /* SEQ_CLIPBOARD_STORAGE_H */
