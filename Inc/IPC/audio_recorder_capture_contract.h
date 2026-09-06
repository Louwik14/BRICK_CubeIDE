#pragma once

#include "IPC/audio_recorder_capture.h"

/*
 * Recorder AUDIO/STORAGE data plane.
 *
 * AUDIO is the sole producer: it writes the PCM24 samples in the ring and
 * publishes head_cursor only after the payload is visible.  AUDIO also owns
 * closed_session and capture_fault; closed_session means that no further
 * frame will be produced for that session, while capture_fault is terminal
 * for that capture.
 *
 * STORAGE is the sole consumer: it reads only the published payload, and
 * advances tail_cursor only after the corresponding bytes have been
 * committed by the existing writer/SD path.  tail_cursor is therefore a
 * reclamation cursor, not an ACK or a desired/applied state.
 *
 * The contract intentionally contains no pointer, callback, context, file
 * object, scheduler state or private buffer.  It is the complete shared
 * Recorder contract for the future CM7 AUDIO -> CM4 STORAGE split.
 *
 * Publication order is AUDIO payload -> DMB -> head_cursor and STORAGE
 * commit -> DMB -> tail_cursor.  AUDIO may inspect tail_cursor only to apply
 * the existing bounded-ring overflow policy; it must never wait for it or
 * call Storage, SD, DMA or FatFs from the audio path.
 */
typedef struct
{
    /* Written by AUDIO, read by STORAGE and AUDIO overflow accounting. */
    volatile uint32_t head_cursor;
    /* Written only by STORAGE after physical commit. */
    volatile uint32_t tail_cursor;
    /* Written only by AUDIO when production ends for the active session. */
    volatile uint32_t closed_session;
    /* Written only by AUDIO; non-zero is terminal for that capture. */
    volatile uint32_t capture_fault;
} audio_recorder_capture_transport_t;

_Static_assert(sizeof(int32_t) == 4U,
               "Recorder ring element ABI changed");
_Static_assert(sizeof(audio_recorder_capture_transport_t) == 16U,
               "Recorder capture transport ABI changed");

extern int32_t g_audio_recorder_capture_ring
    [AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
extern audio_recorder_capture_transport_t g_audio_recorder_capture;

/* Audio-owned command/read surface used by STORAGE without reaching into the
 * AUDIO implementation header. */
void audio_recorder_capture_audio_init(void);
uint8_t audio_recorder_capture_audio_start(uint8_t client,
                                           uint32_t session_id,
                                           uint32_t frame_limit);
uint8_t audio_recorder_capture_audio_stop(uint8_t client,
                                          uint32_t session_id);
uint8_t audio_recorder_capture_audio_pending(void);
