# Audit d’architecture — version approfondie (production-grade)

Contexte cible: STM32H743, audio DMA IRQ hard realtime, UI/tasklets en main loop, zéro `malloc` en audio, évolution progressive sans casser le chemin audio existant.

---

## 1) Revue critique du plan précédent (STEP 1 → STEP 4)

## STEP 1 précédent: `control_router` + `param_store`

### Sous-spécifié
- Le plan ne définissait pas **le modèle de cohérence** entre écriture UI et lecture IRQ (instantané, bloc, événement).
- Pas de contrat explicite sur **qui écrit quoi**:
  - UI/MIDI/sequencer peuvent-ils écrire en parallèle ?
  - qui arbitre en cas de conflits ?
- Pas de granularité d’atomicité (paramètre unique vs groupe atomique).

### Risques realtime
- Si implémenté en « variables partagées directes » sans protocole, risque de lecture d’état transitoire en IRQ (ex: cutoff mis à jour, resonance pas encore).
- Si implémenté avec verrous globaux, risque de gigue audio (verrou pris côté main loop au mauvais moment).

### Manques futur (seq/storage/FX routing)
- Sans notion de **transaction** ou de **snapshot**, impossible de garantir qu’un pattern/preset applique un état cohérent multi-param.
- Pas de canal pour événements non-paramétriques (note on/off, changement de pattern, reset FX).

## STEP 2 précédent: split `audio_float.c`

### Sous-spécifié
- Le split listait des fichiers mais pas l’API exacte ni les dépendances autorisées.
- Absence de définition claire de frontière:
  - `audio_io` pure conversion?
  - `dsp_engine` seul propriétaire des états DSP?

### Risques realtime
- Refactor trop tôt de fonctions IRQ sensibles (unpack/pack) peut introduire régression CPU/cache/alignment.
- Risque d’ajouter des indirections/branches non mesurées dans boucle sample.

### Manques futur
- Pas de place explicite pour intégration FX pool configurable (insert/send).
- Pas de stratégie de migration compatibilité bit/son de la chaîne existante.

## STEP 3 précédent: FX pool

### Sous-spécifié
- Pas de sizing mémoire concret (nombre max instances par type, footprint total SDRAM/D2).
- Pas de distinction claire entre FX:
  - track insert,
  - bus send/return,
  - master insert.

### Risques realtime
- Activation/désactivation runtime sans protocole peut provoquer état incohérent lu en IRQ.
- Reset d’un FX coûteux si exécuté dans IRQ.

### Manques futur
- Absence de mécanisme de « reconfiguration deferred » (appliquer changement de routing uniquement en frontière de bloc).

## STEP 4 précédent: storage + seq bridge

### Sous-spécifié
- Le format projet n’était pas défini (versioning, sections, CRC, compatibilité).
- Pas de séparation stricte « storage ↔ UI ».

### Risques realtime
- Pas explicité que save/load doit rester hors IRQ avec budget tasklet.
- Pas de mécanisme de commit paramétrique au bloc audio lors d’un load.

### Manques futur
- Pas de lien formel entre clock audio-driven (`engine_tasklet`) et automation/p-lock.

---

## 2) Flux de données final (obligatoire)

## Choix du modèle: **D) hybride (double-buffer params + queue lock-free événements)**

### Pourquoi ce choix
- **Double-buffer params**: fournit un snapshot cohérent de tous les paramètres visibles par l’audio **au début de bloc**.
- **Queue lock-free événements**: gère les actions discrètes (note on/off, pattern change, FX activate/reset) sans forcer un gros snapshot à chaque événement.
- Ce modèle couvre les deux natures de contrôle:
  - continu (knobs/automation) → params,
  - discret (triggers/commands) → events.

### Domaine CONTROL (main loop)
- Sources: UI, MIDI, sequencer (futur).
- Écritures:
  1. `control_router_set_param(id, value)` → écrit dans `param_store_staging[]`.
  2. `control_router_push_event(evt)` → enqueue lock-free dans `control_event_queue`.
  3. `control_router_commit_if_needed()` → publie snapshot param atomiquement (flip d’index/version).

### Domaine AUDIO (IRQ DMA)
- Au début de `dsp_engine_process_block()`:
  1. lit `active_param_bank_index` (volatile 32-bit),
  2. prend pointeur bank immutable pour ce bloc,
  3. draine N événements max depuis queue (budget borné),
  4. traite bloc complet avec snapshot stable.

### Cohérence / visibilité
- Les paramètres modifiés en main loop deviennent visibles **uniquement au bloc suivant** après commit.
- Un bloc IRQ ne voit **jamais** un mélange ancien/nouveau d’un même snapshot.
- Les événements sont ordonnés FIFO via ring buffer SPSC.

---

## 3) Param system concret

## 3.1 Fichiers
- `Inc/param_ids.h`
- `Src/param_store.c`
- `Inc/param_store.h`
- `Src/control_router.c`
- `Inc/control_router.h`
- `Src/param_smoother.c` (phase 2, optionnel)

## 3.2 Format `param_id_t`

```c
typedef uint16_t param_id_t;

// [15:12]=domain, [11:8]=entity, [7:0]=index
// domain: 0=GLOBAL,1=TRACK,2=FX,3=SEND,4=SEQ
```

- Exemples:
  - `PARAM_GLOBAL_MASTER_GAIN`
  - `PARAM_TRACK0_GAIN`, `PARAM_TRACK0_PAN`
  - `PARAM_FX0_MIX`, `PARAM_FX0_BYPASS`
  - `PARAM_SEND0_LEVEL_TRACK1`

## 3.3 Layout mémoire

```c
#define PARAM_COUNT 256U

typedef struct {
    float value[PARAM_COUNT];
    uint32_t revision;
} param_bank_t;

static param_bank_t g_param_banks[2];      // double buffer
static volatile uint32_t g_active_bank;    // 0 or 1, lu par IRQ
static uint32_t g_staging_bank;            // écrit par control domain
static volatile uint32_t g_pending_commit; // flag
```

- Bank active: lecture IRQ only.
- Bank staging: écriture control only.
- Flip bank: store 32-bit atomique (section critique courte côté control, sans lock côté IRQ).

## 3.4 Write path (UI/MIDI/SEQ)
1. UI/MIDI/SEQ appellent `control_router_set_param()`.
2. Router valide/clamp selon méta param.
3. Écrit dans bank staging (`g_param_banks[g_staging_bank].value[id]`).
4. Marque dirty bitset (`param_dirty[id]=1`).
5. `control_router_commit_if_needed()` publie snapshot (flip bank + revision++).

## 3.5 Read path (DSP IRQ)
1. `param_store_audio_acquire()` retourne pointeur bank active immutable.
2. `dsp_engine` lit les params depuis ce pointeur durant tout le bloc.
3. Aucun write de params en IRQ.

## 3.6 Anti-glitch / anti-tearing
- Pas de tearing global grâce au bank switch blocaire.
- Pour rampes sensibles (gain/freq), utiliser `param_smoother` côté audio:
  - cible = valeur snapshot,
  - interpolation linéaire sur `frames`.
- Mises à jour multi-param cohérentes via commit unique (ex: cutoff+resonance).

---

## 4) FX pool concret

## 4.1 Modèle mémoire

Statique, sans `malloc`, dimensionné compile-time:

```c
#define FX_POOL_MAX_SLOTS      12
#define FX_MAX_TRACK_INSERTS    2
#define FX_MAX_BUS_SENDS        2
#define FX_MAX_RETURNS          2

typedef enum {
  FX_NONE=0, FX_EQ3, FX_SAT, FX_GRANULAR, FX_REVERB, FX_DELAY
} fx_type_t;

typedef struct {
  uint8_t active;
  uint8_t bypass;
  fx_type_t type;
  uint8_t owner_kind;   // track/bus/master
  uint8_t owner_index;
  uint8_t state_index;  // index dans array d'état par type
} fx_slot_t;
```

États par type en pools séparés (exemples):

```c
static fx_dj_eq3_t      g_eq_state[4];
static fx_saturation_t  g_sat_state[4];
static granular_state_t g_gran_state[2];
static fx_reverb_t      g_rev_state[2];
```

## 4.2 Routing
- **Track inserts**: `track[n] -> insert0 -> insert1 -> mix`.
- **Sends**: `track[n] -> send0/send1` vers bus FX.
- **Returns**: returns ajoutés au bus main dans `audio_mix`.

Table de routing immutable par bloc:

```c
typedef struct {
  int8_t track_insert_slot[MAX_TRACKS][FX_MAX_TRACK_INSERTS];
  int8_t send_slot[FX_MAX_BUS_SENDS];
  float send_level[MAX_TRACKS][FX_MAX_BUS_SENDS];
} fx_routing_snapshot_t;
```

## 4.3 Lifecycle
- `fx_pool_init()` au boot.
- `fx_pool_activate(slot, type, owner...)` hors IRQ, configuration staging.
- `fx_pool_set_bypass(slot, on)` via control router.
- `fx_pool_request_reset(slot)` hors IRQ; reset appliqué au début d’un bloc audio (safe point).

## 4.4 Intégration DSP
- `dsp_engine_process_block()`:
  1. lit snapshot params + routing,
  2. process inserts par track,
  3. calcule sends,
  4. process returns,
  5. appelle `audio_mix_process()`.

## 4.5 Migration chaîne actuelle sans rupture
- Initial routing config reproduit exactement: `track0: EQ3 -> SAT -> GRANULAR`.
- Même ordre, mêmes params par défaut, mêmes gains.
- Ensuite seulement, ouverture config dynamique via control events.

---

## 5) Split strict audio engine

## `audio.c` (inchangé au maximum)
### Responsabilités
- IRQ DMA callbacks RX half/full.
- Appel unique `audio_process_block_int32(rx, tx, frames)`.
- Notification `engine_tasklet_notify_frames()`.

### Interdits
- Aucun param routing.
- Aucun appel UI/storage.

## `audio_io.c`
### Responsabilités
- unpack int24 TDM -> float tracks.
- pack float bus -> int24 TDM.
- mapping de slots TDM.

### Interdits
- Aucun FX, aucun param métier, aucun routing logique.

## `dsp_engine.c`
### Responsabilités
- point d’entrée DSP bloc.
- acquisition snapshot params/routing.
- orchestration inserts/sends/returns.
- application de smoothers.

### Interdits
- Aucun accès HAL.
- Aucun stockage SD/UI.

## `audio_mix.c`
### Responsabilités
- sommation tracks vers main/cue.
- application gains master/cue/send.
- gestion headroom/limiter simple (si ajouté).

### Interdits
- Pas d’accès paramètres globaux mutable hors snapshot.
- Pas de logique de commande.

---

## 6) Intégration engine_tasklet (clock audio-driven)

- IRQ audio appelle déjà `engine_tasklet_notify_frames(frames)`.
- Main loop appelle `engine_tasklet_poll()` qui génère `engine_tick` stable.

## Rôle séquenceur futur
- `seq_engine_on_tick(tick_index)` appelé dans main loop.
- Le séquenceur produit:
  - events discrets (trig note, pattern switch) → queue events,
  - valeurs continues p-lock → `control_router_set_param` puis commit.

## Rôle automation/p-lock
- p-locks planifiés en ticks audio-driven garantissent timing stable.
- Visibilité audio: bloc suivant après commit (latence déterministe ≤ 1 block).

---

## 7) Storage architecture réelle

## Fichiers
- `Inc/project_format.h`
- `Src/storage_manager.c`
- `Inc/storage_manager.h`

## Principes
- Storage **ne dépend pas** de UI.
- Storage lit/écrit via `param_store` + `seq_model`.
- I/O SD borné par tasklet (pas d’accès IRQ audio).

## Format projet (versionné)

```c
#define PROJECT_MAGIC 0x42524B36u // "BRK6"

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint32_t param_count;
  uint32_t seq_bytes;
  uint32_t crc32;
} project_header_t;
```

Sections:
1. header
2. snapshot params (`param_store_export()`)
3. routing FX
4. données séquenceur (patterns/p-lock)

## API storage
- `storage_save_project(slot)`
- `storage_load_project(slot)`
- `storage_request_pattern(pattern_id)` (éventuellement via seq layer)

Load workflow safe:
1. lire SD en buffers tasklet,
2. vérifier CRC,
3. charger dans staging,
4. commit unique param/routing,
5. publier event `SEQ_PATTERN_SWITCH`.

---

## 8) Arborescence cible (sans dépendances circulaires)

```text
Inc/
  audio/
    audio.h
    audio_io.h
  dsp/
    dsp_engine.h
    audio_mix.h
  control/
    param_ids.h
    param_store.h
    control_router.h
    control_events.h
  fx/
    fx_pool.h
    fx_chain.h
  seq/
    seq_model.h
    seq_engine.h
    seq_param_bridge.h
  storage/
    project_format.h
    storage_manager.h
  ui/
    ui_tasklet.h

Src/
  audio/
    audio.c
    audio_io.c
  dsp/
    dsp_engine.c
    audio_mix.c
  control/
    param_store.c
    control_router.c
    control_events.c
  fx/
    fx_pool.c
    fx_chain.c
  seq/
    seq_model.c
    seq_engine.c
    seq_param_bridge.c
  storage/
    storage_manager.c
  ui/
    ui_tasklet.c
    app_controls.c
```

Règles de dépendances:
- `audio/*` dépend de `dsp/*`, jamais de `ui/*` ni `storage/*`.
- `ui/*`, `midi/*`, `seq/*` écrivent via `control_router` uniquement.
- `storage/*` utilise `param_store` + `seq_model`, jamais `ui/*`.

---

## 9) Plan de migration réaliste (audio never break)

## Phase 0 (préparation, zéro changement audio)
1. Créer `param_ids.h`, `param_store.*`, `control_router.*` avec tests unitaires host-side si possible.
2. Ajouter `control_events` ring SPSC (non branché audio encore).

## Phase 1 (découplage contrôle, sans toucher IRQ)
1. Modifier `app_controls.c` et MIDI control path pour passer par `control_router`.
2. Garder la chaîne DSP actuelle intacte (`audio_float.c`) mais lire params via API store (wrapper temporaire).

## Phase 2 (split technique minimal)
1. Extraire `audio_io_unpack/pack` vers `audio_io.c`.
2. Introduire `dsp_engine.c` comme façade interne, appelée par `audio_process_block_int32`.
3. Vérifier budget CPU/audio inchangé.

## Phase 3 (FX pool compat mode)
1. Introduire `fx_pool` + routing snapshot.
2. Charger config par défaut équivalente à la chaîne actuelle track0.
3. Basculer traitement DSP vers `fx_chain_process_track()`.

## Phase 4 (séquenceur/storage)
1. Ajouter `seq_engine` consommant `engine_tasklet` ticks en main loop.
2. Ajouter `storage_manager` avec format projet versionné + CRC.
3. Brancher save/load sur `param_store` snapshot + seq data.

## Ce qui reste explicitement inchangé au début
- `audio.c` IRQ callbacks.
- mécanique DMA SAI.
- `engine_tasklet` base clock.

## Ce qui est délibérément retardé
- optimisation SIMD fine,
- refonte UI complète,
- extensions FX complexes (après stabilité de pool/routing).

---

## Conclusion opérationnelle

Le système doit converger vers une séparation stricte **control domain (main loop)** / **audio domain (IRQ)** avec publication atomique au bloc. Le modèle hybride (double-buffer params + queue events SPSC) est le meilleur compromis pour robustesse temps réel, cohérence multi-param, et évolutivité (FX pool, p-lock, storage), tout en permettant une migration progressive sans casser l’audio existant.
