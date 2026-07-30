#ifndef MONKEY_TEST_SAFETY_H
#define MONKEY_TEST_SAFETY_H

#include <stdint.h>

#include "Core/brick_build_config.h"

#if BRICK_TEST_BUILD

void monkey_test_safety_init(void);
uint8_t monkey_test_safety_prepare(void);
uint8_t monkey_test_safety_restore(void);
uint8_t monkey_test_safety_is_active(void);

#endif

#endif
