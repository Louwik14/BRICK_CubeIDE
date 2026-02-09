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

#include "brick6_app_init.h"

#include "brick6_refactor.h"
#include "diagnostics_tasklet.h"
#include "engine_tasklet.h"
//#include "midi.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"
#include "audio.h"
#include "cs42448.h"

void brick6_app_init(void)
{
  //SDRAM_Init();
  //SDRAM_Test();

  //MX_USB_HOST_Init();

  CS42448_Init(0x48);
  CS42448_DiagnosticsDump(0x48);

  audio_init(&hsai_BlockA1, &hsai_BlockB1);
  audio_start();

  HAL_Delay(200);
}
