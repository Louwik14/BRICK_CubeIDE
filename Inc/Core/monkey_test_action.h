#ifndef MONKEY_TEST_ACTION_H
#define MONKEY_TEST_ACTION_H

#include <stdint.h>

#include "Core/brick_build_config.h"

typedef enum
{
    MONKEY_TEST_ACTION_NONE = 0,
    MONKEY_TEST_ACTION_BUTTON_PRESS,
    MONKEY_TEST_ACTION_BUTTON_RELEASE,
    MONKEY_TEST_ACTION_ENCODER_DELTA,
    MONKEY_TEST_ACTION_KEY_PRESS,
    MONKEY_TEST_ACTION_KEY_RELEASE
} monkey_test_action_type_t;

typedef struct
{
    uint32_t index;
    uint32_t logical_tick;
    uint32_t delay_ticks;
    monkey_test_action_type_t type;
    uint8_t target;
    int16_t value;
} monkey_test_action_t;

enum
{
    MONKEY_TEST_CONTEXT_KEYBOARD = (1U << 0),
    MONKEY_TEST_CONTEXT_TRANSPORT = (1U << 1),
    MONKEY_TEST_CONTEXT_SETTINGS = (1U << 2)
};

#define MONKEY_TEST_ACTION_PENDING_MAX 4U

typedef struct
{
    uint32_t seed;
    uint32_t rng_state;
    uint32_t next_index;
    uint32_t logical_tick;
    uint32_t context_flags;
    monkey_test_action_t pending[MONKEY_TEST_ACTION_PENDING_MAX];
    uint8_t pending_count;
    uint8_t pending_read;
} monkey_test_generator_t;

#if BRICK_TEST_BUILD

void monkey_test_generator_init(monkey_test_generator_t *generator, uint32_t seed);
void monkey_test_generator_set_context(monkey_test_generator_t *generator,
                                       uint32_t context_flags);
uint8_t monkey_test_generator_next(monkey_test_generator_t *generator,
                                   monkey_test_action_t *out_action);
const char *monkey_test_action_type_label(monkey_test_action_type_t type);

#endif

#endif
