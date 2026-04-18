#include "ui_system_sync_internal.h"

static uint8_t ui_system_sync_request_is_valid_against_adapter(const ui_system_sync_request_t *request,
                                                               const ui_system_sync_adapter_t *adapter)
{
    if ((request == 0) || (adapter == 0))
    {
        return 0U;
    }

    if ((request->notify_keyboard_after_runtime_sync != 0U)
        && (adapter->notify_keyboard_active_track_changed == 0))
    {
        return 0U;
    }

    if ((request->invalidate_runtime != 0U)
        && (adapter->invalidate_runtime_all == 0))
    {
        return 0U;
    }

    if ((request->sync_audio_enables != 0U)
        && (adapter->sync_audio_runtime_enables == 0))
    {
        return 0U;
    }

    return 1U;
}

ui_system_sync_request_t ui_system_sync_make_request_restore_bulk(void)
{
    ui_system_sync_request_t request = { 0 };
    request.invalidate_runtime = 1U;
    request.sync_audio_enables = 1U;
    request.runtime_sync_order = UI_SYSTEM_SYNC_RUNTIME_ORDER_INVALIDATE_THEN_ENABLES;
    request.notify_keyboard_after_runtime_sync = 1U;
    return request;
}

ui_system_sync_request_t ui_system_sync_make_request_track_family_change(uint8_t active_track_touched)
{
    ui_system_sync_request_t request = { 0 };
    request.invalidate_runtime = 1U;
    request.sync_audio_enables = 1U;
    request.runtime_sync_order = UI_SYSTEM_SYNC_RUNTIME_ORDER_ENABLES_THEN_INVALIDATE;
    request.notify_keyboard_after_runtime_sync = active_track_touched;
    return request;
}

ui_system_sync_request_t ui_system_sync_make_request_track_type_change(uint8_t active_track_touched)
{
    ui_system_sync_request_t request = { 0 };
    request.invalidate_runtime = 1U;
    request.runtime_sync_order = UI_SYSTEM_SYNC_RUNTIME_ORDER_INVALIDATE_THEN_ENABLES;
    request.notify_keyboard_after_runtime_sync = active_track_touched;
    return request;
}

void ui_system_sync_apply_track_context_change(const ui_system_sync_request_t *request,
                                               const ui_system_sync_adapter_t *adapter)
{
    if (ui_system_sync_request_is_valid_against_adapter(request, adapter) == 0U)
    {
        return;
    }

    if ((request->invalidate_runtime != 0U) || (request->sync_audio_enables != 0U))
    {
        if (request->runtime_sync_order == UI_SYSTEM_SYNC_RUNTIME_ORDER_ENABLES_THEN_INVALIDATE)
        {
            if ((request->sync_audio_enables != 0U)
                && (adapter->sync_audio_runtime_enables != 0))
            {
                adapter->sync_audio_runtime_enables();
            }

            if ((request->invalidate_runtime != 0U)
                && (adapter->invalidate_runtime_all != 0))
            {
                adapter->invalidate_runtime_all();
            }
        }
        else
        {
            if ((request->invalidate_runtime != 0U)
                && (adapter->invalidate_runtime_all != 0))
            {
                adapter->invalidate_runtime_all();
            }

            if ((request->sync_audio_enables != 0U)
                && (adapter->sync_audio_runtime_enables != 0))
            {
                adapter->sync_audio_runtime_enables();
            }
        }
    }

    if ((request->notify_keyboard_after_runtime_sync != 0U)
        && (adapter->notify_keyboard_active_track_changed != 0))
    {
        adapter->notify_keyboard_active_track_changed();
    }

}
