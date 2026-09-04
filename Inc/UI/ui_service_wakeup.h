#ifndef BRICK6_UI_SERVICE_WAKEUP_H
#define BRICK6_UI_SERVICE_WAKEUP_H

#include <stdint.h>

/* Doorbells only. UI event and display state remain authoritative. */
#define UI_SERVICE_WAKE_INPUT (1UL << 0)
#define UI_SERVICE_WAKE_OLED  (1UL << 1)

void ui_service_wakeup(uint32_t flags);

#endif /* BRICK6_UI_SERVICE_WAKEUP_H */
