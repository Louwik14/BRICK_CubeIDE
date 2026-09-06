#ifndef UI_ACTIVE_TRACK_SYNC_H
#define UI_ACTIVE_TRACK_SYNC_H

#include <stdint.h>

void ui_active_track_sync_notify_product_changed(void);
uint8_t ui_active_track_sync_is_pending(void);
void ui_active_track_sync_process_pending(void);

#endif /* UI_ACTIVE_TRACK_SYNC_H */
