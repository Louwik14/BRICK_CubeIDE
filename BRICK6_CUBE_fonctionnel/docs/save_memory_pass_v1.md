# V1 Save Architecture — mini passe de durcissement (pré-implémentation)

## 1) Forme finale recommandée de `PatternSaveV1`

```c
#define PATTERN_V1_MAGIC 0x31565042UL /* 'BPV1' */

#define PATTERN_V1_SOUND_PARAM_COUNT 66U
#define PATTERN_V1_TRACK_MIX_PARAM_COUNT 4U   /* MIX_LEVEL/PAN/SEND1/SEND2 */
#define PATTERN_V1_GLOBAL_MIX_PARAM_COUNT 40U /* PARAM_MIX_TRACK0_* .. PARAM_MIX_SEND2 */
#define PATTERN_V1_GLOBAL_CFG_PARAM_COUNT 8U  /* PARAM_CFG_* */
#define PATTERN_V1_GLOBAL_SYS_PARAM_COUNT 2U  /* POST_GAIN, OUTPUT_COMP */

typedef struct
{
    uint8_t family[SEQ_TRACK_COUNT];
    uint8_t type[SEQ_TRACK_COUNT];
    uint8_t midi_channel[SEQ_TRACK_COUNT];
    uint8_t midi_source[SEQ_TRACK_COUNT];
} pattern_v1_track_cfg_block_t;

typedef struct
{
    /* IDs figés en header + valeurs par track.
       Bloc SOUND = domaines COLORS + TONE + PLAY (track_runtime rule). */
    uint16_t param_ids[PATTERN_V1_SOUND_PARAM_COUNT];
    float values[SEQ_TRACK_COUNT][PATTERN_V1_SOUND_PARAM_COUNT];
    uint8_t valid[SEQ_TRACK_COUNT][PATTERN_V1_SOUND_PARAM_COUNT];
} pattern_v1_sound_block_t;

typedef struct
{
    /* Mix track-aware runtime (MIX_LEVEL/PAN/SEND1/SEND2) */
    uint16_t track_param_ids[PATTERN_V1_TRACK_MIX_PARAM_COUNT];
    float track_values[SEQ_TRACK_COUNT][PATTERN_V1_TRACK_MIX_PARAM_COUNT];
    uint8_t track_valid[SEQ_TRACK_COUNT][PATTERN_V1_TRACK_MIX_PARAM_COUNT];

    /* Mix global historique (PARAM_MIX_TRACK0_* .. PARAM_MIX_SEND2) */
    uint16_t global_mix_param_ids[PATTERN_V1_GLOBAL_MIX_PARAM_COUNT];
    float global_mix_values[PATTERN_V1_GLOBAL_MIX_PARAM_COUNT];
    uint8_t global_mix_valid[PATTERN_V1_GLOBAL_MIX_PARAM_COUNT];
} pattern_v1_mix_block_t;

typedef struct
{
    /* Globals utiles rappel musical */
    uint16_t cfg_param_ids[PATTERN_V1_GLOBAL_CFG_PARAM_COUNT];
    float cfg_values[PATTERN_V1_GLOBAL_CFG_PARAM_COUNT];
    uint8_t cfg_valid[PATTERN_V1_GLOBAL_CFG_PARAM_COUNT];

    uint16_t sys_param_ids[PATTERN_V1_GLOBAL_SYS_PARAM_COUNT];
    float sys_values[PATTERN_V1_GLOBAL_SYS_PARAM_COUNT];
    uint8_t sys_valid[PATTERN_V1_GLOBAL_SYS_PARAM_COUNT];

    /* Snapshot direct runtime seq utiles */
    uint32_t tempo_bpm_milli;
    uint8_t clock_src;
    uint8_t rec_count_in_mode;
    uint8_t rec_len_mode;
    uint8_t track_div[SEQ_TRACK_COUNT];
    uint8_t track_quant[SEQ_TRACK_COUNT];
    uint8_t track_swing[SEQ_TRACK_COUNT];
} pattern_v1_globals_block_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;

    /* Bloc SEQ (autorité existante) */
    seq_project_data_t seq;

    /* Bloc TRACK CONFIG */
    pattern_v1_track_cfg_block_t track_cfg;

    /* Bloc SOUND */
    pattern_v1_sound_block_t sound;

    /* Bloc MIX */
    pattern_v1_mix_block_t mix;

    /* Bloc GLOBALS */
    pattern_v1_globals_block_t globals;

    /* Futur recall partiel / kit-like */
    uint8_t default_recall_mask; /* bit0=SEQ bit1=CFG bit2=SOUND bit3=MIX bit4=GLOBALS */
    uint8_t reserved1[15];

    uint32_t crc32;
} PatternSaveV1;
```

### Pourquoi cette forme (durcie)
- On évite le “sac opaque” `track x PARAM_COUNT` en séparant **SOUND/MIX/GLOBALS** selon les domaines réels du repo.
- On reste **worst-case fixe V1**: tableaux statiques, tailles constantes, pas de compactage.
- Les IDs sont figés par bloc => évolution future recall partiel / kit-like sans casser la structure de haut niveau.

---

## 2) Ordre exact de `pattern_apply_to_live()`

Ordre recommandé (autorités réelles repo + robustesse runtime):

1. **Guards d’entrée**
   - refuser si `in_irq != 0`
   - refuser si `save_apply_in_progress != 0`
   - poser `save_apply_in_progress=1`, `undo_capture_suspended=1`
2. **Geler transport / sorties**
   - mémoriser `was_running = seq_runtime_is_running()`
   - `seq_runtime_stop()`
   - `seq_output_guard_panic(1)` pour nettoyer notes actives
3. **Apply Track Config (bloc track_cfg)**
   - loop tracks: `ui_set_track_family`, `ui_set_track_type`, `ui_set_track_midi_channel`, `ui_set_track_midi_source`
4. **Refresh binding runtime**
   - `track_runtime_refresh_all()`
5. **Apply SOUND (bloc sound)**
   - loop track + param list sound:
     - si `valid=1`: `param_registry_apply_track_value(id, track, value)`
     - si refus (inapplicable), ignorer + compteur de skip
6. **Apply MIX (bloc mix)**
   - track mix: `param_registry_apply_track_value`
   - mix global: `param_set(id, value)`
7. **Apply GLOBALS (bloc globals)**
   - `seq_runtime_set_tempo_bpm_milli`, `seq_runtime_set_clock_source`, `seq_runtime_set_rec_*`
   - `seq_runtime_set_track_div/quant/swing`
   - `param_set` pour cfg/sys autorisés
   - **ne pas appliquer `PARAM_MASTER_GAIN`** (autorité pot master runtime)
8. **Apply SEQ (bloc seq)**
   - `seq_model_load_project(&pattern->seq)`
9. **Reset / resync runtime dérivable**
   - playhead reset loop `seq_runtime_set_playhead_step(track,0)`
   - `param_registry_sync_ui_for_active_track()`
10. **Reprise éventuelle**
   - si option `resume_transport` && `was_running`: `seq_runtime_start()`
11. **Sortie propre**
   - `undo_capture_suspended=0`, `save_apply_in_progress=0`

Raison clé d’ordre: **cfg track avant params track** (sinon params rejetés par binding), **seq après params** pour éviter interactions d’édition/transitions pendant binding.

---

## 3) API minimales recommandées

```c
typedef struct
{
    uint8_t resume_transport; /* 0/1 */
    uint8_t allow_partial;    /* 0/1, V1=0 par défaut */
    uint8_t recall_mask;      /* optionnel, V1 peut ignorer si allow_partial=0 */
} pattern_apply_opts_t;

typedef struct
{
    uint16_t skipped_params;
    uint16_t rejected_params;
} pattern_apply_report_t;

uint8_t pattern_save_v1_capture_from_live(PatternSaveV1 *out_pattern);
uint8_t pattern_save_v1_apply_to_live(const PatternSaveV1 *pattern,
                                      const pattern_apply_opts_t *opts,
                                      pattern_apply_report_t *out_report);

uint8_t project_save_v1_capture_live(ProjectSaveV1 *out_project);
uint8_t project_save_v1_restore_live(const ProjectSaveV1 *project,
                                     uint8_t restore_active_slot,
                                     uint8_t apply_active_slot_now);

uint8_t undo_v1_capture_before_edit(uint8_t source_slot);
uint8_t undo_v1_restore(uint8_t resume_transport);
```

---

## 4) Garde-fous indispensables

1. **Pas d’undo capture pendant load/recall**
   - drapeau global `undo_capture_suspended`
   - `undo_v1_capture_before_edit()` retourne 0 si suspendu
2. **Pas d’apply/capture en IRQ**
   - helper `save_v1_is_in_irq()` (`SCB->ICSR & VECTACTIVE`)
   - APIs save/restore retournent erreur en IRQ
3. **Params inapplicables family/type**
   - toujours appliquer via `param_registry_apply_track_value()` (jamais bypass)
   - compter/reporter les rejects, ne pas hard-fail global
4. **Conflits master/global controls**
   - exclure `PARAM_MASTER_GAIN` du recall pattern
   - `POST_GAIN` / `OUTPUT_COMP` autorisés si marqués valides
5. **Single-writer during apply**
   - `save_apply_in_progress` protège contre réentrance
   - suspendre triggers d’undo et commandes save concurrentes
6. **Transport safety window**
   - recall uniquement transport stoppé (ou stop forcé interne)
   - nettoyage notes actives obligatoire avant reprise

---

## 5) Fichiers à créer / modifier en premier

## À créer (phase 1)
- `Inc/Storage/pattern_save_v1.h`
- `Src/Storage/pattern_save_v1.c`
- `Inc/Storage/project_save_v1.h`
- `Src/Storage/project_save_v1.c`
- `Inc/Storage/undo_v1.h`
- `Src/Storage/undo_v1.c`

## À modifier en premier (phase 1)
- `Src/Seq/seq_edit.c` (hooks `undo_v1_capture_before_edit` sur opérations édition)
- `Src/Core/brick6_app_init.c` (init des modules save/undo)

## À modifier ensuite (phase 2/3)
- `Src/Seq/seq_persistence.c` (ou remplacement progressif par project/pattern persistence V1)
- `Src/UI/...` (commandes save/load/recall sans toucher IRQ audio)

