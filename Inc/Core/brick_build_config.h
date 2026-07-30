#ifndef BRICK_BUILD_CONFIG_H
#define BRICK_BUILD_CONFIG_H

#ifndef BRICK_TEST_BUILD
#define BRICK_TEST_BUILD 0
#endif

#if (BRICK_TEST_BUILD != 0) && (BRICK_TEST_BUILD != 1)
#error "BRICK_TEST_BUILD must be defined to 0 or 1"
#endif

#endif
