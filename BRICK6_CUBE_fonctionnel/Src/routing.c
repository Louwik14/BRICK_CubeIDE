#include "routing.h"

#define ROUTE_DEFAULT ROUTE_SRC_SAI

route_source_t routing_get_source(void)
{
  return ROUTE_DEFAULT;
}
