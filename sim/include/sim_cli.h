#ifndef SIM_CLI_H
#define SIM_CLI_H
#include "sim_engine.h"
int sim_cli_run(sim_engine_t *engine);
int sim_cli_self_test(sim_engine_t *engine, const char *output_dir);
#endif

