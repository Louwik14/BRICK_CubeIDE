# AUDIT + REFINEMENT ARCHI (LEVEL 2)

Références analysées: `roadmap.md` + code actuel (`audio.c`, `audio_float.c`, `app_controls.c`, `engine_tasklet.c`, `mixer.c`, `sd_stream.c`, `midi*.c`).

---

## 1) Re-audit ciblé (points critiques)

## 1.1 Synchronisation commit paramètres

### État réel actuel
- Aucun `param_store`, aucun `control_router`, aucun mécanisme de commit par bloc.
- Les contrôles UI appellent directement `fx_granular_set_*()` hors IRQ.
- Le DSP lit/écrit ses états dans `audio_float.c` directement pendant IRQ.

### Verdict
- **Absent**: pas de garantie "1 commit max / bloc audio".

### Correction proposée (concrète)
Ajouter un compteur de blocs audio et un commit borné:

```c
// audio domain (IRQ)
volatile uint32_t g_audio_block_counter;

void audio_process_block_int32(...) {
  g_audio_block_counter++;
  dsp_engine_process_block(...);
}

// control domain
static uint32_t g_last_commit_block;

bool param_store_commit_if_block_advanced(void) {
  uint32_t b = g_audio_block_counter;
  if (b == g_last_commit_block) return false;
  // flip staging->active atomique (section critique courte)
  g_last_commit_block = b;
  return true;
}
```

- Résultat: maximum un commit publié entre deux blocs IRQ.

## 1.2 Cohérence atomique params + FX routing

### État réel actuel
- Aucun snapshot de routing FX.
- Routing/ordre FX est codé en dur dans `audio_float.c` (EQ→SAT→GRANULAR sur track 0).

### Verdict
- **Absent**: pas de cohérence atomique params+routing.

### Correction proposée (concrète)
Double-buffer **synchronisé** params+routing, avec flip unique:

```c
typedef struct {
  param_bank_t params;
  fx_routing_t routing;
  uint32_t revision;
} control_snapshot_t;

static control_snapshot_t g_snap[2];
static volatile uint32_t g_active_snap;
```

- `commit()` publie **ensemble** params + routing.
- IRQ acquiert une seule fois `active_snap` en début de bloc.

## 1.3 Event queue realtime safety

### État réel actuel
- Pas de queue de contrôle dédiée DSP (seulement des queues MIDI USB internes transport).
- Pas de budget d’événements par bloc audio.

### Verdict
- **Absent**: risque d’implémentation future non bornée côté IRQ.

### Correction proposée (concrète)
Queue SPSC lock-free + budget fixe dans DSP:

```c
#define CONTROL_EVT_Q_LEN 64U
#define CONTROL_EVT_BUDGET_PER_BLOCK 8U

for (uint32_t i = 0; i < CONTROL_EVT_BUDGET_PER_BLOCK; ++i) {
  if (!control_evt_pop(&evt)) break;
  dsp_apply_event(&evt);
}
```

- Le surplus reste en queue pour bloc suivant.
- Coût IRQ strictement borné.

## 1.4 Param smoothing robustesse

### État réel actuel
- Pas de module `param_smoother` central.
- Pas de protocole global de reset des smoothers sur load project / pattern switch / reset FX.

### Verdict
- **Absent**.

### Correction proposée (concrète)
Event dédié au DSP: `EVT_SMOOTHER_RESET(mask)`.

Cas déclencheurs:
1. `storage_load_project()` terminé,
2. changement de pattern majeur,
3. reset FX instance.

Traitement:
- L’event est consommé au début de bloc IRQ,
- les smoothers ciblés sont recopiés instantanément sur la valeur courante snapshot (pas de rampe résiduelle).

---

## 2) Vérification architecture réelle vs cible

## A) Déjà conforme roadmap

1. **Audio IRQ hard realtime structuré**
- `audio.c` traite dans callbacks DMA RX half/full.
- Buffers DMA statiques, pas d’allocation dynamique dans chemin IRQ.

2. **Clock audio-driven disponible**
- `engine_tasklet_notify_frames()` en IRQ + `engine_tasklet_poll()` en main loop.
- Bonne base pour séquenceur temps audio.

3. **Découplage grossier IRQ vs main loop**
- UI/affichage/encodeurs hors IRQ (`ui_tasklet`, `app_controls`).

## B) À modifier

1. `Src/app_controls.c`
- Remplacer appels directs `fx_granular_set_*` par `control_router_set_param`.

2. `Src/audio_float.c`
- Retirer la responsabilité de gouvernance param/routing.
- Conserver traitement audio, mais brancher lecture snapshot immutable.

3. `Src/brick6_app_init.c`
- Initialiser `param_store/control_router` avant `audio_start()`.

4. `Src/mixer.c`
- Clarifier ownership des gains (éviter double source mirror vs moteur audio).

## C) Manque totalement

- `param_ids.h`, `param_store.*`, `control_router.*`.
- `control_event_queue` dédiée DSP.
- `dsp_engine.c` (entrée unique indépendante de `audio_float` monolithique).
- `fx_pool.*`, `fx_chain.*`.
- `storage_manager.*`, `project_format.h`.
- `seq_model/seq_engine/seq_param_bridge`.

---

## 3) STEP NEXT — Param Commit Guard (safe)

### Objectif
Introduire la sécurité minimale avant toute refonte: **publication paramétrique bornée à 1 commit max par bloc audio**, sans changer la chaîne DSP actuelle.

### Fichiers à créer
- `Inc/param_store.h`
- `Src/param_store.c`

### Fichiers à modifier
- `Src/audio_float.c` (exposer/incrémenter `audio_block_counter` en fin de bloc)
- `Inc/audio_float.h` (déclaration compteur en lecture)
- `Src/app_controls.c` (optionnel minimal: passer par `param_store_set_staging()` puis `param_store_commit_if_block_advanced()`; **sans** changer encore les setters FX)

### Diff minimal attendu
1. Ajouter:
   - `volatile uint32_t g_audio_block_counter` (increment 1x par appel `audio_process_block_int32`).
2. Ajouter API `param_store` minimale:
   - `param_store_set_staging(id, float)`
   - `param_store_commit_if_block_advanced(void)`
   - `param_store_get_active(id)`
3. Dans UI tasklet:
   - écrire staging,
   - tenter commit (au plus un par bloc).

### Risques
- **Très faible**: pas de changement du chemin DMA ni de l’ordre DSP.
- Risque principal: overhead mineur en main loop (négligeable).

### Méthode de validation
1. Instrumentation debug:
   - compteur `commit_count`, `last_commit_block`.
2. Vérifier propriété:
   - `commit_count` n’augmente jamais de plus de 1 pour une valeur donnée de `g_audio_block_counter`.
3. Vérifier audio:
   - aucune régression de callback DMA,
   - charge CPU audio inchangée (marge ±1%).

---

## 4) Contraintes hard realtime retenues

- Aucun `malloc` en IRQ/audio.
- Aucun lock bloquant dans audio.
- Coût IRQ borné (events budgetés).
- Commits/flip snapshots réalisés hors IRQ, avec section critique ultra courte seulement pour index actif.

---

## 5) Bonus — risques de layering / mémoire

1. **Violation layering actuelle**: UI dépend directement de FX (`app_controls.c` -> `fx_granular_*`).
2. **Couplage futur dangereux** si storage écrit directement UI au lieu de `param_store`.
3. **Cache/DMA futur**: si D-Cache activé, maintenir buffers DMA en régions non-cacheables ou opérations clean/invalidate strictes (sinon artefacts audio).
4. **Potentiel dépendance circulaire** à éviter:
   - `dsp_engine` ne doit pas dépendre de `ui`/`storage`.
   - `control_router` ne doit pas dépendre de `audio.c`.
