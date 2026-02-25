#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @file fx_pool.h
 * @brief Pool statique de slots FX réutilisables par les chaînes de traitement.
 *
 * Rôle du module:
 * - Décrire les types de slots FX disponibles.
 * - Exposer l'accès aux slots persistants du moteur.
 *
 * Architecture:
 * - Appelé par: fx_chain.c, mixer.c, audio init.
 * - Appelle: aucun.
 *
 * Contraintes temps réel:
 * - Lecture possible en IRQ audio.
 * - Aucune allocation dynamique.
 */

typedef enum {
    FX_NONE = 0,
    FX_EQ3,
    FX_SAT,
    FX_GRANULAR
} fx_type_t;

typedef struct {
    uint8_t active;
    uint8_t type;
    void* state;
} fx_slot_t;

void fx_pool_init(void);
fx_slot_t* fx_pool_get_slot(uint32_t index);

void* fx_alloc_fast(size_t size);
void* fx_alloc_slow(size_t size);

int fx_pool_activate_slot(uint32_t index, fx_type_t type);
void fx_pool_deactivate_slot(uint32_t index);
