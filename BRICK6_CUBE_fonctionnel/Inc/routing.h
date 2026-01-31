#ifndef ROUTING_H
#define ROUTING_H

typedef enum
{
  ROUTE_SRC_SAI = 0,
  ROUTE_SRC_USB,
  ROUTE_SRC_SD,
  ROUTE_SRC_MIX
} route_source_t;

route_source_t routing_get_source(void);

#endif /* ROUTING_H */
