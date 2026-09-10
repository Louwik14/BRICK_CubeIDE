#ifndef BRICK_FATAL_H
#define BRICK_FATAL_H

#include <stdint.h>

typedef enum
{
    BRICK_FATAL_SEQ_SOURCE_CAPACITY = 0x5301U,
    BRICK_FATAL_SEQ_IMMINENT_CAPACITY = 0x5302U,
    BRICK_FATAL_SEQ_OCCURRENCE_CAPACITY = 0x5303U,
    BRICK_FATAL_MUSIC_STAGING_CAPACITY = 0x4D01U,
    BRICK_FATAL_CONTROL_AUDIO_FIFO_CONTRACT = 0x4301U,
    BRICK_FATAL_AUDIO_INVALID_COMMAND = 0x4101U,
    BRICK_FATAL_AUDIO_PROGRAM_INSTALL = 0x4102U,
    BRICK_FATAL_AUDIO_POLYPHONY = 0x4103U,
    BRICK_FATAL_AUDIO_REBIND = 0x4104U,
    BRICK_FATAL_AUDIO_MAPPING = 0x4105U,
    BRICK_FATAL_SAMPLE_RAM_COMMIT = 0x5201U,
    BRICK_FATAL_WAVETABLE_COMMIT = 0x5701U,
    BRICK_FATAL_PROJECT_COMMIT = 0x5001U
} brick_fatal_code_t;

typedef struct
{
    const char * volatile message;
    const char * volatile file;
    volatile uint32_t line;
    const char * volatile function;
    volatile uint32_t code;
    volatile uint32_t entity;
    volatile uint32_t context;
    volatile uint32_t requested;
    volatile uint32_t capacity;
} brick_fatal_record_t;

extern brick_fatal_record_t g_brick_fatal_record;

_Noreturn void brick_fatal_raise_at(const char *message,
                                    const char *file,
                                    uint32_t line,
                                    const char *function,
                                    brick_fatal_code_t code,
                                    uint32_t entity,
                                    uint32_t context,
                                    uint32_t requested,
                                    uint32_t capacity);

#define BRICK_FATAL(message) \
    brick_fatal_raise_at((message), __FILE__, (uint32_t)__LINE__, __func__, \
                         (brick_fatal_code_t)0U, UINT32_MAX, 0U, 0U, 0U)

#define BRICK_FATAL_CONTEXT(message, code, entity, context, requested, capacity) \
    brick_fatal_raise_at((message), __FILE__, (uint32_t)__LINE__, __func__, \
                         (code), (entity), (context), (requested), (capacity))

#endif
