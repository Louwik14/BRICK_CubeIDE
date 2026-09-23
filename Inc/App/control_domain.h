#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void control_domain_init(void);
void control_domain_start(float postgain, float output_compensation);
void control_domain_resume_after_hall_calibration(void);

#ifdef __cplusplus
}
#endif
