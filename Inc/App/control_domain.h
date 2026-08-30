#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void control_domain_init(void);
void control_domain_start(float postgain, float output_compensation);

#ifdef __cplusplus
}
#endif
