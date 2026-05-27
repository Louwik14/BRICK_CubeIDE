#ifndef UI_SYSTEM_SYNC_INTERNAL_H
#define UI_SYSTEM_SYNC_INTERNAL_H

#include <stdint.h>

typedef enum
{
    UI_SYSTEM_SYNC_RUNTIME_ORDER_INVALIDATE_THEN_ENABLES = 0,
    UI_SYSTEM_SYNC_RUNTIME_ORDER_ENABLES_THEN_INVALIDATE
} ui_system_sync_runtime_order_t;

typedef struct
{
    uint8_t invalidate_runtime;
    uint8_t runtime_track;
    uint8_t sync_audio_enables;
    ui_system_sync_runtime_order_t runtime_sync_order;
    uint8_t notify_keyboard_after_runtime_sync;
} ui_system_sync_request_t;

typedef struct
{
    void (*notify_keyboard_active_track_changed)(void);
    void (*invalidate_runtime_all)(void);
    void (*invalidate_runtime_track)(uint8_t track);
    void (*sync_audio_runtime_enables)(void);
} ui_system_sync_adapter_t;

#define UI_SYSTEM_SYNC_RUNTIME_TRACK_ALL 0xFFU

ui_system_sync_request_t ui_system_sync_make_request_restore_bulk(void);
ui_system_sync_request_t ui_system_sync_make_request_track_family_change(uint8_t track, uint8_t active_track_touched);
ui_system_sync_request_t ui_system_sync_make_request_track_type_change(uint8_t track, uint8_t active_track_touched);
void ui_system_sync_apply_track_context_change(const ui_system_sync_request_t *request,
                                               const ui_system_sync_adapter_t *adapter);

/*
 * Internal contract:
 * - request describes which phases are active for the current call.
 * - adapter must provide every callback required by active phases.
 * - missing callbacks are treated as invalid wiring and abort the call.
 */

#endif /* UI_SYSTEM_SYNC_INTERNAL_H */
