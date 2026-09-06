#ifndef PROJECT_LOAD_QUIESCE_H
#define PROJECT_LOAD_QUIESCE_H

#include <stdint.h>

void project_load_quiesce_init(void);
void project_load_quiesce_request(void);
void project_load_quiesce_control_process(void);
void project_load_quiesce_storage_retire(void);
uint8_t project_load_quiesce_safe(void);
uint8_t project_load_quiesce_failed(void);
void project_load_quiesce_end(void);
uint8_t project_load_ingress_is_open(void);
uint8_t project_replacement_is_active(void);
uint8_t project_load_allowed(void);
uint8_t project_transport_stopped_stable(void);

#endif
