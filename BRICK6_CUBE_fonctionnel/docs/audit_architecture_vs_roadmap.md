# LEVEL 4 — Sanity + Hardening avant merge (STEP 1)

## 1) Vérifications critiques

## A. Ordre exact `g_audio_block_counter++`

Décision finale:
- `g_audio_block_counter++` doit être **première instruction** de `audio_process_block_int32()`.
- Aucun incrément dans `audio.c` callbacks DMA.

Patch minimal attendu:
```c
// Src/audio_float.c
volatile uint32_t g_audio_block_counter = 0U;

void audio_process_block_int32(...)
{
    g_audio_block_counter++;   // tout en haut (avant unpack / DSP / pack)
    ...
}
```

## B. Race condition subtile commit

Cas N -> N+1 pendant commit: **acceptable pour STEP 1** (shadow store non lu par DSP).

Hardening retenu (overflow-safe):
```c
uint32_t b = g_audio_block_counter;
if ((uint32_t)(b - g_ps.last_commit_block) == 0U) return false;
```

- Gère overflow `uint32_t` naturellement.
- Évite faux négatifs liés à comparaison naïve future.

## C. Dirty flag robustesse

Règle stricte:
- `dirty = 1` sur tout set staging.
- `dirty = 0` **uniquement après commit réussi**.
- Si commit refusé (même bloc), `dirty` reste à 1.

## D. Taille `PARAM_COUNT`

Décision:
- passer à `PARAM_COUNT = 64U` (au lieu de 32) pour marge STEP suivant.
- granular courant = 6 params, marge suffisante sans impact notable mémoire.

## E. `__DMB()`

Décision:
- conserver `__DMB()` après copie staging->active.
- ajouter commentaire: barrière conservatrice pour ordre mémoire Cortex-M7 (migration future DSP-reader).

---

## 2) Micro-optimisations safe

## A. Copy loop

Remplacer boucle par:
```c
memcpy(g_ps.active, g_ps.staging, sizeof(g_ps.active));
```

## B. Commit fast-exit

Conserver en première ligne:
```c
if (g_ps.dirty == 0U) return false;
```

---

## 3) Debug log (optionnel, non-IRQ)

Ajouter guard:
```c
#ifdef PARAM_STORE_DEBUG
#include <stdio.h>
printf("param_commit count=%lu block=%lu\n",
       (unsigned long)g_ps.commit_count,
       (unsigned long)g_ps.last_commit_block);
#endif
```

- autorisé uniquement en main loop (`param_store_commit_if_block_advanced`).
- interdit en IRQ.

---

## 4) Design final `param_store` (STEP 1 only)

## A. `Inc/param_store.h` (contenu final)
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t param_id_t;

enum {
    PARAM_GRAN_DENSITY = 0,
    PARAM_GRAN_PITCH,
    PARAM_GRAN_MIX,
    PARAM_GRAN_FREEZE,
    PARAM_GRAN_SPREAD,
    PARAM_GRAN_STEREO,
    PARAM_COUNT = 64
};

void param_store_init(void);
void param_store_set_staging(param_id_t id, float v);
bool param_store_commit_if_block_advanced(void);
float param_store_get_active(param_id_t id);

uint32_t param_store_get_commit_count(void);
uint32_t param_store_get_last_commit_block(void);

#ifdef __cplusplus
}
#endif
```

## B. `Src/param_store.c` (contenu final)
```c
#include "param_store.h"
#include "audio_float.h"   // g_audio_block_counter
#include "stm32h7xx_hal.h" // __DMB
#include <string.h>
#ifdef PARAM_STORE_DEBUG
#include <stdio.h>
#endif

typedef struct {
    float staging[PARAM_COUNT];
    float active[PARAM_COUNT];
    volatile uint32_t last_commit_block;
    volatile uint32_t commit_count;
    volatile uint8_t dirty;
} param_store_t;

static param_store_t g_ps;

void param_store_init(void)
{
    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.last_commit_block = g_audio_block_counter;
}

void param_store_set_staging(param_id_t id, float v)
{
    if (id >= PARAM_COUNT) return;
    g_ps.staging[id] = v;
    g_ps.dirty = 1U;
}

bool param_store_commit_if_block_advanced(void)
{
    if (g_ps.dirty == 0U) return false;

    uint32_t b = g_audio_block_counter;
    if ((uint32_t)(b - g_ps.last_commit_block) == 0U) return false;

    memcpy(g_ps.active, g_ps.staging, sizeof(g_ps.active));

    __DMB(); // barrière conservatrice: ordre mémoire avant publication last_commit_block
    g_ps.last_commit_block = b;
    g_ps.commit_count++;
    g_ps.dirty = 0U;

#ifdef PARAM_STORE_DEBUG
    printf("param_commit count=%lu block=%lu\n",
           (unsigned long)g_ps.commit_count,
           (unsigned long)g_ps.last_commit_block);
#endif
    return true;
}

float param_store_get_active(param_id_t id)
{
    if (id >= PARAM_COUNT) return 0.0f;
    return g_ps.active[id];
}

uint32_t param_store_get_commit_count(void)
{
    return g_ps.commit_count;
}

uint32_t param_store_get_last_commit_block(void)
{
    return g_ps.last_commit_block;
}
```

---

## 5) Diff exact par fichier (minimal)

## Créer
- `Inc/param_store.h` (exact ci-dessus)
- `Src/param_store.c` (exact ci-dessus)

## Modifier

### `Inc/audio_float.h`
Ajouter:
```c
extern volatile uint32_t g_audio_block_counter;
```

### `Src/audio_float.c`
- Ajouter global:
```c
volatile uint32_t g_audio_block_counter = 0U;
```
- Ajouter en première ligne de `audio_process_block_int32()`:
```c
g_audio_block_counter++;
```

### `Src/brick6_app_init.c`
- inclure `param_store.h`
- appeler `param_store_init();` avant `audio_start();`

### `Src/app_controls.c`
- inclure `param_store.h`
- conserver setters `fx_granular_set_*` actuels
- ajouter staging + tentative commit uniquement quand valeurs changent

---

## 6) Validation finale avant merge

- [ ] aucun changement `audio.c` / callbacks DMA
- [ ] aucun HAL ajouté en IRQ
- [ ] aucun `malloc`
- [ ] build sans warning
- [ ] `commit_count` n’avance jamais > 1 pour un même `g_audio_block_counter`
- [ ] aucun crash si spam encodeurs
- [ ] aucun changement audible (DSP inchangé)

---

## 7) Garde-fous stricts STEP 1

- `param_store` reste shadow-only.
- Aucun read DSP depuis `param_store` dans ce merge.
- Aucun refactor de chaîne EQ→SAT→GRANULAR dans ce merge.
