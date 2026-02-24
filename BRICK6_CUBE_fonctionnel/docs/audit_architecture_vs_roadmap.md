# LEVEL 3 — Validation finale + pré-implémentation (STEP NEXT safe)

## 1) VALIDATION CRITIQUE

## A. `g_audio_block_counter`

### Vérification code réel
- `audio_process_block_int32()` est appelé dans `process_half()` uniquement. (`Src/audio.c`)
- `process_half()` est appelé uniquement depuis:
  - `HAL_SAI_RxHalfCpltCallback()`
  - `HAL_SAI_RxCpltCallback()`
  avec garde `if (hsai == sai_rx)`. (`Src/audio.c`)

### Conclusion
- Incrémenter le compteur **dans `audio_process_block_int32()`** donne exactement **1 incrément par bloc traité** (bloc = appel DSP sur half-buffer).
- Pas de double incrément parasite tant que l’incrément n’est fait qu’à cet endroit.

### Design retenu (safe)
- `g_audio_block_counter++` au tout début de `audio_process_block_int32()`.
- **Ne pas** incrémenter dans callbacks HAL ni ailleurs.

---

## B. Atomicité du commit

### Problème à couvrir
Code naïf:
```c
uint32_t b = g_audio_block_counter;
if (b == g_last_commit_block) return false;
```

### Validation Cortex-M7
- Lecture/écriture 32-bit alignées sont atomiques.
- `volatile` garantit l’accès mémoire, pas l’exclusion.

### Version strictement safe retenue
- Pas de section critique longue.
- Séquence:
  1. lire `b = g_audio_block_counter`,
  2. si `b == last` → no-op,
  3. copier staging -> active,
  4. barrière mémoire (`__DMB()`),
  5. écrire `g_last_commit_block = b`.

Pourquoi safe pour STEP 1:
- DSP actuel **ne lit pas encore** `param_store`; donc pas de risque audio.
- Si IRQ avance pendant copie, commit reste valide; au pire un commit supplémentaire sera autorisé au bloc suivant (comportement attendu).

---

## C. Coût CPU

### Risque
- `param_store_commit_if_block_advanced()` appelée trop souvent en main loop.

### Mitigation minimale (obligatoire)
1. `dirty_flag` global: pas de copy si aucun param modifié.
2. Appel commit uniquement quand `changed` UI est vrai (déjà disponible dans `app_controls_process`).
3. Coût constant borné: copie `PARAM_COUNT` floats max par commit.

---

## D. Impact sur DSP actuel

### Risque
- incohérence si on mélange nouveau `param_store` avec anciens `fx_granular_set_*`.

### Garde-fou (obligatoire)
- STEP 1 = **shadow store only**:
  - on continue d’appeler `fx_granular_set_*` exactement comme aujourd’hui,
  - `param_store` stocke les valeurs pour validation/migration,
  - aucun read DSP depuis `param_store` dans cette étape.

=> Aucun impact audible attendu.

---

## 2) DESIGN FINAL `param_store` (minimal propre)

## A. Structure mémoire exacte

```c
#define PARAM_COUNT 32U

typedef uint16_t param_id_t;

typedef struct {
    float staging[PARAM_COUNT];
    float active[PARAM_COUNT];
    volatile uint32_t last_commit_block;
    volatile uint32_t commit_count;
    volatile uint8_t dirty;
} param_store_t;
```

- `PARAM_COUNT=32` pour STEP 1 (granular + réserve).
- `active/staging` séparés pour migration future vers double-buffer global.

## B. API exacte

```c
void  param_store_init(void);
void  param_store_set_staging(param_id_t id, float v);
bool  param_store_commit_if_block_advanced(void);
float param_store_get_active(param_id_t id);
uint32_t param_store_get_commit_count(void);
uint32_t param_store_get_last_commit_block(void);
```

## C. Garanties
- Atomicité: écritures 32-bit `last_commit_block` / `commit_count` atomiques.
- Pas de tearing côté audio: DSP non branché sur `param_store` à cette étape.
- Coût constant: O(PARAM_COUNT) seulement quand `dirty=1` et bloc avancé.

---

## 3) PATCH PRÉCIS (étape suivante uniquement)

## Fichiers à créer

### `Inc/param_store.h` (contenu complet)
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
    PARAM_COUNT = 32
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

### `Src/param_store.c` (contenu complet)
```c
#include "param_store.h"
#include "audio_float.h"   // g_audio_block_counter
#include "stm32h7xx_hal.h" // __DMB
#include <string.h>

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
    if (b == g_ps.last_commit_block) return false;

    for (uint32_t i = 0; i < PARAM_COUNT; i++) {
        g_ps.active[i] = g_ps.staging[i];
    }

    __DMB();
    g_ps.last_commit_block = b;
    g_ps.commit_count++;
    g_ps.dirty = 0U;
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

## Fichiers à modifier (diff minimal)

### `Inc/audio_float.h`
Ajouter:
```c
extern volatile uint32_t g_audio_block_counter;
```

### `Src/audio_float.c`
Ajouter global + incrément unique:
```c
volatile uint32_t g_audio_block_counter = 0U;
```

Dans `audio_process_block_int32(...)`, première ligne:
```c
g_audio_block_counter++;
```

### `Src/brick6_app_init.c`
Ajouter init avant `audio_start()`:
```c
#include "param_store.h"
...
param_store_init();
```

### `Src/app_controls.c`
Ajouts minimaux (sans retirer setters FX actuels):
- inclure `param_store.h`
- dans les deux chemins de mise à jour granular:
```c
param_store_set_staging(PARAM_GRAN_DENSITY, ui_0_127_to_unit_float(granular_density));
param_store_set_staging(PARAM_GRAN_PITCH, ui_0_127_to_pitch_semitones(granular_pitch));
param_store_set_staging(PARAM_GRAN_MIX, ui_0_127_to_unit_float(granular_mix));
param_store_set_staging(PARAM_GRAN_FREEZE, (granular_freeze >= 64U) ? 1.0f : 0.0f);
param_store_set_staging(PARAM_GRAN_SPREAD, ui_0_127_to_unit_float(granular_spread));
param_store_set_staging(PARAM_GRAN_STEREO, ui_0_127_to_unit_float(granular_stereo));
(void)param_store_commit_if_block_advanced();
```

---

## 4) CHECKLIST AVANT MERGE

- [ ] audio toujours stable (DMA RX half/full OK)
- [ ] aucun warning build ajouté
- [ ] aucun `malloc` ajouté
- [ ] aucun appel HAL ajouté en IRQ
- [ ] commit <= 1 par bloc validé (`commit_count` vs `g_audio_block_counter`)
- [ ] aucun changement audible (chaîne DSP inchangée)

---

## 5) Notes de sécurité immédiates

- Ne pas brancher `param_store_get_active()` dans DSP durant STEP 1.
- Ne pas modifier `audio.c` callbacks.
- Ne pas déplacer l’ordre actuel EQ→SAT→GRANULAR dans cette étape.
