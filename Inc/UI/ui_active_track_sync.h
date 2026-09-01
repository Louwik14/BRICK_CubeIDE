#ifndef UI_ACTIVE_TRACK_SYNC_H
#define UI_ACTIVE_TRACK_SYNC_H

#include <stdint.h>

void ui_active_track_sync_full_after_reconfigure(void);
void ui_active_track_sync_after_track_structure_change(uint8_t sync_active_track_ui_context);
void ui_active_track_sync_after_track_creation_from_off(uint8_t sync_active_track_ui_context);
void ui_active_track_sync_full_after_global_restore(void);

#endif /* UI_ACTIVE_TRACK_SYNC_H */
