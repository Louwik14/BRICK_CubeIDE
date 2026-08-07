#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_format.h"
#include "Sampler/sample_page_cache.h"

#define SAMPLE_MULTI_STREAM_DIAG_MAGIC            (0x4D534447UL)
#define SAMPLE_MULTI_STREAM_DIAG_MAX_WINDOW_PAGES (6U)

typedef enum
{
    SAMPLE_MULTI_STREAM_DIAG_NONE = 0U,
    SAMPLE_MULTI_STREAM_DIAG_PAGE0_NOT_READY,
    SAMPLE_MULTI_STREAM_DIAG_READER_BIND,
    SAMPLE_MULTI_STREAM_DIAG_RESERVE_PAGE_REQUEST,
    SAMPLE_MULTI_STREAM_DIAG_RESERVE_NEED_MISSING,
    SAMPLE_MULTI_STREAM_DIAG_QUEUE_PAGE_REQUEST,
    SAMPLE_MULTI_STREAM_DIAG_READER_BEGIN,
    SAMPLE_MULTI_STREAM_DIAG_SEGMENT_STATUS,
    SAMPLE_MULTI_STREAM_DIAG_FAULT
} sample_multi_stream_diag_code_t;

typedef struct
{
    uint32_t page_index;
    uint32_t generation;
    uint16_t slot_index;
    uint16_t frame_count;
    uint16_t use_count;
    uint8_t state;
} sample_multi_stream_diag_page_t;

typedef struct
{
    uint32_t magic;
    uint32_t code;
    uint32_t failure_result;
    uint32_t frozen;
    uint16_t sample_id;
    uint16_t key_object_id;
    uint8_t key_domain;
    uint8_t voice_index;
    uint8_t voice_active;
    uint8_t reader_active;
    uint8_t source_kind;
    uint32_t voice_generation;
    uint32_t format;
    uint32_t position_frame;
    uint32_t current_page;
    uint32_t loop_page;
    uint32_t current_expected_pages;
    uint32_t current_acquired_pages;
    uint32_t loop_expected_pages;
    uint32_t loop_acquired_pages;
    uint32_t active_needs;
    uint32_t readers_active;
    uint32_t pages_free;
    uint32_t pc;
    uint32_t lr;
    uint32_t fault_type;
    uint32_t exc_return;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmfar;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_xpsr;
    sample_multi_stream_diag_page_t current_pages[SAMPLE_MULTI_STREAM_DIAG_MAX_WINDOW_PAGES];
    sample_multi_stream_diag_page_t loop_pages[SAMPLE_MULTI_STREAM_DIAG_MAX_WINDOW_PAGES];
} sample_multi_stream_diag_snapshot_t;

#if defined(BRICK6_MULTI_STREAM_DIAG)
extern volatile sample_multi_stream_diag_snapshot_t g_sample_multi_stream_diag;
extern volatile uint32_t g_sample_multi_stream_diag_frozen;

void sample_multi_stream_diag_capture_failure(
    sample_audio_key_t key,
    uint16_t sample_id,
    uint8_t voice_index,
    uint8_t voice_active,
    uint8_t reader_active,
    uint8_t source_kind,
    uint32_t voice_generation,
    sample_audio_format_t format,
    uint32_t position_frame,
    uint32_t current_frame,
    uint32_t loop_frame,
    uint32_t failure_result,
    sample_multi_stream_diag_code_t code);
void sample_multi_stream_diag_capture_fault(const uint32_t *stack_pointer,
                                            uint32_t exc_return,
                                            uint32_t fault_type);
uint8_t sample_multi_stream_diag_breakpoint_pending(void);
__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_breakpoint(void);

#define SAMPLE_MULTI_STREAM_DIAG_CAPTURE_FAILURE(...) \
    sample_multi_stream_diag_capture_failure(__VA_ARGS__)
#else
#define SAMPLE_MULTI_STREAM_DIAG_CAPTURE_FAILURE(...) ((void)0)
#endif
