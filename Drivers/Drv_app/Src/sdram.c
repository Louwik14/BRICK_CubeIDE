/**
 * @file sdram.c
 * @brief Initialisation de la SDRAM externe via FMC.
 *
 * Ce module configure la SDRAM W9825G6KH via le contrôleur FMC et
 *
 * Rôle dans le système:
 * - Mise en service de la mémoire externe avant usage applicatif.
 *
 * Contraintes temps réel:
 * - Critique audio: non (exécuté à l'init).
 * - Tasklet: non.
 * - IRQ: non.
 * - Borné: non critique (HAL bloquant possible).
 *
 * Architecture:
 * - Appelé par: brick6_app_init.
 * - Appelle: HAL SDRAM/FMC, UART pour logs.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas appeler en IRQ.
 *
 * @note L’API publique est déclarée dans sdram.h.
 */

#include "sdram.h"
#include "fmc.h"
#include "w9825g6kh_conf.h"

static FMC_SDRAM_CommandTypeDef sdram_command;

/* =========================================================
 * Local prototypes
 * ========================================================= */
static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command);

/* =========================================================
 * Public API
 * ========================================================= */

/**
 * @brief Point d'entrée SDRAM_Init.
 *
 * Rôle:
 * - Exécuter le traitement associé à SDRAM_Init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void SDRAM_Init(void)
{
    SDRAM_Initialization_Sequence(&hsdram1, &sdram_command);
}

/**
 *
 * Rôle:
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */


/* =========================================================
 * SDRAM Initialization Sequence (ST style)
 * ========================================================= */

/**
 * @brief Point d'entrée SDRAM_Initialization_Sequence.
 *
 * Rôle:
 * - Exécuter le traitement associé à SDRAM_Initialization_Sequence.
 *
 * @param hsdram Paramètre d'entrée de l'API.
 * @param command Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram,
                                          FMC_SDRAM_CommandTypeDef *command)
{
    __IO uint32_t tmpmrd = 0;

    /* Step 1: Clock enable */
    command->CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    HAL_Delay(1);

    /* Step 2: Precharge all */
    command->CommandMode = FMC_SDRAM_CMD_PALL;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 3: Auto-refresh */
    command->CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 8;
    command->ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 4: Load mode register */
    tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1 |
             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
             SDRAM_MODEREG_CAS_LATENCY_3 |
             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    command->CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command->CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command->AutoRefreshNumber = 1;
    command->ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(hsdram, command, SDRAM_TIMEOUT);

    /* Step 5: Set refresh rate */
    HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

/* =========================================================
 * Utils
 * ========================================================= */

/**
 *
 * Rôle:
 *
 * @param pBuffer Paramètre d'entrée de l'API.
 * @param buffer_length Paramètre d'entrée de l'API.
 * @param offset Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
