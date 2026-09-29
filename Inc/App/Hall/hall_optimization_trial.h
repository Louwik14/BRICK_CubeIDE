#ifndef APP_HALL_HALL_OPTIMIZATION_TRIAL_H
#define APP_HALL_HALL_OPTIMIZATION_TRIAL_H

/* Temporary diagnostic: keep these Hall acquisition decisions out of IPA. */
#define HALL_O0_NOIPA __attribute__((optimize("O0"), noipa))

#endif
