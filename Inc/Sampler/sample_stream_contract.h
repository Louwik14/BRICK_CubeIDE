#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Sampler/sample_stream_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_REQUEST_CONTRACT_VERSION (1U)

/*
 * Pointer-free wire contract for the future M7 -> M4 request queue.
 * All fields have fixed widths; enum values are carried as explicit bytes.
 */
typedef struct
{
    sample_stream_audio_frame_t created_audio_frame;
    sample_stream_audio_frame_t consume_deadline_audio_frame;
    uint32_t page_index;
    uint32_t owner_generation;
    uint32_t registration_epoch;
    uint16_t object_id;
    uint8_t domain;
    uint8_t role;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint8_t format;
    uint8_t flags;
    uint32_t reserved;
} sample_stream_request_contract_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(offsetof(sample_stream_request_contract_t, created_audio_frame) == 0U,
               "stream request created time ABI");
_Static_assert(offsetof(sample_stream_request_contract_t, consume_deadline_audio_frame) == 8U,
               "stream request deadline ABI");
_Static_assert(offsetof(sample_stream_request_contract_t, page_index) == 16U,
               "stream request page ABI");
_Static_assert(offsetof(sample_stream_request_contract_t, object_id) == 28U,
               "stream request object ABI");
_Static_assert(offsetof(sample_stream_request_contract_t, reserved) == 36U,
               "stream request reserved ABI");
_Static_assert(sizeof(sample_stream_request_contract_t) == 40U,
               "stream request wire ABI must remain 40 bytes");
#endif

#ifdef __cplusplus
}
#endif
