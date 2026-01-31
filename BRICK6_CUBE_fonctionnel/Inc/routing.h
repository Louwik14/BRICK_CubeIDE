#ifndef ROUTING_H
#define ROUTING_H

#include <stdint.h>

void routing_apply(const int32_t *src, int32_t *dst, uint32_t frames, uint32_t channels);

#endif /* ROUTING_H */
