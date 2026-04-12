/**
 * @file sdram_alloc.c
 * @brief Allocateur linéaire simple pour tests SDRAM.
 *
 * Ce module fournit un bump allocator sans libération, destiné aux
 * diagnostics SDRAM et à l'initialisation des buffers statiques.
 *
 * Rôle dans le système:
 * - Allocation contrôlée dans la SDRAM externe.
 * - Support aux tests et aux buffers applicatifs.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - Tasklet: non (utilisé dans diagnostics).
 * - IRQ: non.
 * - Borné: oui (arithmétique simple, O(1)).
 *
 * Architecture:
 * - Appelé par: diagnostics_tasklet, init SDRAM.
 * - Appelle: aucun module externe.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage.
 *
 * @note L’API publique est déclarée dans sdram_alloc.h.
 */

#include "sdram_alloc.h"
#include "sdram.h"

#include <stddef.h>

static uint32_t sdram_alloc_base = SDRAM_BANK_ADDR;
static uint32_t sdram_alloc_size = SDRAM_ALLOC_DEFAULT_SIZE_BYTES;
static uint32_t sdram_alloc_offset = 0U;

/**
 * @brief Point d'entrée SDRAM_Alloc_Init.
 *
 * Rôle:
 * - Exécuter le traitement associé à SDRAM_Alloc_Init.
 *
 * @param base_address Paramètre d'entrée de l'API.
 * @param size_bytes Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void SDRAM_Alloc_Init(uint32_t base_address, uint32_t size_bytes)
{
    sdram_alloc_base = base_address;
    sdram_alloc_size = size_bytes;
    sdram_alloc_offset = 0U;
}

/**
 * @brief Point d'entrée SDRAM_Alloc_Reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à SDRAM_Alloc_Reset.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void SDRAM_Alloc_Reset(void)
{
    sdram_alloc_offset = 0U;
}

/**
 * @brief Point d'entrée Align_Up.
 *
 * Rôle:
 * - Exécuter le traitement associé à Align_Up.
 *
 * @param value Paramètre d'entrée de l'API.
 * @param alignment Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint32_t Align_Up(uint32_t value, uint32_t alignment)
{
    if (alignment == 0U)
    {
        return value;
    }

    uint32_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

void *SDRAM_Alloc(uint32_t size_bytes, uint32_t alignment)
{
    uint32_t aligned_offset = Align_Up(sdram_alloc_offset, alignment);
    uint32_t next_offset = aligned_offset + size_bytes;

    if (next_offset > sdram_alloc_size)
    {
        return NULL;
    }

    sdram_alloc_offset = next_offset;
    return (void *)(sdram_alloc_base + aligned_offset);
}
