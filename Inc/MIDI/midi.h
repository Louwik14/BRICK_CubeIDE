/**
 * @file midi.h
 * @brief Interface du module MIDI pour STM32 HAL (USB Device + backends futurs).
 *
 * Fournit une API unifiée pour l'envoi de messages MIDI sur différents transports :
 * - USB Device composite TinyUSB
 * - USB Host (stub pour l'instant)
 * - DIN UART (stub pour l'instant)
 */

#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Seq/seq_types.h"

/* ====================================================================== */
/*                        CONFIGURATION GLOBALE                           */
/* ====================================================================== */

/**
 * @brief Comportement en cas de débordement de la file d'attente USB MIDI.
 *
 * Si défini à 1, le message le plus ancien est supprimé pour insérer le nouveau.
 * Sinon, le nouveau message est perdu.
 */
#ifndef MIDI_MB_DROP_OLDEST
#define MIDI_MB_DROP_OLDEST  0
#endif

/**
 * @brief Numéro de câble USB MIDI (0 pour interface unique).
 */
#ifndef MIDI_USB_CABLE
#define MIDI_USB_CABLE  0u
#endif

/* ====================================================================== */
/*                              TYPES ET STRUCTURES                       */
/* ====================================================================== */

typedef enum {
  MIDI_DEST_NONE = 0,  /**< Aucune sortie */
  MIDI_DEST_UART,      /**< Envoi uniquement via port UART DIN */
  MIDI_DEST_USB,       /**< Envoi uniquement via USB MIDI */
  MIDI_DEST_BOTH       /**< Envoi sur les deux sorties */
} midi_dest_t;

#define MIDI_ADMISSION_UART 0x01U
#define MIDI_ADMISSION_USB  0x02U
#define MIDI_NOTE_OFF_RESERVE 16U

typedef enum {
  MIDI_CLOCK_MODE_SLAVE = 0,
  MIDI_CLOCK_MODE_MASTER
} midi_clock_mode_t;

typedef struct {
  uint8_t data[3];
  uint8_t len;
  uint32_t tim5_tick;
  uint32_t ingress_serial;
} midi_msg_t;

/* ====================================================================== */
/*                              INITIALISATION                            */
/* ====================================================================== */

/**
 * @brief Initialise le module MIDI.
 */
void midi_init(void);

/**
 * @brief Indique si le module MIDI a déjà été initialisé.
 */
bool midi_is_initialized(void);

/**
 * @brief Configure la destination des messages MIDI entrants (USB → moteur/DIN).
 */
void midi_set_rx_destination(midi_dest_t dest);

/** @brief Retourne la destination de routage des messages MIDI entrants. */
midi_dest_t midi_get_rx_destination(void);

/**
 * @brief Traitement périodique (à appeler dans la boucle principale).
 *
 * - Vide la file RX USB (décodage + injection moteur)
 * - Tente d'émettre les messages USB en attente
 */
void midi_poll(void);

/**
 * @brief Envoie un message MIDI brut vers une destination.
 *
 * @param dest Destination(s) de sortie.
 * @param msg Buffer MIDI (1 à 3 octets).
 * @param len Taille du message.
 */
void midi_send_raw(midi_dest_t dest, const uint8_t *msg, size_t len);

/* ====================================================================== */
/*                                 CLOCK                                  */
/* ====================================================================== */

void midi_clock_set_mode(midi_clock_mode_t mode);

midi_clock_mode_t midi_clock_get_mode(void);

void midi_clock_set_bpm_milli(uint32_t bpm_milli);

uint32_t midi_clock_get_bpm_milli(void);

void midi_clock_set_running(bool running);

bool midi_clock_is_running(void);

void midi_clock_set_destination(midi_dest_t dest);

midi_dest_t midi_clock_get_destination(void);

/**
 * @brief Callback à appeler depuis HAL_TIM_PeriodElapsedCallback.
 *
 * // TODO CubeMX: configure TIMx à 24 PPQN
 */
void midi_clock_on_timer_tick(void);

/* ====================================================================== */
/*                        COMMANDES “CHANNEL VOICE”                       */
/* ====================================================================== */

void midi_note_on(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel);
void midi_note_off(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel);
uint8_t midi_note_on_admit(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel);
uint8_t midi_note_off_admit(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t vel);
uint32_t midi_usb_transport_generation(void);
void midi_poly_aftertouch(midi_dest_t dest, uint8_t ch, uint8_t note, uint8_t pressure);
void midi_cc(midi_dest_t dest, uint8_t ch, uint8_t cc, uint8_t val);
void midi_program_change(midi_dest_t dest, uint8_t ch, uint8_t program);
void midi_channel_pressure(midi_dest_t dest, uint8_t ch, uint8_t pressure);
void midi_pitchbend(midi_dest_t dest, uint8_t ch, int16_t value14b);

/* ====================================================================== */
/*                      COMMANDES “SYSTEM COMMON”                         */
/* ====================================================================== */

void midi_mtc_quarter_frame(midi_dest_t dest, uint8_t qf);
void midi_song_position(midi_dest_t dest, uint16_t pos14);
void midi_song_select(midi_dest_t dest, uint8_t song);
void midi_tune_request(midi_dest_t dest);

/* ====================================================================== */
/*                      COMMANDES “SYSTEM REALTIME”                       */
/* ====================================================================== */

void midi_clock(midi_dest_t dest);
void midi_start(midi_dest_t dest);
void midi_continue(midi_dest_t dest);
void midi_stop(midi_dest_t dest);
void midi_active_sensing(midi_dest_t dest);
void midi_system_reset(midi_dest_t dest);

/* ====================================================================== */
/*                       MESSAGES DE MODE DE CANAL                        */
/* ====================================================================== */

void midi_all_sound_off(midi_dest_t dest, uint8_t ch);
void midi_reset_all_controllers(midi_dest_t dest, uint8_t ch);
void midi_local_control(midi_dest_t dest, uint8_t ch, bool on);
void midi_all_notes_off(midi_dest_t dest, uint8_t ch);
void midi_omni_mode_off(midi_dest_t dest, uint8_t ch);
void midi_omni_mode_on(midi_dest_t dest, uint8_t ch);
void midi_mono_mode_on(midi_dest_t dest, uint8_t ch, uint8_t num_channels);
void midi_poly_mode_on(midi_dest_t dest, uint8_t ch);

/* ====================================================================== */
/*                              OUTILS                                    */
/* ====================================================================== */


uint16_t midi_usb_queue_high_watermark(void);
uint32_t midi_usb_tx_drop_count(void);
uint16_t midi_usb_rx_high_watermark(void);


/**
 * @brief Callback faible injectant un message MIDI dans le moteur interne.
 */
void midi_internal_receive(const uint8_t *msg, size_t len);
void midi_internal_receive_with_source(const uint8_t *msg, size_t len, seq_clock_src_t source);
void midi_internal_receive_with_timestamp(const uint8_t *msg, size_t len,
                                          seq_clock_src_t source,
                                          uint32_t tim5_tick,
                                          uint32_t ingress_serial);

/**
 * @brief Alimente la file RX USB (appel depuis l'ISR USB OUT).
 */
void midi_usb_rx_submit_from_isr(const uint8_t *packet, size_t len);
uint16_t midi_usb_rx_free_packets(void);
void midi_rx_discard_pending(void);

#endif /* MIDI_H */
