# BRICK — REGISTRE D’AUDIT CONTROL / AUDIO

## Statut

Ce document centralise les anomalies **déjà découvertes** dans le secteur CONTROL / AUDIO.

But des prochains audits :

- ne pas recompter les problèmes déjà listés ici ;
- chercher uniquement de nouvelles violations indépendantes ;
- si une nouvelle anomalie est démontrée, l’ajouter à ce document à la fin de l’audit ;
- conserver les zones explicitement validées comme propres afin d’éviter de les rouvrir sans nouvelle preuve.

Dernière consolidation : **après ROUND 5**.

---

# 1. Architecture cible

Après démarrage du scheduler :

```text
UI_SERVICE ─────┐
USB_SERVICE ────┤
STORAGE_IO ─────┤
Hall / MIDI ────┤
                ▼
            CONTROL_RT
                │
                ▼
       FIFO CONTROL→AUDIO
                │
                ▼
           AUDIO owner
```

Invariants :

- `CONTROL_RT` est l’unique producer final CONTROL→AUDIO ;
- la FIFO reste SPSC ;
- l’horizon CONTROL est mono-owner ;
- Audio IRQ ne doit jamais attendre qu’une tâche moins prioritaire progresse ;
- un état Audio hot ne doit pas être modifié concurremment par IRQ et background ;
- le background peut préparer, mais l’application finale d’une mutation hot doit être sérialisée côté Audio ;
- aucune rustine de type mutex global, retry, resync, pending caché ou clamp temporel.

---

# 2. CONTROL→AUDIO — multi-producer réel

La FIFO CONTROL→AUDIO est supposée SPSC mais est atteinte depuis plusieurs contexts :

- `CONTROL_RT`
- `UI_SERVICE`
- `STORAGE_IO`

Exemples déjà trouvés :

### UI_SERVICE
- encoder audio ;
- Audio Rec / rec-bus ;
- preview gain ;
- preview stop ;
- visual requests ;
- CFG restore ;
- Audio FX ;
- certaines publications param/audio.

### STORAGE_IO
- preview active ;
- sampler retirement ;
- RAM sampler retirement ;
- wavetable retirement ;
- multi-sample retirement ;
- recorder stop/service.

Conséquences :

- corruption possible de `head` ;
- ordre temporel non garanti ;
- horizon intercalable ;
- incompatibilité directe avec la cible H747.

Classification : `P0 / REQUIRED IN SAME REFACTOR`

---

# 3. Horizon CONTROL — multi-writer

Structure :

```text
g_control_audio_horizon
```

Writers réels déjà démontrés :

- CONTROL_RT
- UI_SERVICE
- STORAGE_IO

Risques :

- compteur incohérent ;
- staging intercalé ;
- ordre temporel cassé ;
- commit d’un horizon contenant des commandes provenant d’un autre contexte.

Invariant cible :

```text
CONTROL_RT
→ begin
→ stage
→ commit / abort
```

Aucun autre writer.

Classification : `P0 / REQUIRED IN SAME REFACTOR`

---

# 4. Temporalité CONTROL→AUDIO

Trois sémantiques légitimes :

## `captured`
Uniquement pour un vrai ingress dont le timestamp est pris à la source.

Exemples légitimes :
- Hall ;
- MIDI ;
- encoder.

## `now`
Mutation CONTROL immédiate.

## `scheduled`
Événement explicitement daté.

## Timestamp parent
Une même transition dépendante doit partager un timestamp parent résolu une seule fois.

Cas démontré :

```text
PROGRAM(T)
PARAM initiaux(T)
MIDI_CONFIG(T)
```

Faux `captured` déjà identifiés dans notamment :

- VCA ;
- mute ;
- tone program ;
- polyphony ;
- mixer ;
- Audio FX ;
- filter ;
- macro ;
- ENV3 ;
- Matrix ;
- global param ;
- clipboard ;
- persistent patch ;
- persistent pattern ;
- CFG restore.

Règle :

> un contrôleur métier ne doit pas appeler `live_clock_capture_tick()` pour fabriquer un faux instant d’ingress.

Classification : `P0/P1 — REQUIRED IN SAME REFACTOR`

---

# 5. AUDIO_BG_LOCAL ↔ Audio IRQ — états hot partagés

## `g_looper_tracks`
- AUDIO_BG peut modifier le runtime looper ;
- Audio IRQ lit/modifie/rend le même état.

Classification : `P0/P1 — BEFORE H747`

## `g_sampler_multi_voice`
- background sampler service ;
- Audio IRQ render/executor.

Classification : `P0/P1 — BEFORE H747`

## `g_sampler_multi_stream_release_pending`
- background et IRQ peuvent modifier/consommer l’état.

Classification : `P0/P1 — BEFORE H747`

## `g_multi_voice_dsp_pool`
Découvert Round 3.

- background appelle des resets complets ;
- Audio IRQ lit/modifie le même slot ;
- `multi_voice_dsp_reset_slot()` effectue `memset` + reset multi-champs.

Classification : `P1 — BEFORE H747`

## `g_looper_playing_mask`
Découvert Round 5.

- `AUDIO_BG` peut modifier via `looper_update_ready_state()`;
- Audio IRQ consomme en parallèle ;
- masque et état peuvent diverger ;
- dropouts possibles.

Classification : `P1 — BEFORE H747`

---

# 6. Seqlocks / snapshots Audio dangereux

## Sample classic projection

Structure :

```text
g_sample_classic_audio_source[]
```

Problème :
- writer STORAGE passe le compteur en impair ;
- Audio IRQ peut préempter le writer ;
- reader IRQ boucle jusqu’à stabilité ;
- writer ne peut plus progresser tant que l’IRQ tourne.

Risque : livelock / blocage Audio IRQ.

Classification : `P0`

## Wavetable registry

Structure :

```text
g_audio_wavetable_registry[]
```

Même défaut :
- seqlock reader IRQ non borné ;
- writer Storage préemptable.

Classification : `P0`

---

# 7. `g_audio_sample_clock` — lecture 64 bits non atomique

Découvert Round 4.

- writer : Audio IRQ ;
- reader : background ;
- lecture 64 bits non snapshotée ;
- possibilité de valeur déchirée lors d’un rollover 32 bits ;
- impact possible sur timing Looper.

Classification : `P1 — BEFORE H747`

---

# 8. Pools / caches / assets mutables exposés

Structures déjà identifiées :

```text
g_sample_global_pool
g_sample_page_cache_state
g_sample_cache[]
g_sampler_ram_pool
g_wavetable_pool
g_multi_instruments[]
g_multi_samples[]
g_multi_zones[]
```

Problèmes :

- UI/CONTROL peuvent lire des structures directement ;
- STORAGE peut reset/recycle/remplacer ;
- absence de snapshot/lease clair ;
- pointeurs directs ;
- lifetime non explicitement garanti.

Classification : `P1 — BEFORE H747`

---

# 9. Résultats async RAM / Wavetable

Défaut déjà démontré :

- plusieurs contexts peuvent appeler `*_take_result()`;
- le premier consommateur remet l’état à `IDLE`;
- les autres perdent le résultat ;
- pas de requester/token/owner unique.

Classification : `P1 — BEFORE H747`

---

# 10. Recorder facade — ownership mixte

Structure :

```text
g_audio_recorder
```

Writers déjà démontrés :

- UI
- CONTROL
- STORAGE

Le même agrégat mélange :

- session/control ;
- admission ;
- état physique Storage ;
- stop/cancel ;
- préparation.

Le ring PCM Audio→Storage est **propre** et ne doit pas être refondu.

Classification : `P1`

---

# 11. `sd_preview` — ownership mixte

Structure :

```text
g_sd_preview
```

Writers déjà démontrés :

- UI_SERVICE
- CONTROL_RT
- STORAGE_IO

Le ring PCM preview STORAGE→AUDIO est **SPSC propre**.

Le problème concerne :

- phase ;
- session ;
- gain ;
- path ;
- stop/start ;
- publication métier.

Classification : `P1`

---

# 12. `control_music_output` — lecteur cross-domain ambigu

Structure :

```text
g_control_music_outputs
```

État connu :

- writer CONTROL unique ;
- readers CONTROL/UI/STORAGE ;
- pas de second writer démontré ;
- possibilité de lecture pendant mutation/window CONTROL non totalement prouvée.

Classification : `AMBIGUOUS`

Ne pas patcher sans preuve supplémentaire.

---

# 13. Zones explicitement validées comme propres

À ne pas rouvrir sans preuve :

- track runtime ;
- param state normal ;
- routing ;
- mute ;
- EXT ownership ;
- NOTE ;
- PROGRAM normal ;
- TRANSPORT ;
- executor principal ;
- mixer ;
- voices ;
- polyphony runtime principal hors leaks listés ;
- engine dispatch ;
- FX principal hors UI ownership ;
- Hall event queues SPSC ;
- recorder PCM ring ;
- preview PCM ring ;
- streamer data path ;
- modulation/FX/mixer hors anomalies connues ;
- boucles IRQ sans autre attente non bornée identifiée au Round 5.

---

# 14. Règles d’audit suivantes

Lors d’un nouveau round CONTROL/AUDIO :

1. lire ce document avant toute recherche ;
2. ne pas recompter les anomalies listées ;
3. rechercher uniquement :
   - nouvelle structure ;
   - nouveau writer ;
   - nouvelle race indépendante ;
   - nouvel invariant manquant ;
4. si une anomalie connue a une conséquence nouvelle, préciser ce qui est réellement nouveau ;
5. mettre à jour ce document à la fin.

---

# 15. Nouvelles découvertes à ajouter

```text
## ROUND N

### ID — classification
- Structure :
- Writers :
- Readers :
- Cause :
- Impact :
- H747 :
```

## ROUND 6

### R6-01 — init Audio modulation lazy dans l'IRQ — `P1`

- Structure/fonctions : `g_mod_lfo_audio_config`, `g_mod_lfo_runtime`, les
  banques poly LFO, `g_mod_env3_audio_config`, `g_mod_env3_runtime` et leurs
  flags d'initialisation ; `mod_lfo_v1_process_block()`,
  `mod_lfo_v1_set_track_param_audio()` et les entrées Audio ENV3.
- Writers/readers : l'initialisation complète (`memset`, initialisation ADSR,
  valeurs RNG) est déclenchée depuis l'Audio IRQ lorsque le flag est nul ; le
  runtime est ensuite lu et modifié par cette même IRQ. `audio_domain_init()`
  ne réalise pas ces initialisations Audio.
- Invariant cassé : tout état Audio hot doit être initialisé par son owner
  avant l'activation de l'IRQ ; aucune initialisation globale lazy dans le
  chemin hard realtime.
- Impact H743 : premier bloc, premier note-on ou première automation peut
  exécuter une remise à zéro de banque dans l'IRQ, avec coût non budgété et
  risque de perdre un état Audio déjà préparé.
- Impact H747 : owner naturel M7 ; pas un blocage principalement physique,
  mais le correctif doit être absorbé par le refactor global de l'owner Audio
  (ou appliqué comme correctif local indépendant avant celui-ci).

### R6-02 — publication des leases Looper à deux writers Audio — `P1`

- Structure/fonctions : `g_sample_page_leases`,
  `sample_page_lease_audio_publish()/clear()`,
  `looper_update_primary_lease()`.
- Writers/readers : `brick6_looper_runtime_service()` écrit depuis le
  background Audio ; le rendu Looper écrit depuis l'Audio IRQ sur les mêmes
  slots. STORAGE lit via `sample_page_lease_control_read()` et les helpers
  `protects/references_key/all_released`.
- Invariant cassé : le seqlock de lease suppose un writer unique. Un writer
  background peut être préempté après la mise en séquence impaire par l'IRQ,
  puis terminer avec une ancienne valeur et écraser la lease publiée par
  l'IRQ.
- Impact H743 : lease incohérente ou périmée ; le recyclage du page-cache peut
  ne plus voir une page référencée, avec risque de backing réutilisé,
  underrun ou lecture d'une page étrangère.
- Impact H747 : owner naturel M7 Audio pour la publication ; le port physique
  ne supprime pas le problème si le service reste sur M7. Il faut sérialiser
  l'écriture dans le refactor global, pas concevoir un nouvel IPC ici.

### R6-03 — seqlock diagnostic boot à deux writers — `P1 local`

- Structure/fonctions : `g_audio_diag`, `g_audio_boot_diag_layout`,
  `audio_boot_diag_producer_publish_state()` et
  `audio_boot_diag_producer_publish_cpu()`.
- Writers/readers : le bootstrap (`audio_start()`, après activation possible
  du RX DMA) publie l'état ; les callbacks Audio IRQ publient périodiquement
  la charge CPU ; l'UI lit via `audio_boot_diag_read()`.
- Invariant cassé : le publisher seqlock doit avoir un writer sérialisé. Le
  writer bootstrap peut être préempté par le writer IRQ, puis réécrire une
  séquence et un snapshot calculés avant la préemption.
- Impact H743 : snapshot diagnostic perdu ou incohérent, séquence pouvant
  provoquer des lectures invalidées ; pas d'effet direct démontré sur le DSP.
- Impact H747 : producer naturel M7 ; pas de blocage principalement physique.
  Correctif local indépendant de synchronisation, à conserver dans le
  refactor global.

## ROUND 7

### R7-01 — calibration lazy de l'horloge sample dans l'IRQ — `NEW INSTANCE OF KNOWN RULE` — `P1`

- Structure/fonctions : `g_audio_sample_clock`,
  `g_audio_sample_clock_valid`, `audio_sample_clock_init_on_first_callback()`
  et les callbacks `HAL_SAI_RxHalfCpltCallback()` / `HAL_SAI_RxCpltCallback()`.
- Writers/readers : `audio_boot_init_binding_io()` remet l'horloge à zéro
  avant le démarrage ; le premier callback RX calcule et publie la valeur
  initiale ; l'IRQ la fait ensuite progresser. Les lectures background de la
  valeur 64 bits restent l'anomalie Round 4 distincte.
- Cause : après activation du DMA, le premier callback exécute
  `HAL_RCC_GetPCLK1Freq()`, lit `RCC`/`TIM5` et effectue une division 64 bits
  pour calibrer l'origine temporelle. Cette calibration n'est pas réalisée
  par le bootstrap Audio avant l'activation de l'IRQ.
- Invariant cassé : aucune initialisation ou calibration lazy non budgétée
  dans le chemin hard realtime ; le premier bloc doit entrer dans le même
  budget que les blocs suivants.
- Impact H743 actuel : premier half-buffer avec travail HAL/RCC/timer et
  arithmétique 64 bits supplémentaire, coût non inclus dans le budget nominal
  et origine temporelle dépendante d'une première activation runtime.
- Owner : M7 / Audio IRQ. Le problème est réel sur H743 et reste applicable
  au futur H747 ; à corriger avant le port ou à absorber dans le refactor de
  l'owner Audio. Aucun design IPC requis.
