# Passe 2 — verrouillage d’implémentation séquenceur (STM32)

## 1. Hypothèses reprises de la passe 1

- Transport global unique, start/stop via bouton PLAY.
- 8 tracks fixes V1, longueur libre par track, max 64 steps (4 pages x 16).
- Hall modes figés: HALL9=KBD, HALL10=ARP, HALL11=SEQ, SHIFT-before-HALL.
- En mode SEQ: transpose +/- = changement de page visible.
- LED V1: trig vert, playhead blanc, playhead invisible hors page.
- P-lock V1: ensembles p-lockables = COLORS + TONE (flag par ensemble), stockage sparse.
- Multi-step: offset relatif, auto-create lock manquant, clamp individuel.
- Si step suivant sans lock param: retour valeur base track.
- Pool p-lock: limite globale + limite par step; si plein => refus lock.
- Copy inter-tracks: copier compatible, ignorer incompatible, feedback “copie partielle”.
- Une séquence par track en V1, page SEQ mémorisée par track.
- Pas de malloc, pas de SDRAM externe sur le runtime critique.

Ancrage code existant (points réutilisés):
- `ui_core` possède déjà active track + hall mode + logique SHIFT+halls + double-tap.
- `ui_template_page` gère familles template track-aware.
- `led_layer` expose déjà couches `LED_LAYER_SEQ_STATE` et `LED_LAYER_SEQ_CURSOR`.
- `engine_tasklet` est déjà l’horloge centrale non-IRQ côté main loop.
- `audio_float` reste sur 4 tracks DSP runtime (important: séquenceur 8 tracks ≠ 8 moteurs audio complets aujourd’hui).

---

## 2. Décisions d’implémentation à verrouiller

1. **Point d’entrée runtime SEQ**: module `seq_runtime_process()` appelé dans `brick6_app_process()` (pas dans rendu UI).
2. **Base temporelle**: dériver du domaine audio (frames/ticks engine), pas de `HAL_GetTick()` pour avance steps.
3. **Application locks**: modèle diff `prev_active_mask -> next_active_mask` par track, avec restauration explicite des params sortants.
4. **Référence de valeur base**: cache base par track/param p-lockable, maintenu hors lecture step.
5. **Pipeline step boundary**: `restore obsolete` -> `apply new locks` -> `fire trig event`.
6. **No compaction en runtime**: free-list O(1), compactage volontairement absent en V1.
7. **Clipboard unique global SEQ**: structure fixe, overwrite sur COPY.
8. **Limite max locks/step V1**: **16** (décision ferme section 7).

---

## 3. Modèle exact d’application des p-locks

### 3.1 Principe

Pour chaque track en lecture, à chaque boundary de step:

1. Lire la liste de locks du step courant (`next_locks`).
2. Construire `next_mask` (bitset params lockés ce step).
3. Calculer `to_restore = prev_mask & ~next_mask`.
4. Restaurer chaque param de `to_restore` à sa valeur base track.
5. Appliquer chaque lock de `next_locks` (value lockée).
6. Mettre `prev_mask = next_mask` et cache des valeurs actives lock.
7. Déclencher trig/note event du step (si trig activé).

Pourquoi cet ordre:
- Le trig doit partir **après** application des locks du step courant (sinon note jouée avec ancien état).
- Les locks sortants sont restaurés avant, pour éviter fuite d’état.

### 3.2 Cas demandés

- **Step courant locke le param, step suivant ne le locke pas**:
  - step N: lock appliqué.
  - boundary N->N+1: param est dans `to_restore` -> retour base track.
- **Plusieurs params lockés**:
  - application dans ordre déterministe (ordre de liste lock du step).
  - collisions impossibles si unicité `(step,param)` respectée.
- **Plusieurs ensembles lockés sur un step**:
  - même pipeline; `lock_set_mask` mis à jour pour LED/couleur future.
- **Aucune dérive d’état**:
  - restauration systématique des params sortants.
  - base track maintenue séparément des locks runtime.

### 3.3 API recommandée (séquenceur <-> couche paramètres)

Ajouter une façade dédiée, sans passer par `ui_get_active_track()`:

```c
typedef enum {
    SEQ_APPLY_OK = 0,
    SEQ_APPLY_UNSUPPORTED_PARAM,
    SEQ_APPLY_UNSUPPORTED_TRACK,
} seq_apply_result_t;

// valeur base (q15/u16) pour un param p-lockable sur une track donnée
bool seq_param_base_get(uint8_t track, uint8_t param8, uint16_t *out_v);

// met à jour la valeur base (appelée quand utilisateur édite hors step-hold)
bool seq_param_base_set(uint8_t track, uint8_t param8, uint16_t v);

// applique une valeur lockée au runtime cible (track explicite)
seq_apply_result_t seq_param_apply_lock(uint8_t track, uint8_t param8, uint16_t v);

// restaure la valeur base au runtime cible
seq_apply_result_t seq_param_restore_base(uint8_t track, uint8_t param8);

// compat track/param (copy/paste + runtime safety)
bool seq_param_is_supported_on_track(uint8_t track, uint8_t param8);
```

### 3.4 Point de refactor indispensable (ancré code)

Aujourd’hui une partie des apply callbacks (`param_registry.c`) dépend du contexte actif UI (`ui_get_active_track`/résolution implicite). Pour un séquenceur qui continue pendant changement de focus track, il faut des apply funcs à **target track explicite** pour les params p-lockables (COLORS/TONE), sinon la lecture appliquerait les locks sur la mauvaise track.

---

## 4. Structures de données recommandées

## 4.1 Types et constantes

```c
#define SEQ_TRACK_COUNT             8u
#define SEQ_STEPS_PER_PAGE          16u
#define SEQ_MAX_PAGES               4u
#define SEQ_MAX_STEPS               (SEQ_STEPS_PER_PAGE * SEQ_MAX_PAGES) // 64

#define SEQ_STEP_MAX_LOCKS          16u   // décision V1
#define SEQ_PLOCK_POOL_CAP          1536u // global

#define SEQ_CLIPBOARD_MAX_STEPS     64u
#define SEQ_CLIPBOARD_MAX_LOCKS     512u

#define SEQ_PLOCK_PARAM_MAX         64u   // mapping compact p-lockable params
```

## 4.2 Données persistées

```c
typedef struct {
    uint16_t next;      // index pool suivant, 0xFFFF = fin
    uint8_t  param8;    // param id compact V1
    uint8_t  set_id;    // ensemble source (COLORS/TONE)
    uint16_t value16;   // valeur lockée quantifiée
    uint8_t  flags;     // réserve
    uint8_t  _pad;
} seq_plock_entry_t; // 8 bytes

typedef struct {
    uint16_t lock_head;     // tête de liste chainée dans pool, 0xFFFF = none
    uint8_t  lock_count;    // <= SEQ_STEP_MAX_LOCKS
    uint8_t  trig;          // 0/1
    uint8_t  lock_set_mask; // bit0=COLORS, bit1=TONE, futur extensible
    uint8_t  _rsv[3];
} seq_step_t; // 8 bytes

typedef struct {
    seq_step_t steps[SEQ_MAX_STEPS]; // 64
    uint8_t length_steps;            // 1..64
    uint8_t ui_page;                 // 0..3 mémorisé par track
    uint8_t _rsv[2];
} seq_track_data_t;

typedef struct {
    seq_track_data_t tracks[SEQ_TRACK_COUNT];

    // pool global locks
    seq_plock_entry_t pool[SEQ_PLOCK_POOL_CAP];
    uint16_t free_head;              // free-list head
    uint16_t free_count;

    uint16_t crc16;
    uint16_t version;
} seq_project_data_t;
```

## 4.3 Runtime non persisté

```c
typedef struct {
    uint8_t running;
    uint8_t clock_src;      // INT / EXT_MIDI
    uint8_t _rsv[2];

    uint32_t global_step_counter;

    // accumulateur fixe (pas de HAL_GetTick)
    uint32_t phase_q32;
    uint32_t inc_q32;       // dépend BPM + sample domain

    uint8_t play_step[SEQ_TRACK_COUNT]; // 0..63

    // masque params lockés au step courant, par track
    uint64_t active_lock_mask[SEQ_TRACK_COUNT];

    // base values p-lockables par track
    uint16_t base_value[SEQ_TRACK_COUNT][SEQ_PLOCK_PARAM_MAX];
} seq_runtime_state_t;
```

## 4.4 UI + édition non persistés

```c
typedef struct {
    uint8_t selected_track;   // miroir ui_get_active_track
    uint8_t visible_page;     // 0..3 (page courante affichée)
    uint8_t param_ui_open;    // 0/1
    uint8_t _rsv;
} seq_ui_state_t;

typedef struct {
    uint64_t held_steps_mask; // sélection steps dans la track active (bit 0..63)
    uint8_t  ref_step;        // step référence pour offset
    uint8_t  ref_valid;
    uint8_t  _rsv[2];
} seq_edit_state_t;

typedef struct {
    uint8_t valid;
    uint8_t src_track;
    uint8_t src_step_min;
    uint8_t step_count;

    uint8_t  rel_pos[SEQ_CLIPBOARD_MAX_STEPS]; // offsets relatifs depuis src_step_min
    seq_step_t step_meta[SEQ_CLIPBOARD_MAX_STEPS];

    seq_plock_entry_t locks[SEQ_CLIPBOARD_MAX_LOCKS];
    uint16_t lock_count;
} seq_clipboard_t;
```

---

## 5. Budget RAM estimatif

Hypothèse tailles alignées (STM32/GCC):

- `seq_plock_entry_t` = 8 B
- `seq_step_t` = 8 B
- `seq_track_data_t` = 64*8 + 4 = 516 B (arrondi 520 B)

### 5.1 Persisté + runtime central

- `tracks[8]` ≈ 8 * 520 = **4.1 KB**
- pool locks `1536 * 8` = **12.0 KB**
- overhead projet = **< 0.5 KB**

Sous-total `seq_project_data_t` ≈ **16.6 KB**

### 5.2 Runtime

- `active_lock_mask[8]` = 64 B
- `base_value[8][64]` = 1024 B
- state runtime divers ≈ 128 B

Sous-total runtime ≈ **1.3 KB**

### 5.3 UI/edit/clipboard

- `seq_edit_state_t` + `seq_ui_state_t` ≈ négligeable (< 64 B)
- clipboard:
  - `step_meta[64]` = 512 B
  - `locks[512]` = 4 KB
  - + rel_pos/headers ~ 128 B

Sous-total clipboard ≈ **4.7 KB**

### 5.4 Total V1

Environ **22.5–23.0 KB RAM**.

Répartition recommandée:
- **RAM critique runtime (D2 interne)**: `seq_project_data_t` + `seq_runtime_state_t` (lecture/écriture fréquente, déterminisme).
- **RAM non critique (D2 ou D1)**: clipboard.
- **À éviter en SDRAM externe** pour V1 runtime:
  - pool locks,
  - steps,
  - runtime active/base masks.

Raison: latence/variabilité SDRAM + debug plus dur dans chemin temps réel.

---

## 6. Politique de pool de p-locks

## 6.1 Limites

- Limite par step: `SEQ_STEP_MAX_LOCKS = 16`.
- Limite globale pool: `SEQ_PLOCK_POOL_CAP = 1536`.

## 6.2 Stratégie si plein

- **Pool global plein**: refus lock, feedback UI bref “POOL FULL”.
- **Step à limite**: refus lock, feedback “STEP FULL”.
- Aucun eviction implicite (prévisible, safe).

## 6.3 Coût opérations

- **overwrite lock existant**: O(k_step), `k_step<=16`.
- **ajout lock**: O(1) alloc free-list + O(1) insertion tête + O(k_step) check existence.
- **suppression lock**: O(k_step) dans liste chaînée locale step.
- **copy/paste**: O(n_locks_copiés) avec check compat par lock.

## 6.4 Compactage

- V1: **pas de compactage global**.
- Justification: simplicité/déterminisme/debug.
- La free-list absorbe churn normal sans coût caché.

## 6.5 Indexation recommandée

- Par step: liste chaînée dans pool (`lock_head + next`).
- Pas d’index hash global en V1.
- Pourquoi: `k_step` borné à 16 => scan local rapide et simple.

---

## 7. Recommandation ferme sur le max de p-locks par step

## Recommandation V1: **16 locks/step**

### Pourquoi 16

- 8 est trop serré pour combiner COLORS+TONE sur un même step dans un usage réel.
- 32 augmente coût CPU (scans/restores/copy) et pression pool inutilement en V1.
- 16 donne une marge créative solide avec coût borné simple à debugger.

### Ce qu’on gagne

- Bonne expressivité p-lock sans exploser la RAM.
- O(k_step) reste court et stable.
- Clipboard/copy restent compacts.

### Ce qu’on perd

- Cas extrêmes “locker presque tout” d’un step impossible (volontaire en V1).

### Chemin d’évolution

- Garder constantes versionnées (`SEQ_STEP_MAX_LOCKS`, `SEQ_PLOCK_POOL_CAP`).
- V2 possible vers 24/32 si profiling réel justifie.

---

## 8. Multi-step edit détaillé

## 8.1 Step de référence

- Référence = **plus petit index step tenu** au moment du premier mouvement encodeur.
- figée tant que sélection courante reste non vide.

## 8.2 Valeur affichée

- Afficher valeur du step référence pour le param édité.
- Indicateur visuel “multi” si >1 step tenu.

## 8.3 Offset relatif

Pour chaque step sélectionné:
- si lock existant: `new = old + delta_ref`.
- si lock absent: créer lock avec `base_or_ref + delta_ref`.
  - `base_or_ref` recommandé = valeur base track (comportement stable) ;
  - variante acceptable = valeur du ref step avant delta.

Décision V1: **base track** pour lock absent (moins surprenant inter-steps hétérogènes).

## 8.4 Clamps

- Clamp individuel par param via min/max metadata.
- Pas de compensation entre steps (chacun clampé indépendamment).

## 8.5 Suppression lock multi-step

Action explicite (pas implicite quand valeur == base):
- si commande “clear lock param” sur sélection, supprimer lock de ce param pour tous steps tenus.
- si step devient lock_count=0 et trig=0 => step vide.

## 8.6 Anti-surprise utilisateur

- Pas d’auto-delete lock quand valeur revient numériquement à base (évite flicker logique).
- Messages clairs si refus (`STEP FULL`, `POOL FULL`).

---

## 9. Copy/paste de steps détaillé

## 9.1 COPY

- Précondition: ≥1 step tenu.
- Sauver:
  - `src_track`
  - `src_step_min`
  - liste offsets relatifs (`rel_pos[]`) des steps sélectionnés (contigus ou non)
  - meta step + locks associés

## 9.2 PASTE (SHIFT+COPY selon décision figée)

- Précondition: clipboard valide + ≥1 step tenu sur cible.
- `dst_anchor = min(steps tenus cible)`.
- Pour chaque entrée clipboard:
  - `dst = dst_anchor + rel_pos[i]`.
  - si `dst >= track_length` => ignorer (pas de wrap V1).

## 9.3 Paste autre track

- Trig copié tel quel.
- Pour chaque lock:
  - si `seq_param_is_supported_on_track(dst_track,param8)` => copier
  - sinon ignorer + incrément compteur incompatibles

## 9.4 Feedback

- Si au moins un lock ignoré: “PASTE PARTIAL”.
- Si tout collé: “PASTE OK”.
- Si troncature hors longueur: “PASTE TRUNC”.

## 9.5 Règle de simplicité

- Toujours conserver positions relatives depuis l’ancre source.
- Pas de remapping intelligent compliqué en V1.

---

## 10. Transport/runtime détaillé

## 10.1 Start/stop global

- Sur événement bouton `BTN_PLAY` (dans pipeline UI events):
  - si stopped -> running=1, reset phase (ou continue selon choix produit),
  - si running -> running=0.

## 10.2 Avance tracks longueurs différentes

- Global clock commune.
- Chaque track fait `step = (step + 1) % length_steps`.
- Le changement de track active UI n’impacte pas lecture.

## 10.3 Clock interne

- Baser l’avance sur domaine engine/audio.
- Recommandé: exposer compteur frames total depuis `engine_tasklet` (ou compteur ticks + accumulateur fixe q32).
- Pas de dépendance `HAL_GetTick()` pour séquenceur.

## 10.4 Clock MIDI externe (futur)

- Ajouter injecteur d’impulsions transport (`seq_transport_on_midi_clock/start/continue/stop`).
- Le moteur step consomme une interface uniforme `seq_clock_source` (INT/EXT).

## 10.5 Point d’accroche exact

- `brick6_app_process()`:
  1. `engine_tasklet_poll()` (déjà)
  2. `seq_runtime_process()` (**nouveau**, avant UI render)
  3. reste pipeline actuel

Pourquoi:
- déjà central, cadence stable, pas d’IRQ lourde.

## 10.6 Pourquoi `engine_tasklet` est le bon point

- Déjà aligné audio, déterministe, utilisé par transport recorder.
- Évite jitter de SysTick généraliste.

---

## 11. UI SEQ minimale recommandée

Ne pas surdesigner pages. Minimum utile:

1. **Hall mode SEQ** dans `ui_core`:
   - trigger SHIFT+HALL11,
   - double tap HALL11 ouvre page SEQ.
2. **Une page template SEQ minimale**:
   - juste les paramètres strictement nécessaires V1 (Length, éventuellement Rate/ClockSrc si déjà branchés).
3. **Hooks UI->SEQ**:
   - transpose +/- en SEQ => page visible track.
   - halls en SEQ => toggle step.
4. **API UI minimale**:

```c
uint8_t seq_ui_get_visible_page(uint8_t track);
void    seq_ui_set_visible_page(uint8_t track, uint8_t page);
void    seq_edit_toggle_step(uint8_t track, uint8_t step_idx);
uint8_t seq_step_has_trig(uint8_t track, uint8_t step_idx);
```

Ce qu’il faut laisser ouvert:
- pages avancées,
- macro-édition,
- realtime record notes/trigs.

---

## 12. Architecture LED détaillée

## 12.1 Rendu V1

- `LED_LAYER_SEQ_STATE`:
  - step trig actif sur page visible => vert.
- `LED_LAYER_SEQ_CURSOR`:
  - playhead si step courant dans page visible => blanc.
- `LED_LAYER_UI`:
  - scène existante boutons param + autres états UI.

Priorité visuelle:
- curseur (blanc) doit dominer trig vert sur même LED.
- implémentation simple: dans couche cursor, valeur blanche suffisamment haute + composition saturée.

## 12.2 Futur couleurs combinées

Recommandation: **`lock_set_mask` par step** (bits ensembles lockés), pas couleur persistée finale.

Pourquoi dériver couleur au rendu:
- palette peut évoluer sans migration des projets.
- évite incohérences si règles de couleur changent.
- données persistées restent sémantiques, pas UI-coupled.

## 12.3 Branchage dans architecture actuelle

- Garder `led_rgb.c` autorité de composition fixe.
- Ajouter helper `seq_led_render_page(track, page, ...)` appelé depuis `led_apply_fixed_scene()` quand mode SEQ.
- Réutiliser remap existant `led_remap_led_for_hall()`.

---

## 13. Persistance recommandée

## 13.1 Ownership

- Nouveau module `seq_persistence` propriétaire du format sérialisé séquenceur.
- `seq_project_data_t` = unité atomique V1 (1 séquence/track).

## 13.2 Où brancher

- Boot init: dans `brick6_app_init` (après init storage), charger `seq_project_data_t`.
- Save: via action explicite projet (ou autosave futur), hors chemin audio critique.

## 13.3 Format minimal V1

Header:

```c
typedef struct {
    uint32_t magic;      // 'BSEQ'
    uint16_t version;    // 1
    uint16_t payload_sz;
    uint16_t crc16;
    uint16_t _rsv;
} seq_file_header_t;
```

Payload:
- `seq_project_data_t` (tracks + pool).

## 13.4 Versioning

- Rejet si magic/version invalides -> init defaults.
- V2+ : migration explicite `v1->v2` par fonction dédiée.

## 13.5 Anticipation mémoire de séquences réutilisables

- Ne pas casser V1: garder payload “project current seqs”.
- Ajouter plus tard section optionnelle `pattern_bank` versionnée après payload principal.

---

## 14. Plan d’implémentation mis à jour, plus concret

### Étape A — Infrastructure data + runtime vide

**Nouveaux fichiers**
- `Inc/Seq/seq_types.h` (constantes/types)
- `Inc/Seq/seq_model.h`, `Src/Seq/seq_model.c`
- `Inc/Seq/seq_runtime.h`, `Src/Seq/seq_runtime.c`
- `Inc/Seq/seq_edit.h`, `Src/Seq/seq_edit.c`
- `Inc/Seq/seq_param_iface.h`, `Src/Seq/seq_param_iface.c`
- `Inc/Seq/seq_transport.h`, `Src/Seq/seq_transport.c`
- `Inc/Seq/seq_clipboard.h`, `Src/Seq/seq_clipboard.c`
- `Inc/Seq/seq_led.h`, `Src/Seq/seq_led.c`
- `Inc/Seq/seq_persistence.h`, `Src/Seq/seq_persistence.c`

**Fichiers existants à modifier**
- `Src/Core/brick6_app_init.c` (init seq)
- `Src/Core/brick6_app_init.c` / `brick6_app_process` (appel process seq)
- `Src/UI/ui_core.c/h` (HALL11 + transpose page SEQ)
- `Src/UI/ui_navigation.c` + `ui_page_manager` (page SEQ)
- `Src/UI/ui_template_page.c/h` (family SEQ + flag p-lockable)
- `Src/UI/ui_param.c` (redir édition vers seq_edit en mode step-hold)
- `Drivers/Drv_app/Src/led_rgb.c` (rendu seq layers)
- `Src/Param/param_registry.c` (API apply explicite par track pour params p-lockables)

### Étape B — Features minimales visibles

1. Mode SEQ activable (SHIFT+HALL11) + page visible par track.
2. Toggle trig halls 16 steps page courante.
3. LED trig vert + playhead blanc page-aware.
4. Transport PLAY start/stop global.

### Étape C — P-locks robustes

5. Pool global + limites.
6. Multi-step offset relatif.
7. Copy/paste steps + feedback partiel.
8. Persistance V1.

### Points de vigilance par fichier

- `ui_core.c`: ne pas casser SHIFT-before-HALL ni KEYBOARD/ARP.
- `ui_param.c`: éviter ambiguïté édition normale vs édition locks.
- `param_registry.c`: retirer dépendances à active track pour apply lock runtime.
- `led_rgb.c`: coût CPU constant, pas de logique lourde par frame inutile.
- `seq_runtime.c`: aucune allocation, section critique minimale, ordre apply/restaure stable.

---

## 15. Questions restantes, uniquement si elles sont réellement bloquantes

1. **Mapping param8 compact**: valider table V1 exacte des params lockables COLORS/TONE (index stable persisté).  
   (Bloquant car impact format fichier et compat future.)
2. **Politique start**: au PLAY, repartir step 0 ou reprendre position précédente ?  
   (Bloquant UX/transport, impact tests et attentes utilisateur.)

