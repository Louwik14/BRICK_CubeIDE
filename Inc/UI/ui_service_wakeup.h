#ifndef BRICK6_UI_SERVICE_WAKEUP_H
#define BRICK6_UI_SERVICE_WAKEUP_H

#include <stdint.h>

/* Doorbells only. UI event and display state remain authoritative. */
#define UI_SERVICE_WAKE_INPUT (1UL << 0)
#define UI_SERVICE_WAKE_OLED  (1UL << 1)
#define UI_SERVICE_WAKE_DIRTY (1UL << 2)
#define UI_SERVICE_WAKE_LED   (1UL << 3)

void ui_service_wakeup(uint32_t flags);
void ui_service_dirty_set(void);
uint8_t ui_service_dirty_take(void);
uint8_t ui_service_dirty_is_set(void);
void ui_service_led_dirty_set(void);
uint8_t ui_service_led_dirty_take(void);
uint8_t ui_service_led_dirty_is_set(void);
void ui_service_asset_receipts_invalidate(void);
uint8_t ui_service_asset_receipts_invalidation_take(void);
uint8_t ui_service_asset_receipts_invalidation_is_pending(void);
void ui_service_project_progress_notify(void);
uint8_t ui_service_project_progress_take(void);
uint8_t ui_service_project_progress_is_pending(void);
void ui_service_settings_progress_notify(void);
uint8_t ui_service_settings_progress_take(void);
uint8_t ui_service_settings_progress_is_pending(void);
void ui_service_audio_rec_data_notify(void);
uint8_t ui_service_audio_rec_data_take(void);
uint8_t ui_service_audio_rec_data_is_pending(void);

#endif /* BRICK6_UI_SERVICE_WAKEUP_H */
