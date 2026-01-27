/**
 * @file midi.c
 * @brief Implémentation du module MIDI (UART + USB) pour ChibiOS.
 *
 * Ce module fournit l’envoi de messages MIDI vers deux destinations :
 * - **DIN UART** (31250 bauds) via `SD2`,
 * - **USB MIDI** (class compliant) via l’endpoint IN (EP2).
 *
 * Principes d’implémentation :
 * - Un **thread dédié** agrège les paquets USB-MIDI en trames de 64 octets (EP IN bulk)
 *   à partir d’une **mailbox** non bloquante.
 * - Les messages “Realtime” (F8, FA/FB/FC/FE/FF) bénéficient d’un **chemin rapide**
 *   avec micro-attente pour l’envoi immédiat si l’endpoint est libre.
 * - Les statistiques d’envoi sont tenues dans `midi_tx_stats` pour le diagnostic.
 *
 * Contraintes temps réel :
 * - Le thread de TX USB doit avoir une priorité **au moins égale ou supérieure à l’UI**.
 * - Les callbacks d’USB doivent rester **courts** (signalement de sémaphore/flags uniquement).
 * - Aucun appel bloquant en ISR ; pas d’allocations dynamiques à l’exécution.
 *
 * @note L’API publique est déclarée dans `midi.h`.
 * @ingroup drivers
 */

#include "ch.h"
#include "hal.h"
#include "brick_config.h"
#include "midi.h"
#include "usbcfg.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Indicateur d’état de l’USB MIDI.
 * @details Positionné à `true` lorsque l’USB est configuré et l’EP prêt.
 *          Fourni par `usbcfg.c` (déclaré ici en `extern`).
 */
extern volatile bool usb_midi_tx_ready;

static bool midi_initialized = false;

static inline bool midi_usb_ready(void) {
  bool ready;
  osalSysLock();
  ready = usb_midi_tx_ready;
  osalSysUnlock();
  return ready;
}

/* Nettoyage/invalidations D-Cache pour les buffers USB (OTG FS accède au D2). */
#if defined(STM32H7xx) && defined(STM32_DCACHE_ENABLED)
static inline void midi_usb_flush_tx_cache(const void *addr, size_t len) {
  const size_t cache_line = 32U;
  uintptr_t start = (uintptr_t)addr & ~(cache_line - 1U);
  uintptr_t end = (uintptr_t)addr + len;
  SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}
#else
static inline void midi_usb_flush_tx_cache(const void *addr, size_t len) {
  (void)addr; (void)len;
}
#endif

/* Callback faible pour injection dans le moteur MIDI interne. */
__attribute__((weak)) void midi_internal_receive(const uint8_t *msg, size_t len) {
  (void)msg; (void)len;
}

/* ====================================================================== */
/*                         CONFIGURATION / ÉTAT                            */
/* ====================================================================== */

/**
 * @brief Priorité du thread de transmission USB-MIDI.
 * @details Doit être ≥ priorité UI pour garantir une latence stable des I/O.
 */
#ifndef MIDI_USB_TX_PRIO
#define MIDI_USB_TX_PRIO   (NORMALPRIO + 1)
#endif

/**
 * @brief Timeout d’attente du sémaphore TX en thread (ms).
 * @details Évite les blocages si l’hôte USB ne réarme pas l’endpoint.
 */
#ifndef MIDI_USB_TX_WAIT_MS
#define MIDI_USB_TX_WAIT_MS  2
#endif

/**
 * @brief Périphérique série utilisé pour la sortie DIN MIDI.
 * @details Déplacé dans brick_config.h pour faciliter le portage carte.
 */
#define MIDI_UART   BRICK_MIDI_UART

/**
 * @brief Taille de la file (mailbox) de messages USB-MIDI (en éléments de 32 bits).
 * @details Chaque élément correspond à un paquet USB-MIDI de 4 octets packé dans un `msg_t`.
 */
#define MIDI_USB_QUEUE_LEN   256

/** @brief Mailbox de transmission USB-MIDI (producteur/consommateur). */
static mailbox_t midi_usb_mb;
/** @brief Buffer circulaire pour la mailbox USB-MIDI. */
static msg_t     midi_usb_queue[MIDI_USB_QUEUE_LEN];
static uint16_t  midi_usb_queue_fill = 0;
static uint16_t  midi_usb_queue_high_water = 0;

/**
 * @brief Taille de la file (mailbox) de réception USB-MIDI (paquets 4 octets).
 */
#define MIDI_USB_RX_QUEUE_LEN 128

/** @brief Mailbox de réception brute USB-MIDI (alimentée en ISR). */
static mailbox_t midi_usb_rx_mb;
/** @brief Buffer circulaire pour la mailbox de réception. */
static msg_t     midi_usb_rx_queue[MIDI_USB_RX_QUEUE_LEN];
static uint16_t midi_usb_rx_queue_fill = 0;
static uint16_t midi_usb_rx_queue_high_water = 0;
volatile uint32_t midi_usb_rx_drops = 0;

static inline void midi_usb_queue_increment(void) {
  osalSysLock();
  if (midi_usb_queue_fill < MIDI_USB_QUEUE_LEN) {
    midi_usb_queue_fill++;
    if (midi_usb_queue_fill > midi_usb_queue_high_water) {
      midi_usb_queue_high_water = midi_usb_queue_fill;
    }
  }
  osalSysUnlock();
}

static inline void midi_usb_queue_decrement(void) {
  osalSysLock();
  if (midi_usb_queue_fill > 0U) {
    midi_usb_queue_fill--;
  }
  osalSysUnlock();
}

static inline void midi_usb_rx_queue_increment_i(void) {
  /* Les compteurs RX sont manipulés sous verrou système (ISR ou lock explicite). */
  if (midi_usb_rx_queue_fill < MIDI_USB_RX_QUEUE_LEN) {
    midi_usb_rx_queue_fill++;
    if (midi_usb_rx_queue_fill > midi_usb_rx_queue_high_water) {
      midi_usb_rx_queue_high_water = midi_usb_rx_queue_fill;
    }
  }
}

static inline void midi_usb_rx_queue_decrement(void) {
  /* Les compteurs RX sont manipulés sous verrou système (ISR ou lock explicite). */
  if (midi_usb_rx_queue_fill > 0U) {
    midi_usb_rx_queue_fill--;
  }
}

static inline void midi_usb_start_tx(const uint8_t *buffer, size_t len) {
  midi_usb_flush_tx_cache(buffer, len);
  osalSysLock();
  usbStartTransmitI(&USBD1, MIDI_EP_IN, buffer, len);
  osalSysUnlock();
}

void midi_usb_rx_submit_from_isr(const uint8_t *packet, size_t len) {
  if ((packet == NULL) || (len < 4U)) {
    return;
  }

  const size_t packets = len / 4U;

  osalSysLockFromISR();
  if (midi_usb_rx_mb.buffer == NULL) {
    osalSysUnlockFromISR();
    return;
  }

  for (size_t i = 0; i < packets; i++) {
    if (midi_usb_rx_queue_fill >= MIDI_USB_RX_QUEUE_LEN) {
      midi_usb_rx_drops++;
      midi_rx_stats.usb_rx_drops++;
    } else {
      msg_t m = ((msg_t)packet[0] << 24) |
                ((msg_t)packet[1] << 16) |
                ((msg_t)packet[2] << 8)  |
                ((msg_t)packet[3]);

      if (chMBPostI(&midi_usb_rx_mb, m) == MSG_OK) {
        midi_usb_rx_queue_increment_i();
        midi_rx_stats.usb_rx_enqueued++;
      } else {
        midi_usb_rx_drops++;
        midi_rx_stats.usb_rx_drops++;
      }
    }
    packet += 4U;
  }
  osalSysUnlockFromISR();
}

/**
 * @brief Sémaphore “endpoint libre”.
 * @details Signalé dans le callback d’EP IN (ex. `ep2_in_cb()` côté `usbcfg.c`).
 */
binary_semaphore_t tx_sem;
binary_semaphore_t sof_sem;

/** @brief Statistiques globales de transmission MIDI (USB/DIN). */
midi_tx_stats_t midi_tx_stats = {0};

/** @brief Statistiques globales de réception MIDI. */
midi_rx_stats_t midi_rx_stats = {0};

/** @brief Destination actuelle pour le routage des messages entrants. */
static midi_dest_t midi_rx_dest = MIDI_DEST_BOTH;

/* ====================================================================== */
/*                        VÉRIFICATIONS DE CONFIG EP                      */
/* ====================================================================== */

#if MIDI_EP_IN != 2
#error "MIDI_EP_IN doit valoir 2 (EP2 IN)."
#endif
#if MIDI_EP_OUT != 1
#error "MIDI_EP_OUT doit valoir 1 (EP1 OUT)."
#endif
#if MIDI_EP_SIZE != 64
#error "MIDI_EP_SIZE doit valoir 64."
#endif

static void midi_process_usb_rx(void);

/* ====================================================================== */
/*                        THREAD DE TRANSMISSION USB                      */
/* ====================================================================== */

/**
 * @brief Zone de travail du thread de transmission USB-MIDI.
 */
static THD_WORKING_AREA(waMidiUsbTx, 512);

/**
 * @brief Thread d’agrégation et d’envoi USB-MIDI.
 *
 * Récupère des paquets USB-MIDI (4 octets packés dans un `msg_t`) depuis
 * la mailbox @ref midi_usb_mb, les agrège dans un buffer de 64 octets et
 * déclenche `usbStartTransmitI()` sur EP IN (EP2).
 *
 * Politique de robustesse :
 * - Si l’USB n’est pas prêt, les paquets sont comptabilisés en
 *   @ref midi_tx_stats.usb_not_ready_drops.
 * - Si le sémaphore n’est pas obtenu dans @ref MIDI_USB_TX_WAIT_MS ms,
 *   le lot courant est **abandonné** (drop contrôlé) pour éviter tout blocage.
 *
 * @param arg Argument inutilisé.
 */
static THD_FUNCTION(thdMidiUsbTx, arg) {
  (void)arg;
#if CH_CFG_USE_REGISTRY
  chRegSetThreadName("MIDI_USB_TX");
#endif
  uint8_t buf[64];
  size_t n = 0;

  while (true) {
    midi_process_usb_rx();

    msg_t msg;
    msg_t res = chMBFetchTimeout(&midi_usb_mb, &msg, TIME_MS2I(1));

    if (res == MSG_OK) {
      midi_usb_queue_decrement();
      buf[n++] = (uint8_t)((msg >> 24) & 0xFF);
      buf[n++] = (uint8_t)((msg >> 16) & 0xFF);
      buf[n++] = (uint8_t)((msg >> 8)  & 0xFF);
      buf[n++] = (uint8_t)( msg        & 0xFF);

      if (n == sizeof(buf)) {
        const bool ready = midi_usb_ready();
        if (ready) {
          const systime_t tw = TIME_MS2I(MIDI_USB_TX_WAIT_MS);
          if (chBSemWaitTimeout(&tx_sem, tw) == MSG_OK) {
            midi_usb_start_tx(buf, n);
            midi_tx_stats.tx_sent_batched++;
          } else {
            /* Endpoint non réarmé à temps : abandon contrôlé du lot. */
            midi_tx_stats.usb_not_ready_drops += n / 4;
          }
        } else {
          midi_tx_stats.usb_not_ready_drops += n / 4;
        }
        n = 0;
      }
    } else if (n > 0U) {
      /* Flush du lot partiel déclenché sur le prochain SOF ou après timeout. */
      chBSemWaitTimeout(&sof_sem, TIME_MS2I(1));
      const bool ready = midi_usb_ready();
      if (ready) {
        const systime_t tw = TIME_MS2I(MIDI_USB_TX_WAIT_MS);
        if (chBSemWaitTimeout(&tx_sem, tw) == MSG_OK) {
          midi_usb_start_tx(buf, n);
          midi_tx_stats.tx_sent_batched++;
        } else {
          midi_tx_stats.usb_not_ready_drops += n / 4;
        }
      } else {
        midi_tx_stats.usb_not_ready_drops += n / 4;
      }
      n = 0;
    }
  }
}

/* ====================================================================== */
/*                          INITIALISATION DU MODULE                      */
/* ====================================================================== */

/**
 * @brief Initialise le sous-système MIDI.
 *
 * - Configure l’UART DIN à 31250 bauds,
 * - Initialise la mailbox et son buffer circulaire,
 * - Initialise le sémaphore d’EP libre,
 * - Démarre le thread de transmission USB-MIDI.
 */
void midi_init(void) {
  osalSysLock();
  if (midi_initialized) {
    osalSysUnlock();
    return;
  }
  midi_initialized = true;
  osalSysUnlock();

  static const SerialConfig uart_cfg = { 31250, 0, 0, 0 };
  sdStart(MIDI_UART, &uart_cfg);
  midi_usb_queue_fill = 0;
  midi_usb_queue_high_water = 0;
  midi_usb_rx_queue_fill = 0;
  midi_usb_rx_queue_high_water = 0;
  midi_usb_rx_drops = 0;
  chMBObjectInit(&midi_usb_mb, midi_usb_queue, MIDI_USB_QUEUE_LEN);
  chMBObjectInit(&midi_usb_rx_mb, midi_usb_rx_queue, MIDI_USB_RX_QUEUE_LEN);
  chBSemObjectInit(&tx_sem, true);
  chBSemObjectInit(&sof_sem, true);
  chThdCreateStatic(waMidiUsbTx, sizeof(waMidiUsbTx),
                    MIDI_USB_TX_PRIO, thdMidiUsbTx, NULL);
}

bool midi_is_initialized(void) {
  bool initialized;

  osalSysLock();
  initialized = midi_initialized;
  osalSysUnlock();

  return initialized;
}

/* ====================================================================== */
/*                           FONCTIONS BAS NIVEAU                         */
/* ====================================================================== */

/**
 * @brief Envoie un message brut sur la sortie DIN (UART).
 * @param msg Pointeur sur les octets du message MIDI.
 * @param len Longueur en octets du message.
 */
static void send_uart(const uint8_t *msg, size_t len) { sdWrite(MIDI_UART, msg, len); }

/**
 * @brief Poste un paquet USB-MIDI (4 octets packés) dans la mailbox, sinon le supprime.
 *
 * @param m Paquet USB-MIDI encodé dans un `msg_t` (octet 0 dans bits 31..24).
 * @param force_drop_oldest Si vrai, retire le plus ancien élément de la mailbox
 *                          pour insérer le nouveau (politique “drop-oldest”).
 *                          Sinon, le paquet courant est perdu si la file est pleine.
 */
static void post_mb_or_drop(msg_t m, bool force_drop_oldest) {
  if (chMBPostTimeout(&midi_usb_mb, m, TIME_IMMEDIATE) != MSG_OK) {
    if (force_drop_oldest || MIDI_MB_DROP_OLDEST) {
      msg_t throwaway;
      if (chMBFetchTimeout(&midi_usb_mb, &throwaway, TIME_IMMEDIATE) == MSG_OK) {
        midi_usb_queue_decrement();
      }
      if (chMBPostTimeout(&midi_usb_mb, m, TIME_IMMEDIATE) != MSG_OK)
        midi_tx_stats.tx_mb_drops++;
      else
        midi_usb_queue_increment();
    } else {
      midi_tx_stats.tx_mb_drops++;
    }
  } else {
    midi_usb_queue_increment();
  }
}

/* ====================================================================== */
/*                        RÉCEPTION USB (DÉCODAGE)                        */
/* ====================================================================== */

static bool usb_midi_decode_packet(const uint8_t pkt[4], midi_msg_t *out) {
  if (out == NULL) {
    return false;
  }

  const uint8_t cin = (uint8_t)(pkt[0] & 0x0F);

  switch (cin) {
    case 0x08: /* Note Off */
    case 0x09: /* Note On */
    case 0x0A: /* Poly Aftertouch */
    case 0x0B: /* Control Change */
    case 0x0E: /* Pitch Bend */
      out->len = 3U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      out->data[2] = pkt[3];
      return true;

    case 0x0C: /* Program Change */
    case 0x0D: /* Channel Pressure */
      out->len = 2U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      return true;

    case 0x02: /* System Common 2 bytes (MTC Quarter Frame / Song Select) */
      out->len = 2U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      return true;

    case 0x03: /* System Common 3 bytes (Song Position Pointer) */
      out->len = 3U;
      out->data[0] = pkt[1];
      out->data[1] = pkt[2];
      out->data[2] = pkt[3];
      return true;

    case 0x0F: /* Single-byte real-time */
      switch (pkt[1]) {
        case 0xF8: /* Clock */
        case 0xFA: /* Start */
        case 0xFB: /* Continue */
        case 0xFC: /* Stop */
        case 0xFE: /* Active Sensing */
        case 0xFF: /* System Reset */
          out->len = 1U;
          out->data[0] = pkt[1];
          return true;
        default:
          return false;
      }
      break;

    default:
      break;
  }

  return false;
}

static void midi_dispatch_rx_message(const midi_msg_t *msg) {
  midi_internal_receive(msg->data, msg->len);

  if ((midi_rx_dest == MIDI_DEST_UART) || (midi_rx_dest == MIDI_DEST_BOTH)) {
    send_uart(msg->data, msg->len);
  }
}

static void midi_process_usb_rx(void) {
  const uint32_t max_burst = 16U;
  uint32_t processed = 0U;
  msg_t raw;

  while ((processed < max_burst) &&
         (chMBFetchTimeout(&midi_usb_rx_mb, &raw, TIME_IMMEDIATE) == MSG_OK)) {
    osalSysLock();
    midi_usb_rx_queue_decrement();
    osalSysUnlock();

    uint8_t pkt[4];
    pkt[0] = (uint8_t)((raw >> 24) & 0xFF);
    pkt[1] = (uint8_t)((raw >> 16) & 0xFF);
    pkt[2] = (uint8_t)((raw >> 8)  & 0xFF);
    pkt[3] = (uint8_t)( raw        & 0xFF);

    midi_msg_t msg;
    if (usb_midi_decode_packet(pkt, &msg)) {
      midi_dispatch_rx_message(&msg);
      midi_rx_stats.usb_rx_decoded++;
    } else {
      midi_rx_stats.usb_rx_ignored++;
    }

    processed++;
  }
}

/* ====================================================================== */
/*                       TRANSMISSION USB (PROTOCOLE)                     */
/* ====================================================================== */

/**
 * @brief Construit un paquet USB-MIDI (4 octets) et déclenche son envoi.
 *
 * Règles principales :
 * - Mappage correct du **CIN** selon le type de message (NoteOn=0x9, CC=0xB, Realtime=0xF, etc.),
 * - Zéro-padding pour les messages courts (1 ou 2 octets),
 * - Priorité aux **Realtime** :
 *   - `0xF8` (Clock) : tentative immédiate, sinon file pour flush au prochain SOF,
 *   - `FA/FB/FC/FE/FF` : idem.
 * - Pour les **Notes** : tentative immédiate (sans attente active), sinon agrégation.
 *
 * @param msg Pointeur vers le message MIDI (status + data).
 * @param len Taille du message en octets (1 à 3 selon le type).
 */
static void send_usb(const uint8_t *msg, size_t len) {
  uint8_t packet[4]={0,0,0,0};
  const uint8_t st = msg[0];
  const uint8_t cable = (uint8_t)(MIDI_USB_CABLE<<4);

  bool is_note=false;

  /* Channel Voice */
  if ((st & 0xF0)==0x80 && len>=3){ packet[0]=cable|0x08; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=msg[2]; is_note=true; }
  else if ((st & 0xF0)==0x90 && len>=3){ packet[0]=cable|0x09; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=msg[2]; is_note=true; }
  else if ((st & 0xF0)==0xA0 && len>=3){ packet[0]=cable|0x0A; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=msg[2]; }
  else if ((st & 0xF0)==0xB0 && len>=3){ packet[0]=cable|0x0B; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=msg[2]; }
  else if ((st & 0xF0)==0xE0 && len>=3){ packet[0]=cable|0x0E; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=msg[2]; }
  else if ((st & 0xF0)==0xC0 && len>=2){ packet[0]=cable|0x0C; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=0; }
  else if ((st & 0xF0)==0xD0 && len>=2){ packet[0]=cable|0x0D; packet[1]=msg[0]; packet[2]=msg[1]; packet[3]=0; }

  /* System Common */
  else if (st==0xF1 && len>=2){ packet[0]=cable|0x02; packet[1]=0xF1; packet[2]=msg[1]; }
  else if (st==0xF2 && len>=3){ packet[0]=cable|0x03; packet[1]=0xF2; packet[2]=msg[1]; packet[3]=msg[2]; }
  else if (st==0xF3 && len>=2){ packet[0]=cable|0x02; packet[1]=0xF3; packet[2]=msg[1]; }
  else if (st==0xF6){          packet[0]=cable|0x0F; packet[1]=0xF6; }

  /* Realtime */
  else if (st>=0xF8){
    packet[0]=cable|0x0F; packet[1]=st;

    if (st==0xF8){
      if (midi_usb_ready() && chBSemWaitTimeout(&tx_sem, TIME_IMMEDIATE)==MSG_OK){
        midi_usb_start_tx(packet, 4);
        midi_tx_stats.tx_sent_immediate++;
      } else {
        msg_t m=((msg_t)packet[0]<<24)|((msg_t)packet[1]<<16)|((msg_t)packet[2]<<8)|packet[3];
        post_mb_or_drop(m,false);
      }
      return;
    }

    if (st==0xFA || st==0xFB || st==0xFC || st==0xFE || st==0xFF){
      if (midi_usb_ready() && chBSemWaitTimeout(&tx_sem, TIME_IMMEDIATE)==MSG_OK){
        midi_usb_start_tx(packet, 4);
        midi_tx_stats.tx_sent_immediate++;
      } else {
        midi_tx_stats.rt_other_enq_fallback++;
        msg_t m=((msg_t)packet[0]<<24)|((msg_t)packet[1]<<16)|((msg_t)packet[2]<<8)|packet[3];
        post_mb_or_drop(m,true);
      }
      return;
    }

    msg_t m3=((msg_t)packet[0]<<24)|((msg_t)packet[1]<<16)|((msg_t)packet[2]<<8)|packet[3];
    post_mb_or_drop(m3,false);
    return;
  }

  else { packet[0]=cable|0x0F; packet[1]=len>0?msg[0]:0; packet[2]=len>1?msg[1]:0; packet[3]=len>2?msg[2]:0; }

  if (is_note){
    if (midi_usb_ready() && chBSemWaitTimeout(&tx_sem, TIME_IMMEDIATE)==MSG_OK){
      midi_usb_start_tx(packet, 4);
      midi_tx_stats.tx_sent_immediate++; return;
    }
  }

  msg_t m=((msg_t)packet[0]<<24)|((msg_t)packet[1]<<16)|((msg_t)packet[2]<<8)|packet[3];
  post_mb_or_drop(m,false);
}

/* ====================================================================== */
/*                              ROUTAGE MIDI                              */
/* ====================================================================== */

/**
 * @brief Envoie un message MIDI vers la destination choisie.
 * @param d Destination d’envoi (UART, USB, ou les deux).
 * @param m Pointeur sur les octets du message MIDI.
 * @param n Longueur du message en octets.
 */
static void midi_send(midi_dest_t d, const uint8_t *m, size_t n){
  switch(d){
    case MIDI_DEST_UART: send_uart(m,n); break;
    case MIDI_DEST_USB:  send_usb(m,n);  break;
    case MIDI_DEST_BOTH: send_uart(m,n); send_usb(m,n); break;
    default: break;
  }
}

void midi_set_rx_destination(midi_dest_t dest) {
  switch (dest) {
    case MIDI_DEST_UART:
    case MIDI_DEST_USB:
    case MIDI_DEST_BOTH:
      midi_rx_dest = dest;
      break;
    default:
      midi_rx_dest = MIDI_DEST_BOTH;
      break;
  }
}

midi_dest_t midi_get_rx_destination(void) {
  return midi_rx_dest;
}

/* ====================================================================== */
/*                                API MIDI                                */
/* ====================================================================== */

void midi_note_on(midi_dest_t d,uint8_t ch,uint8_t n,uint8_t v){
  if ((v & 0x7F)==0){ midi_note_off(d,ch,n,0); return; }
  uint8_t m[3]={ (uint8_t)(0x90|(ch&0x0F)), (uint8_t)(n&0x7F), (uint8_t)(v&0x7F) };
  midi_send(d,m,3);
}

void midi_note_off(midi_dest_t d,uint8_t ch,uint8_t n,uint8_t v){
  uint8_t m[3]={ (uint8_t)(0x80|(ch&0x0F)), (uint8_t)(n&0x7F), (uint8_t)(v&0x7F) };
  midi_send(d,m,3);
}

void midi_poly_aftertouch(midi_dest_t d,uint8_t ch,uint8_t n,uint8_t p){
  uint8_t m[3]={ (uint8_t)(0xA0|(ch&0x0F)), (uint8_t)(n&0x7F), (uint8_t)(p&0x7F) };
  midi_send(d,m,3);
}

void midi_cc(midi_dest_t d,uint8_t ch,uint8_t c,uint8_t v){
  uint8_t m[3]={ (uint8_t)(0xB0|(ch&0x0F)), (uint8_t)(c&0x7F), (uint8_t)(v&0x7F) };
  midi_send(d,m,3);
}

void midi_program_change(midi_dest_t d,uint8_t ch,uint8_t pg){
  uint8_t m[2]={ (uint8_t)(0xC0|(ch&0x0F)), (uint8_t)(pg&0x7F) };
  midi_send(d,m,2);
}

void midi_channel_pressure(midi_dest_t d,uint8_t ch,uint8_t p){
  uint8_t m[2]={ (uint8_t)(0xD0|(ch&0x0F)), (uint8_t)(p&0x7F) };
  midi_send(d,m,2);
}

void midi_pitchbend(midi_dest_t d,uint8_t ch,int16_t v14){
  uint16_t u=(uint16_t)(v14+8192);
  uint8_t l=u&0x7F, m=(u>>7)&0x7F;
  uint8_t msg[3]={ (uint8_t)(0xE0|(ch&0x0F)), l, m };
  midi_send(d,msg,3);
}

void midi_mtc_quarter_frame(midi_dest_t d,uint8_t qf){
  uint8_t m[2]={ 0xF1,(uint8_t)(qf&0x7F) };
  midi_send(d,m,2);
}

void midi_song_position(midi_dest_t d,uint16_t p14){
  uint8_t l=(uint8_t)(p14&0x7F),h=(uint8_t)((p14>>7)&0x7F);
  uint8_t m[3]={0xF2,l,h};
  midi_send(d,m,3);
}

void midi_song_select(midi_dest_t d,uint8_t s){
  uint8_t m[2]={ 0xF3,(uint8_t)(s&0x7F) };
  midi_send(d,m,2);
}

void midi_tune_request(midi_dest_t d){
  uint8_t m[1]={0xF6};
  midi_send(d,m,1);
}

void midi_clock(midi_dest_t d){
  uint8_t m[1]={0xF8};
  midi_send(d,m,1);
}

void midi_start(midi_dest_t d){
  uint8_t m[1]={0xFA};
  midi_send(d,m,1);
}

void midi_continue(midi_dest_t d){
  uint8_t m[1]={0xFB};
  midi_send(d,m,1);
}

void midi_stop(midi_dest_t d){
  uint8_t m[1]={0xFC};
  midi_send(d,m,1);
}

void midi_active_sensing(midi_dest_t d){
  uint8_t m[1]={0xFE};
  midi_send(d,m,1);
}

void midi_system_reset(midi_dest_t d){
  uint8_t m[1]={0xFF};
  midi_send(d,m,1);
}

// --- FIX: centraliser les Channel Mode messages (All Notes Off & cie) dans midi.c ---
static void midi_channel_mode_cc(midi_dest_t dest, uint8_t ch, uint8_t control, uint8_t value) {
  uint8_t msg[3] = {
      (uint8_t)(0xB0 | (ch & 0x0F)),
      (uint8_t)(control & 0x7F),
      (uint8_t)(value & 0x7F)
  };
  midi_send(dest, msg, 3);
}

void midi_all_sound_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 120U, 0U);
}

void midi_reset_all_controllers(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 121U, 0U);
}

void midi_local_control(midi_dest_t dest, uint8_t ch, bool on) {
  midi_channel_mode_cc(dest, ch, 122U, on ? 127U : 0U);
}

void midi_all_notes_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 123U, 0U);
}

void midi_omni_mode_off(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 124U, 0U);
}

void midi_omni_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 125U, 0U);
}

void midi_mono_mode_on(midi_dest_t dest, uint8_t ch, uint8_t num_channels) {
  midi_channel_mode_cc(dest, ch, 126U, num_channels);
}

void midi_poly_mode_on(midi_dest_t dest, uint8_t ch) {
  midi_channel_mode_cc(dest, ch, 127U, 0U);
}

uint16_t midi_usb_queue_high_watermark(void) {
  return midi_usb_queue_high_water;
}

uint16_t midi_usb_rx_high_watermark(void) {
  return midi_usb_rx_queue_high_water;
}

/**
 * @brief Réinitialise les statistiques de transmission MIDI.
 */
void midi_stats_reset(void){
  midi_tx_stats=(midi_tx_stats_t){0};
  midi_rx_stats=(midi_rx_stats_t){0};
  midi_usb_rx_drops = 0;
}
