#ifndef SIM_PATH_H
#define SIM_PATH_H
#include <stddef.h>
int sim_path_resolve_output(const char *requested, char *resolved,
                            size_t resolved_size, char *error,
                            size_t error_size);
#endif

