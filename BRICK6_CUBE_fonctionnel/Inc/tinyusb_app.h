#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// LIFECYCLE
//--------------------------------------------------------------------+

/**
 * @brief Initialise le backend TinyUSB audio.
 *        À appeler une seule fois au boot.
 */
void tinyusb_app_init(void);

/**
 * @brief Task USB audio (TX 1 ms).
 *        À appeler régulièrement depuis la boucle principale.
 */
void tinyusb_app_task(void);

//--------------------------------------------------------------------+
// HID AUDIO DEBUG (OPTIONNEL)
//--------------------------------------------------------------------+

/**
 * @brief Envoie un rapport HID de debug audio (FIFO, sample rate, etc).
 *        À appeler périodiquement depuis le main (ex: 1–10 ms).
 */
void tinyusb_app_audio_debug_task(void);

//--------------------------------------------------------------------+
// DIAGNOSTICS RX USB
//--------------------------------------------------------------------+

uint32_t tinyusb_app_get_rx_done_count(void);
uint32_t tinyusb_app_get_rx_bytes_total(void);
uint32_t tinyusb_app_get_rx_samples_total(void);
uint32_t tinyusb_app_get_rx_zero_reads(void);

#ifdef __cplusplus
}
#endif
