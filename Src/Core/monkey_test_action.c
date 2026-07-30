#include "Core/monkey_test_action.h"

#include <string.h>

#include "buttons_ids.h"

#define MONKEY_TEST_TICKS_PER_SECOND 1500U
#define MONKEY_TEST_MS_TO_TICKS(ms) \
    ((((uint32_t)(ms) * MONKEY_TEST_TICKS_PER_SECOND) + 999U) / 1000U)
#define MONKEY_TEST_DEFAULT_SEED 0x6D6F6E6BU

static const uint8_t g_monkey_buttons[] = {
    BTN_PARAM_1, BTN_PARAM_2, BTN_PARAM_3, BTN_PARAM_4,
    BTN_PARAM_5, BTN_PARAM_6, BTN_PARAM_7, BTN_TRACK,
    BTN_PLAY, BTN_REC, BTN_TRANSPOSE_UP, BTN_TRANSPOSE_DOWN,
    BTN_COPY, BTN_PASTE, BTN_SETTINGS,
    BTN_PAGE_1, BTN_PAGE_2, BTN_PAGE_3, BTN_PAGE_4,
    BTN_ENCODER_1_PUSH, BTN_ENCODER_2_PUSH,
    BTN_ENCODER_3_PUSH, BTN_ENCODER_4_PUSH,
    BTN_STEP_1, BTN_STEP_2, BTN_STEP_3, BTN_STEP_4,
    BTN_STEP_5, BTN_STEP_6, BTN_STEP_7, BTN_STEP_8,
    BTN_STEP_9, BTN_STEP_10, BTN_STEP_11, BTN_STEP_12,
    BTN_STEP_13, BTN_STEP_14, BTN_STEP_15, BTN_STEP_16
};

static uint32_t monkey_test_random(monkey_test_generator_t *generator)
{
    uint32_t x = generator->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    generator->rng_state = x;
    return x;
}

static uint32_t monkey_test_random_range(monkey_test_generator_t *generator,
                                         uint32_t minimum,
                                         uint32_t maximum)
{
    const uint32_t span = (maximum - minimum) + 1U;
    return minimum + (monkey_test_random(generator) % span);
}

static uint32_t monkey_test_delay_ms(monkey_test_generator_t *generator,
                                     uint32_t minimum_ms,
                                     uint32_t maximum_ms)
{
    return MONKEY_TEST_MS_TO_TICKS(
        monkey_test_random_range(generator, minimum_ms, maximum_ms));
}

static uint8_t monkey_test_random_button(monkey_test_generator_t *generator)
{
    const uint32_t count =
        (uint32_t)(sizeof(g_monkey_buttons) / sizeof(g_monkey_buttons[0]));
    return g_monkey_buttons[monkey_test_random(generator) % count];
}

static void monkey_test_pending_add(monkey_test_generator_t *generator,
                                    monkey_test_action_type_t type,
                                    uint8_t target,
                                    int16_t value,
                                    uint32_t delay_ticks)
{
    if (generator->pending_count >= MONKEY_TEST_ACTION_PENDING_MAX)
    {
        return;
    }

    monkey_test_action_t *const action =
        &generator->pending[generator->pending_count++];
    memset(action, 0, sizeof(*action));
    action->type = type;
    action->target = target;
    action->value = value;
    action->delay_ticks = delay_ticks;
}

static void monkey_test_generate_button_tap(monkey_test_generator_t *generator)
{
    const uint8_t button = monkey_test_random_button(generator);
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            button, 1,
                            monkey_test_delay_ms(generator, 25U, 350U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            button, 0,
                            monkey_test_delay_ms(generator, 20U, 160U));
}

static void monkey_test_generate_button_hold(monkey_test_generator_t *generator)
{
    const uint8_t button = monkey_test_random_button(generator);
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            button, 1,
                            monkey_test_delay_ms(generator, 40U, 450U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            button, 0,
                            monkey_test_delay_ms(generator, 250U, 2500U));
}

static void monkey_test_generate_shift_combo(monkey_test_generator_t *generator)
{
    uint8_t button = monkey_test_random_button(generator);
    if (button == (uint8_t)BTN_SHIFT)
    {
        button = (uint8_t)BTN_TRACK;
    }

    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            (uint8_t)BTN_SHIFT, 1,
                            monkey_test_delay_ms(generator, 30U, 400U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            button, 1,
                            monkey_test_delay_ms(generator, 1U, 20U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            button, 0,
                            monkey_test_delay_ms(generator, 20U, 250U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            (uint8_t)BTN_SHIFT, 0,
                            monkey_test_delay_ms(generator, 0U, 80U));
}

static void monkey_test_generate_chord(monkey_test_generator_t *generator)
{
    const uint8_t first = monkey_test_random_button(generator);
    uint8_t second = monkey_test_random_button(generator);
    if (second == first)
    {
        second = (uint8_t)BTN_SHIFT;
    }

    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            first, 1,
                            monkey_test_delay_ms(generator, 30U, 500U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_PRESS,
                            second, 1,
                            monkey_test_delay_ms(generator, 0U, 10U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            first, 0,
                            monkey_test_delay_ms(generator, 30U, 500U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_BUTTON_RELEASE,
                            second, 0,
                            monkey_test_delay_ms(generator, 0U, 10U));
}

static void monkey_test_generate_encoder(monkey_test_generator_t *generator,
                                         uint8_t extreme)
{
    const uint8_t encoder = (uint8_t)(monkey_test_random(generator) % 4U);
    const int16_t magnitude = (extreme != 0U)
        ? (int16_t)monkey_test_random_range(generator, 64U, 512U)
        : (int16_t)monkey_test_random_range(generator, 1U, 12U);
    const int16_t delta =
        ((monkey_test_random(generator) & 1U) != 0U) ? magnitude
                                                     : (int16_t)-magnitude;
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_ENCODER_DELTA,
                            encoder, delta,
                            monkey_test_delay_ms(generator, 15U, 350U));
}

static void monkey_test_generate_key(monkey_test_generator_t *generator)
{
    const uint8_t key = (uint8_t)(monkey_test_random(generator) % 24U);
    const int16_t velocity =
        (int16_t)monkey_test_random_range(generator, 1U, 127U);
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_KEY_PRESS,
                            key, velocity,
                            monkey_test_delay_ms(generator, 20U, 300U));
    monkey_test_pending_add(generator, MONKEY_TEST_ACTION_KEY_RELEASE,
                            key, 0,
                            monkey_test_delay_ms(generator, 30U, 1200U));
}

static void monkey_test_fill_pending(monkey_test_generator_t *generator)
{
    generator->pending_count = 0U;
    generator->pending_read = 0U;

    uint32_t choice = monkey_test_random(generator) % 100U;
    if ((generator->context_flags & MONKEY_TEST_CONTEXT_KEYBOARD) != 0U)
    {
        choice = (choice + 10U) % 100U;
    }

    if (choice < 24U)
    {
        monkey_test_generate_button_tap(generator);
    }
    else if (choice < 36U)
    {
        monkey_test_generate_button_hold(generator);
    }
    else if (choice < 52U)
    {
        monkey_test_generate_shift_combo(generator);
    }
    else if (choice < 61U)
    {
        monkey_test_generate_chord(generator);
    }
    else if (choice < 82U)
    {
        monkey_test_generate_encoder(generator, 0U);
    }
    else if (choice < 96U)
    {
        monkey_test_generate_key(generator);
    }
    else
    {
        monkey_test_generate_encoder(generator, 1U);
    }
}

void monkey_test_generator_init(monkey_test_generator_t *generator, uint32_t seed)
{
    if (generator == 0)
    {
        return;
    }

    memset(generator, 0, sizeof(*generator));
    generator->seed = (seed != 0U) ? seed : MONKEY_TEST_DEFAULT_SEED;
    generator->rng_state = generator->seed;
}

void monkey_test_generator_set_context(monkey_test_generator_t *generator,
                                       uint32_t context_flags)
{
    if (generator != 0)
    {
        generator->context_flags = context_flags;
    }
}

uint8_t monkey_test_generator_next(monkey_test_generator_t *generator,
                                   monkey_test_action_t *out_action)
{
    if ((generator == 0) || (out_action == 0))
    {
        return 0U;
    }

    if (generator->pending_read >= generator->pending_count)
    {
        monkey_test_fill_pending(generator);
    }
    if (generator->pending_read >= generator->pending_count)
    {
        return 0U;
    }

    *out_action = generator->pending[generator->pending_read++];
    out_action->index = generator->next_index++;
    generator->logical_tick += out_action->delay_ticks;
    out_action->logical_tick = generator->logical_tick;
    return 1U;
}

const char *monkey_test_action_type_label(monkey_test_action_type_t type)
{
    switch (type)
    {
        case MONKEY_TEST_ACTION_BUTTON_PRESS:
            return "BTN DOWN";
        case MONKEY_TEST_ACTION_BUTTON_RELEASE:
            return "BTN UP";
        case MONKEY_TEST_ACTION_ENCODER_DELTA:
            return "ENCODER";
        case MONKEY_TEST_ACTION_KEY_PRESS:
            return "KEY DOWN";
        case MONKEY_TEST_ACTION_KEY_RELEASE:
            return "KEY UP";
        default:
            return "NONE";
    }
}
