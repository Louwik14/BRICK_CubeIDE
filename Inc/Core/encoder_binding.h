#ifndef BRICK6_ENCODER_BINDING_H
#define BRICK6_ENCODER_BINDING_H

#include <stdint.h>

/* Fixed, pointer-free binding snapshot shared by UI and encoder capture. */
#define ENCODER_BINDING_ENCODER_COUNT 4U

typedef enum
{
    ENCODER_BINDING_ROUTE_LEGACY = 0U,
    ENCODER_BINDING_ROUTE_AUDIO = 1U
} encoder_binding_route_t;

typedef struct
{
    uint32_t entry[ENCODER_BINDING_ENCODER_COUNT];
} encoder_binding_snapshot_t;

/* 14-bit parameter, 2-bit scope, 4-bit track, 8-bit slot, 4 flags. */
static inline uint32_t encoder_binding_pack(uint16_t parameter,
                                            uint8_t scope,
                                            uint8_t track,
                                            uint8_t slot,
                                            uint8_t shift_down,
                                            encoder_binding_route_t route,
                                            uint8_t track_modifier_down,
                                            uint8_t valid)
{
    return (((uint32_t)parameter) & 0x3FFFU)
         | (((uint32_t)scope & 0x03U) << 14U)
         | (((uint32_t)track & 0x0FU) << 16U)
         | (((uint32_t)slot) << 20U)
         | (((uint32_t)route & 0x01U) << 28U)
         | (((uint32_t)(shift_down != 0U)) << 29U)
         | (((uint32_t)(valid != 0U)) << 30U)
         | (((uint32_t)(track_modifier_down != 0U)) << 31U);
}

static inline uint16_t encoder_binding_parameter(uint32_t entry)
{
    return (uint16_t)(entry & 0x3FFFU);
}

static inline uint8_t encoder_binding_scope(uint32_t entry)
{
    return (uint8_t)((entry >> 14U) & 0x03U);
}

static inline uint8_t encoder_binding_track(uint32_t entry)
{
    return (uint8_t)((entry >> 16U) & 0x0FU);
}

static inline uint8_t encoder_binding_slot(uint32_t entry)
{
    return (uint8_t)((entry >> 20U) & 0xFFU);
}

static inline encoder_binding_route_t encoder_binding_route(uint32_t entry)
{
    return (encoder_binding_route_t)((entry >> 28U) & 0x01U);
}

static inline uint8_t encoder_binding_shift_down(uint32_t entry)
{
    return (uint8_t)((entry >> 29U) & 0x01U);
}

static inline uint8_t encoder_binding_valid(uint32_t entry)
{
    return (uint8_t)((entry >> 30U) & 0x01U);
}

static inline uint8_t encoder_binding_track_modifier_down(uint32_t entry)
{
    return (uint8_t)((entry >> 31U) & 0x01U);
}

#endif /* BRICK6_ENCODER_BINDING_H */
