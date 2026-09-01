#ifndef BRICK_BUILD_CONFIG_H
#define BRICK_BUILD_CONFIG_H

#if defined(BRICK6_VARIANT_LOWCOST) && defined(BRICK6_VARIANT_PREMIUM)
#error "BRICK6_VARIANT_LOWCOST and BRICK6_VARIANT_PREMIUM are mutually exclusive"
#elif !defined(BRICK6_VARIANT_LOWCOST) && !defined(BRICK6_VARIANT_PREMIUM)
#error "A supported BRICK6 variant must be selected"
#endif

#define BRICK6_LOOPER_GLOBAL_CAP 1U
#define BRICK6_LOOPER_SHIFTER_SLOT_CAP 1U

#endif
