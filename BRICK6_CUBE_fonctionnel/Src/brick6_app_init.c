/**
 * @file brick6_app_init.c
 * @brief Initialisation applicative BRICK6 (hors CubeMX).
 *
 * Ce module regroupe l'initialisation des sous-systèmes applicatifs
 * (SDRAM, SD, USB, audio) afin de garder main.c minimal.
 *
 * Rôle dans le système:
 * - Point d'entrée applicatif après l'init CubeMX.
 * - Séquenceur d'initialisation des modules utilisateurs.
 *
 * Contraintes temps réel:
 * - Critique audio: non (exécuté une seule fois au démarrage).
 * - Tasklet: non.
 * - IRQ: non.
 * - Borné: non critique (peut appeler HAL bloquant).
 *
 * Architecture:
 * - Appelé par: main.c (USER CODE BEGIN 2).
 * - Appelle: SDRAM_Init/Test, sd_stream_init, MX_USB_*,
 *            AudioIn/Out_Init/Start, engine_tasklet_init.
 *
 * Règles:
 * - Pas de logique temps réel.
 * - Autorisé à utiliser des appels HAL bloquants d'init.
 *
 * @note L’API publique est déclarée dans brick6_app_init.h.
 */

#include <string.h>
#include "brick6_app_init.h"

#include "engine_tasklet.h"
//#include "midi.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
//#include "usb_host.h"
#include "audio.h"
#include "audio_float.h"
#include "cs42448.h"
#include "mixer.h"


/* ============================================================
   Audio callback (DSP engine entry point)
   ============================================================ */

static void my_dsp(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames)
{
    /* ============================================================
       First real engine stage : mixer core
       ============================================================ */

    mixer_process(tracks, track_count, frames);

    /* Later here:
       - track engine
       - routing matrix
       - Mutable FX
       - sampler playback
    */
}


/* ============================================================
   Application Init
   ============================================================ */

void brick6_app_init(void)
{
  //SDRAM_Init();
  //SDRAM_Test();

  //MX_USB_HOST_Init();

  /* --- Codec init --- */
  CS42448_Init(0x48);

  /* --- Mixer init (first engine module) --- */
  mixer_init();

  /* --- Daisy-style gain staging (audio_float boundary) --- */
  audio_float_set_postgain(1.0f);
  audio_float_set_output_compensation(1.0f);

  /* Track state init: disable all + clear buffers/gains */
  audio_tracks_init();

  /* Example: set master volume (-6 dB approx) */
  mixer_set_master(2.0f);

  /* Enable stereo tracks: 0->slots0/1, 1->slots2/3, 2->slots4/5 */
  track_enable(0, 1U);
  track_enable(1, 1U);
  track_enable(2, 1U);

  track_set_gain(0, 1.0f);
  track_set_gain(1, 1.0f);
  track_set_gain(2, 1.0f);

  /* --- Audio init --- */
  audio_init(&hsai_BlockA1, &hsai_BlockB1);

  /* Float DSP entry point */
  audio_set_float_callback(my_dsp);

  engine_tasklet_init(48000);
  /* --- Start DMA audio --- */
  audio_start();

  HAL_Delay(200);
}
