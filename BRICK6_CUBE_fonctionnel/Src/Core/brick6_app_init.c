/**
 * @file brick6_app_init.c
 * @brief Initialisation applicative BRICK6 (hors CubeMX).
 *
 * Rôle du module:
 * - Centraliser l'ordre d'init des briques applicatives (codec, audio, engine).
 * - Garder main.c minimal et lisible.
 *
 * Architecture:
 * - Appelé par: main.c (USER CODE BEGIN 2).
 * - Appelle: CS42448_Init, mixer_init, audio_*(), engine_tasklet_init.
 *
 * Contraintes temps réel:
 * - Exécuté une seule fois au démarrage (hors IRQ audio).
 * - Appels HAL bloquants autorisés ici (jamais dans le chemin IRQ audio).
 *
 * Pourquoi l'ordre d'init est important:
 * 1) Init codec d'abord (interface audio prête côté conversion).
 * 2) Init états audio_float/tracks/gains avant démarrage DMA.
 * 3) Enregistrer le callback DSP avant audio_start().
 * 4) Démarrer le DMA en dernier pour éviter tout traitement sans état valide.
 */

#include <string.h>
#include "brick6_app_init.h"

#include "engine_tasklet.h"
#include "midi.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"
#include "usb_device.h"
#include "audio.h"
#include "audio_float.h"
#include "cs42448.h"
#include "mixer.h"
#include "fx_pool.h"
#include "param_store.h"
#include "control_events.h"

/* ============================================================
   Audio callback (DSP engine entry point)
   ============================================================ */

/**
 * @brief Entrée DSP principale par bloc audio.
 *
 * @param tracks Tableau de tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio (via audio_process_block_int32).
 *
 * Effets de bord:
 * - Appelle le module mixer (actuellement routage-only).
 */
static void my_dsp(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames)
{
    /* Première étape moteur: mixer/routage. */
    mixer_process(tracks, track_count, frames);

    /* Extensions prévues:
       - track engine
       - routing matrix
       - Mutable FX
       - sampler playback
    */
}

/* ============================================================
   Application Init
   ============================================================ */

/**
 * @brief Initialise la pile applicative BRICK6 dans l'ordre sûr.
 *
 * Séquence actuelle:
 * - Init codec CS42448.
 * - Init mixer + gain staging audio_float.
 * - Init état tracks (disable + clear + gains par défaut).
 * - Paramétrage tracks/gains initiaux.
 * - Init interface audio SAI/DMA.
 * - Enregistrement callback DSP.
 * - Init scheduler tasklet.
 * - Start DMA audio.
 *
 * Contexte d'appel:
 * - Main loop, phase boot.
 */
void brick6_app_init(void)
{
    SDRAM_Init();
    //SDRAM_Test();
    MX_USB_DEVICE_Init();
    MX_USB_HOST_Init();

    /* 1) Codec audio externe. */
    CS42448_Init(0x48);

    /* 2) Init mixer (routing) + gains frontière float. */
    mixer_init();
    fx_pool_init();
    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    /* 3) État tracks déterministe avant démarrage audio. */
    audio_tracks_init();

    /* 4) Configuration initiale de gains et activation tracks. */
    mixer_set_master(2.0f);

    /* Mapping tracks: T0=0/1, T1=2/3, T2=4/5. */
    track_enable(0, 1U);
    track_enable(1, 1U);
    track_enable(2, 1U);

    track_set_gain(0, 1.0f);
    track_set_gain(1, 1.0f);
    track_set_gain(2, 1.0f);

    /* 5) Init périphériques audio, puis callback DSP, puis start DMA. */
    audio_init(&hsai_BlockA1, &hsai_BlockB1);
    audio_set_float_callback(my_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    control_event_init();
    audio_start();

    HAL_Delay(200);

    /* Init MIDI */
    midi_init();
}
