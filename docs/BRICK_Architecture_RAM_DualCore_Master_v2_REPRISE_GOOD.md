# BRICK — Architecture RAM & Dual-Core figée
## Cahier des charges maître pour la refonte H743 → H747

**Version :** 2.0 — 18 août 2026  
**Statut :** architecture conceptuelle figée ; **reprise d’implémentation depuis le dernier état H743 matériellement GOOD**  
**Point de reprise Git :** `fcc6ceb092d423fa9403ff2c22511429d9bbaacc` — `stable avant refonte ram`  
**Branche/commit BAD de référence :** `e5b29b0df` — audio bruité + entrées Settings/Sample Browser buguées ; à conserver uniquement pour audit/diff, **jamais comme base de reprise**  
**Cible actuelle :** STM32H743 monocœur, même frontière logique CONTROL/AUDIO  
**Cible future :** STM32H747 dual-core, M4 CONTROL / M7 AUDIO  
**Règle de pilotage :** ce document sert de cahier des charges. Les futurs agents ne doivent pas rediscuter les décisions marquées **FIGÉ** sauf découverte d’une contradiction démontrée dans le code.

---

# 0. Point de reprise et leçons du chantier abandonné — OBLIGATOIRE

## 0.1 Référence matérielle

Le chantier mené le 18 août 2026 entre les deux checkpoints suivants est **abandonné comme implémentation** :

```text
fcc6ceb092d423fa9403ff2c22511429d9bbaacc
"stable avant refonte ram"
→ matériel : AUDIO PROPRE confirmé

        ↓ chantier regroupé sans checkpoints intermédiaires

e5b29b0df
"pas stable son bruité et menu beugué"
→ matériel : AUDIO BRUITÉ
→ entrée Settings buguée
→ entrée Sample Browser buguée
```

Le `reflog` ne montre aucun commit intermédiaire entre 15:43 et 19:43. Le commit BAD regroupe donc plusieurs passes sans possibilité de bisect fin.

**Règle absolue de reprise :** repartir du commit GOOD `fcc6ceb09...`. Le commit `e5b29b0df` peut être conservé sur une branche de sauvegarde et consulté comme source d’audits/diffs, mais ses patches ne sont pas considérés comme validés.

## 0.2 Ce qui est conservé du chantier BAD

On conserve **les conclusions d’architecture démontrées par les audits**, pas leur implémentation :

- ownership M4 CONTROL / M7 AUDIO ;
- décisions `FIGÉ` de ce document ;
- classification de `g_slot`, `g_mod_destination_cache`, `g_sampler_clip_slots`, `.sdram_audio_cold` ;
- architecture page-cache B, générations et quiescence ;
- séparation cible Looper/Recorder et Sampler/Multi ;
- cible p-lock 512 à valider ;
- nécessité de publications/events/plans pointer-free ;
- nécessité d’un boot AUDIO propriétaire ;
- nécessité de router les commandes UI waveform par une frontière CONTROL→AUDIO ;
- liste des bypass legacy dangereux identifiés.

On **ne conserve pas comme acquis** :

- Clean 1A ;
- contrats linker/RAM PASS 1 ;
- publications PASS 2 ;
- séparation Mixer/FX PASS 3 ;
- routing PASS 3B ;
- toute mesure MAP issue du commit BAD ;
- toute affirmation de fermeture de passe basée uniquement sur les builds du commit BAD.

Tout cela doit être réappliqué et revalidé depuis le commit GOOD, par petites passes.

## 0.3 Fuites découvertes à anticiper dès la reprise

L’audit global du chantier abandonné a révélé quatre familles de bypass à ne pas redécouvrir une par une :

1. **Boot FX global direct** : bootstrap CONTROL/Core → `fx_pool_init()` / `fx_pool_activate_slot()` → runtime DSP.
2. **Boot Mixer/master direct** : `mixer_init`, `mixer_set_master`, postgain/output compensation appelés directement par le bootstrap commun.
3. **Boot moteurs/binding direct** : init Drum/Braids/Stack/Wave/FM et binding physique déclenchés depuis le bootstrap commun sans entrée AUDIO propriétaire.
4. **UI waveform direct** : UI → `audio_waveform_capture_set_entity`, `set_fast_refresh`, `disable`.

Dette legacy identifiée :

- `param_backend_reapply_tone_{prism,fm,stack,wave}_runtime()` lit directement `track_tone_sound_state` puis modifie les moteurs ; aucun appelant actif observé lors de l’audit, mais API dangereuse à supprimer/neutraliser dans une passe dédiée ;
- ancien Matrix/rebuild et certains blocs `#if 0` dormants à classer et nettoyer après fermeture des frontières actives.

Les couplages **Looper/Recorder** et **Sampler/Multi/page-cache** restent des chantiers volontairement différés, pas des raisons de bloquer les premières passes.

## 0.4 Leçon de validation

Un build LC/Premium réussi n’est **pas** une preuve fonctionnelle suffisante. Le commit BAD compilait tout en présentant plusieurs régressions matérielles.

Après chaque passe structurelle, le user effectue un smoke test H743 minimal avant tout nouveau chantier :

- note synthé : son propre ;
- input LINE : son propre ;
- navigation UI de base ;
- entrée Settings ;
- entrée Sample Browser ;
- fonction directement touchée par la passe.

Si un de ces points régresse : **STOP immédiat**, audit du seul diff de la passe.

---

# 1. Objectif

La refonte RAM de BRICK n’a plus pour but de « trouver quelques kilo-octets » pour un effet particulier. Elle doit aligner la mémoire avec l’architecture logique déjà largement présente dans le firmware.

Le résultat recherché est :

- une D1 principalement consacrée au working set AUDIO M7 ;
- une D2 structurée par rôle réel plutôt que comme un bloc générique ;
- une D3 utilisée comme petite zone d’échange/IPC et non comme débarras ;
- une séparation stricte entre données CONTROL et runtime AUDIO ;
- aucune dégradation des chemins streaming déjà calibrés ;
- la même architecture logique sur H743 et H747 ;
- une migration vers le H747 qui soit principalement une matérialisation physique des frontières déjà testées sur H743.

---

# 2. Architecture générale — FIGÉ

```text
M4 CONTROL
├─ UI
├─ MIDI / clavier
├─ séquenceur / PLAY / NoteFX
├─ ParamStore et états canoniques
├─ configuration modulation
├─ configuration mixer / FX
├─ routing logique / binding intent
├─ catalogues sampler / Multi
├─ chargement / SD / FatFs / CLMT
└─ persistence

        ↓ événements horodatés
        ↓ snapshots spécialisés
        ↓ plans compilés
        ↓ handles / tokens / générations

M7 AUDIO
├─ IRQ audio
├─ synth engines / voices
├─ LFO / ENV3 / Matrix runtime
├─ mixer / filters / VCA
├─ FX et historiques DSP
├─ sampler / Multi playback
├─ looper playback / capture
├─ page-cache runtime
└─ lecteurs / état audio local
```

## Règles fondamentales — FIGÉ

1. **M4 est l’autorité CONTROL.**
2. **M7 est l’autorité AUDIO.**
3. Le M7 ne doit pas lire directement une structure CONTROL mutable.
4. Le M7 ne doit pas réécrire une autorité CONTROL pour « rester synchronisé ».
5. Le M4 ne doit pas lire directement les états DSP internes du M7.
6. Les échanges inter-domaines sont pointer-free autant que possible.
7. Les gros états DSP ne traversent jamais la frontière.
8. Les valeurs fréquentes traversent comme événements horodatés.
9. Les changements structurels traversent comme petits snapshots versionnés.
10. Les traitements nécessitant préparation/résolution utilisent des plans compacts.
11. H743 et H747 doivent conserver les mêmes APIs et les mêmes frontières logiques.
12. Le H747 ne doit pas entraîner une deuxième architecture fonctionnelle.

---

# 3. Modèle de communication — FIGÉ

Trois mécanismes principaux sont retenus.

## 3.1 Événements horodatés

À utiliser pour :

- Note On / Note Off ;
- p-locks ;
- paramètres live ;
- gates ;
- transport nécessitant précision sample ;
- événements musicaux ;
- commandes ponctuelles.

Propriétés :

- pointer-free ;
- taille bornée ;
- génération / provenance si nécessaire ;
- `due_sample` ou équivalent ;
- aucune lecture secondaire dans une grosse structure CONTROL pour terminer l’application.

## 3.2 Snapshots spécialisés

À utiliser pour :

- configuration LFO / ENV ;
- routing ;
- binding ;
- configuration structurelle FX ;
- configuration structurelle mixer ;
- petits états transport/tempo ;
- publications AUDIO → CONTROL de faible débit.

Ne pas créer un gros snapshot complet de track.

## 3.3 Plans compilés

À utiliser lorsque le CONTROL peut préparer une représentation directement exploitable par l’AUDIO :

- Matrix ;
- routing ;
- source résolue Sampler/Multi ;
- autres mappings complexes.

Le M7 ne doit pas refaire un lookup dans ParamStore, catalogue ou pool mutable après réception du plan.

---

# 4. Latence cible — FIGÉ

À 48 kHz avec blocs de 64 samples :

- quantum audio : environ 1,33 ms ;
- événement publié à temps : application au sample demandé ;
- paramètre non sample-critical : application au plus tard au prochain segment/bloc ;
- changement structurel : au plus un bloc si publié à temps ;
- publication tardive : différée explicitement au quantum suivant ;
- aucun appel bloquant M4 depuis le rendu M7 ;
- aucun mutex/HSEM bloquant dans l’IRQ audio.

Le M7 doit toujours pouvoir produire le prochain bloc sans attendre le M4.

---

# 5. Séquenceur / PLAY / NoteFX — FIGÉ

## Ownership

**M4 :**

- `g_seq_project` ;
- PLAY ;
- scheduler ;
- occurrences ;
- output guards ;
- p-lock runtime ;
- pool p-lock ;
- NoteFX ;
- tempo/transport musical ;
- live-record CONTROL.

**M7 :**

- horloge sample locale ;
- consommation des événements datés ;
- admission physique ;
- voices ;
- rendu.

## Frontière

```text
M4 seq / PLAY / NoteFX
        ↓
events pointer-free + snapshot tempo/transport
        ↓
M7 audio
```

Le M7 ne reçoit ni steps bruts, ni PLAY brut, ni pool p-lock, ni état ARP/Euclid.

## `g_slot` — FIGÉ

`g_slot` NoteFX est **100 % CONTROL** dans la cible.

Le moteur NoteFX produit les notes finales via la queue audio. Aucun runtime algorithmique ARP/Euclid n’est requis côté M7.

## `g_seq_project` — FIGÉ

`g_seq_project` doit rester en SRAM interne dans l’architecture cible.  
La piste `g_seq_project → SDRAM + cache lookahead` est abandonnée comme solution par défaut.

Raison : le modèle et le scheduler ont des accès fréquents et aléatoires ; conserver le modèle actif en SRAM est plus simple et plus déterministe.

---

# 6. Pool p-lock — CIBLE RETENUE, VALIDATION H743 AVANT PATCH

État actuel :

- 16 tracks ;
- 1024 entrées p-lock par track ;
- 6 octets par nœud ;
- pool total actuel : 98 304 B ;
- `g_seq_project` actuel : 129 664 B.

Cible proposée :

- **512 entrées p-lock par track** ;
- limite par step **inchangée à 32 p-locks** ;
- notes totalement séparées du pool p-lock ;
- 512 p-locks simultanés par track ;
- 16 steps peuvent être saturés à 32 locks ;
- moyenne maximale sur 64 steps : 8 locks/step ;
- gain D2 attendu : **49 152 B** ;
- `g_seq_project` attendu : **80 512 B** avant autres changements.

## Avant implémentation

Valider :

- comportement de persistence contenant encore une capacité historique 1024 ;
- projets/corpus réels ou stress synthétiques ;
- saturation `POOL_EMPTY` ;
- restauration de projets dépassant 512 ;
- absence de dépendance cachée des notes au pool.

Ne pas réduire :

- 64 steps ;
- 32 p-locks max/step ;
- scheduler queue ;
- output guards ;
- occurrences actives ;
- NoteFX sources ;
- `control_audio_queue`.

---

# 7. Tempo / transport — FIGÉ

Le scheduler et le transport musical sont M4.

Le M7 possède l’horloge sample réelle.

Le M4 publie un petit snapshot versionné contenant au minimum :

- running / start-pending ;
- tempo effectif ;
- `samples_per_step_q16` ;
- transport epoch ;
- phase/step logique uniquement si un consommateur M7 en a réellement besoin.

Les événements conservent leurs timestamps absolus.

Le M7 ne doit plus appeler directement les getters du scheduler dans LFO, sampler ou looper.

---

# 8. Modulation — FIGÉ

Le **calcul de modulation reste entièrement sur M7**.

## M4 possède

- configuration des 3 LFO ;
- configuration ENV3 ;
- configuration MULTI/SLEW ;
- routes Matrix ;
- valeurs canoniques ;
- destinations et configuration logique.

## M7 possède

- phase courante LFO ;
- RNG / sample-and-hold ;
- ENV3 runtime ;
- opérateurs MULTI/SLEW runtime ;
- Matrix runtime ;
- plans audio ;
- rampes ;
- application aux destinations.

## Frontière cible

```text
M4 ModulationControl
        ↓ snapshot / plan
M7 ModulationRuntime
```

Les accès directs restants à `track_sound_state` doivent disparaître.

Le plan Matrix ne doit plus revenir consulter ParamRegistry ou une structure CONTROL pendant le rendu.

---

# 9. ParamStore / Track state — FIGÉ

## ParamStore

`ParamStore` devient M4-only :

- staging ;
- active CONTROL ;
- transactions ;
- persistence ;
- valeurs canoniques.

Le M7 reçoit des commandes/événements et ne lit pas ParamStore pour piloter le DSP.

## `track_sound_state`

Autorité M4.

Ne doit jamais devenir une ABI inter-core complète.

Le M7 reçoit des projections spécialisées.

## `track_tone_sound_state`

Autorité M4 des paramètres TONE persistants.

Les moteurs audio possèdent leurs propres runtimes.

## Interdiction

L’application AUDIO ne doit plus utiliser `update_base_state = 1` ou un équivalent qui réécrit les états CONTROL.

---

# 10. Mixer / filtres / VCA — FIGÉ

Architecture cible :

```text
MixerControl M4
        ↓ événements / snapshot routing
MixerAudioRuntime M7
```

## M4 possède

- gain demandé ;
- pan demandé ;
- send levels ;
- mute demandé ;
- routing logique ;
- mode filtre ;
- paramètres filtre ;
- paramètres VCA ;
- configuration.

## M7 possède

- gains courants ;
- smoothing ;
- pan courant ;
- filtres DSP ;
- coefficients ;
- enveloppes ;
- VCA runtime ;
- états de gate/note ;
- historiques ;
- routing physique appliqué.

Ne jamais copier vers M4 :

- `env_adsr_t` ;
- historiques filtre ;
- coefficients courants ;
- smoothing ;
- états gate ;
- compteurs audio internes.

---

# 11. FX — FIGÉ

Architecture cible :

```text
AudioFxControl M4
        ↓ événements / snapshot modèle
AudioFxRuntime M7
```

## M4 possède

- modèle/type ;
- P1/P2/P3 ;
- activation ;
- ordre logique ;
- routing ;
- valeurs canoniques.

## M7 possède

- union DSP ;
- buffers delay/reverb ;
- historiques ;
- feedback ;
- coefficients ;
- filtres internes ;
- smoothing ;
- état de transition/reset.

Un changement de modèle est structurel et peut entraîner un reset/transition M7.

Les historiques DSP ne traversent jamais la frontière.

## DJ EQ3

**DIFFÉRÉ.**

La suppression propre du DJ EQ3 est validée conceptuellement mais n’est pas un prérequis à la refonte RAM. Ne pas l’inclure automatiquement dans les futures passes tant qu’elle n’est pas explicitement relancée.

---

# 12. Looper / Recorder — ARCHITECTURE FIGÉE, IMPLÉMENTATION À RISQUE ÉLEVÉ

## Cible

```text
M4 LooperControl
├─ configuration canonique
├─ arm / transport / routing / stretch demandé
├─ session storage
├─ SD / FatFs / finalisation
└─ commandes M4→M7
        ↓
shared pointer-free
        ↓
M7 LooperAudioRuntime
├─ transport audio effectif
├─ capture
├─ playback
├─ playhead
├─ preroll
├─ stretch/pitch
├─ resync
└─ état effectif
        ↓ PCM
ring
        ↓
M4 Storage Writer
├─ packing PCM24
├─ SD writer
├─ committed tail
└─ finalisation WAV
```

## Recorder

Le principe du ring actuel est conservé, ainsi que ses dimensions validées.

À refaire :

- head/tail explicites ;
- ownership producer M7 / consumer M4 ;
- compteurs compatibles inter-core ;
- commandes ;
- acknowledgements ;
- génération de session ;
- séparation intent / état effectif.

Aucun accès SD ou attente M4 depuis l’IRQ.

## Niveau de risque

**ÉLEVÉ.**

Ce chantier doit être implémenté après les frontières générales et le placement RAM, avec audit de diff indépendant.

---

# 13. Sampler / Multi — ARCHITECTURE FIGÉE, IMPLÉMENTATION À RISQUE TRÈS ÉLEVÉ

## M4 possède

- catalogues ;
- IDs logiques ;
- mapping logique → runtime ;
- instruments ;
- zones ;
- samples metadata ;
- paths ;
- WAV metadata ;
- loader ;
- FatFs ;
- CLMT ;
- physical maps ;
- orchestration SD ;
- buffers import/load CONTROL.

## M7 possède

- voices ;
- play plans ;
- interpolation ;
- pitch ;
- stretch ;
- readers ;
- cursors ;
- références actives ;
- runtime audio ;
- page-cache runtime.

## Source publication

Multi doit être résolu côté M4 avant admission audio :

```text
M4 resolve instrument + zone + sample
        ↓
source record compact immutable + generation
        ↓
M7 copies record
        ↓
reader / voice
```

Le M7 ne doit plus scanner/reconsulter les pools instruments/zones/samples pendant l’audio.

Aucun gros pool de descriptors n’est copié côté M7.

---

# 14. Page-cache — FIGÉ

Architecture retenue :

**B — M7 owner du runtime page-cache.**

## M7 possède

- table de résidence ;
- allocation logique des slots ;
- leases ;
- holds/pins logiques ;
- page generations ;
- LRU runtime ;
- éviction ;
- validation completion ;
- autorité finale de `READY` ;
- recyclage des slots après quiescence.

## M4 possède

- path ;
- WAV metadata ;
- FatFs ;
- CLMT ;
- physical map ;
- media epoch ;
- lecture SD ;
- décodage ;
- priorités/scheduler I/O ;
- completions.

## Flux

```text
M7 réserve slot
→ request(token, slot, page)
→ M4 remplit PCM
→ completion(token, result)
→ M7 valide token + generations
→ maintenance cache si nécessaire
→ M7 publie READY
```

Le PCM reste physiquement unique.

## Règle critique

Le M7 gère le cache **hors IRQ** pour :

- allocation ;
- LRU ;
- éviction.

Dans l’IRQ / deadline audio :

- lookup borné ;
- validation ;
- acquire/release ;
- changement de page ;
- miss immédiat si absent ;
- aucune attente M4 ;
- aucun scan global.

---

# 15. Page-cache — lifecycle et générations — FIGÉ

Trois générations fonctionnelles :

1. **asset_generation** — incarnation d’un sample/instrument ;
2. **page_generation** — incarnation d’un slot/page ;
3. **voice_incarnation** — incarnation d’une voix.

À distinguer de :

- `media_epoch` — validité physique média ;
- `command_sequence` — identité/ordre d’une requête I/O.

`registration_epoch` peut devenir l’implémentation de `asset_generation` si le contrat est rendu complet, notamment pour Multi.

## Unload / rebind

```text
RETIRING
→ interdiction nouveaux starts
→ M7 bloque nouveaux acquires
→ arrêt/quiescence voices
→ release leases
→ ACK M7 quiescent
→ M4 annule/drain I/O
→ ACK M4 I/O quiescent
→ M7 recycle slots
→ M4 efface metadata
→ nouvelle asset_generation
```

Aucun ID ou slot ne peut être réutilisé tant qu’une ancienne voix/completion peut l’interpréter comme l’ancien asset.

---

# 16. Streaming — ZONE PROTÉGÉE ABSOLUE

La refonte d’ownership **ne doit pas modifier la politique streaming**.

À préserver strictement :

- sample page pool ;
- nombre de pages ;
- taille page ;
- dimensions cache ;
- stream buffers ;
- `g_sampler_ram_io` ;
- `g_wavetable_pool_io` ;
- `g_sample_cache_io_storage` ;
- FatFs scratch ;
- CLMT ;
- Multi load/import buffers ;
- admission ;
- `sample_stream_needs` ;
- page 0 ;
- prefetch ;
- ordre/priorité de service ;
- garanties de voix ;
- timings ;
- critères underrun ;
- géométrie ;
- politique SD.

La garantie actuelle de **8 voix streaming** est une contrainte de non-régression.

Ne pas mutualiser/déplacer ces buffers dans le cadre de la refonte RAM sans chantier benchmark spécifique.

---

# 17. Architecture mémoire physique — FIGÉ AU NIVEAU DES CONTRATS

## DTCM

Rôle :

- M7 uniquement ;
- très petits états / buffers audio extrêmement chauds ;
- voix/filtres/DSP critiques ;
- aucune donnée CONTROL.

Ne pas utiliser DTCM pour « faire de la place » ailleurs.

## D1 AXI SRAM

Rôle :

- working set AUDIO M7 hot/warm ;
- moteurs ;
- voix ;
- mixer/FX runtime ;
- modulation runtime/plans ;
- reverb ;
- sampler clip rendering ;
- autres données justifiées par accès audio.

D1 ne doit plus être la destination par défaut de `.bss` CONTROL importante.

## D2 — SRAM1 / SRAM2 / SRAM3

Ne plus traiter D2 comme un bloc plat de 288 KiB.

Contrats à séparer :

- DMA ;
- M4 local ;
- IPC ;
- M7 local si besoin ;
- données CPU-only cacheables ;
- vraies LUT si utilisées.

Le futur linker doit connaître les limites physiques SRAM1/SRAM2/SRAM3.

## D3 — SRAM4

Petite zone spécialisée :

- IPC ;
- mailboxes ;
- flags ;
- générations ;
- petites publications ;
- DMA/BDMA spécifique si démontré.

Ne pas y placer de gros buffers génériques simplement parce qu’elle est disponible.

## SDRAM

Priorité aux usages bulk déjà validés :

- sample pools/pages ;
- audio histories/delays validés ;
- recorder ;
- UI/persistence/bulk lorsque leur trafic est borné et démontré acceptable.

Ne pas déporter massivement le CONTROL en SDRAM uniquement pour libérer D2/D3 : la contention SDRAM peut affecter le streaming.

---

# 18. Contrats de sections mémoire — CIBLE

Noms exacts encore libres, mais les contrats doivent être explicites.

Exemple cible :

```text
.ram_d1_audio
.ram_d2_dma
.ram_d2_m4
.ram_d2_m7
.ram_d2_ipc
.ram_d2_local_cacheable
.ram_d3_ipc
.ram_d3_dma
.sdram_control_bulk
.sdram_recorder
```

Principes :

- une section = un contrat d’ownership/cache/DMA compréhensible ;
- ne plus utiliser `.ram_d2_lut` comme fourre-tout ;
- alignements 32 B pour buffers/rings concernés par D-cache ;
- flags de publication et payload ne doivent pas partager accidentellement une cache-line lorsqu’un contrat inter-core l’interdit ;
- assertions linker/startup sur tailles et adresses.

---

# 19. Cache / MPU / IPC — FIGÉ CONCEPTUELLEMENT

## Sur H743

Les frontières logiques peuvent être implémentées sans simuler artificiellement le dual-core.

Pas de HSEM factice nécessaire.

## Sur H747

À valider/implémenter :

- attributs cache des régions shared ;
- MPU ;
- clean/invalidate ;
- atomiques ;
- HSEM/SEV/IPCC ou mécanisme choisi ;
- notifications ;
- contention réelle.

## Règles

- `__disable_irq()` ne protège jamais une concurrence M4/M7 ;
- `volatile` ne garantit pas la cohérence cache ;
- aucun mutex/HSEM bloquant dans l’IRQ audio ;
- rings SPSC lorsque le modèle producer/consumer est naturel ;
- pointer-free pour les messages ;
- générations pour la durée de vie ;
- ownership explicite ;
- aucune structure CONTROL complète mutable partagée.

---

# 20. Baseline mémoire — REPARTIR DU COMMIT GOOD

Aucune baseline issue de `e5b29b0df` ou de ses passes internes ne doit être utilisée comme référence de reprise.

La première action après retour à `fcc6ceb09...` est de reconstruire Release Low-Cost et Premium et d’archiver les MAP **avant toute modification**.

Les chiffres historiques restent utiles uniquement comme ordre de grandeur, jamais comme gate :

- DTCM historique : ~120 640 / 131 072 B ;
- D1 LC historique : ~498 528 / 524 288 B ;
- D1 Premium historique : ~476 608 / 524 288 B ;
- D2 historique : ~274 KiB / 288 KiB ;
- D3 historique : ~58 336 / 65 536 B.

## Clean 1A

Après rollback, statut : **À REFAIRE depuis le commit GOOD**.

Les audits du chantier BAD ont confirmé que le nettoyage diagnostic est conceptuellement souhaitable, avec conservation obligatoire de :

- `cpu_load` ;
- compteurs audio fonctionnels ;
- admission ;
- needs ;
- snapshots fonctionnels ;
- watchdog ;
- crash capsule ;
- waveform UI produit.

Clean 1A doit être réappliqué comme **première passe isolée**, puis testé matériellement et checkpointé avant toute modification RAM.

## DJ EQ3

Suppression toujours **DIFFÉRÉE**. Ne pas l’inclure dans le redémarrage du chantier.

---

# 21. Nettoyages déjà décidés / différés

## Diagnostics

À supprimer lors de la nouvelle Clean 1A (liste issue des audits précédents) :

- `sample_stream_event_trace` ;
- `rec_live_debug` ;
- `mixer_path_diag` ;
- diagnostics compile-time historiques purement développement.

À conserver :

- `cpu_load` ;
- compteurs audio nécessaires au timing ;
- admission ;
- needs ;
- snapshots fonctionnels ;
- watchdog ;
- crash capsule ;
- waveform UI produit ;
- télémétrie produit utile.

## DJ EQ3

Suppression validée conceptuellement mais **DIFFÉRÉE**.

Ne pas l’inclure dans les passes RAM sans demande explicite.

---

# 22. Contradictions d’audits résolues

## `g_slot`

Ancien verdict : D1 AUDIO justifiée.  
Verdict final : **CONTROL / futur M4**.

Raison : l’audit ciblé a retracé le flux complet :

```text
keyboard / MIDI / seq
→ NoteFX g_slot
→ emit
→ control_audio_queue
→ M7 audio
```

Aucun pointeur ou lookup audio vers `g_slot`.

## `g_mod_destination_cache`

Ancien verdict contradictoire : cache M7 possible.  
Verdict final : **catalogue/résolution CONTROL**.

Le M7 utilise les plans/runtime Matrix dédiés et ne dépend pas du gros cache de destination.

## `g_sampler_clip_slots`

Ancien soupçon : configuration M4 possible.  
Verdict final : **M7 AUDIO runtime**, contient les buffers de rendu/stretch.

À protéger et à garder proche du M7.

## `.sdram_audio_cold`

Le placement Low-Cost en D1 n’est pas une optimisation démontrée. C’est un héritage d’un revert.

Contenu :

- `g_sd_preview_ring` 16 384 B — consommé par l’audio, déplacement conditionnel à benchmark ;
- `g_sd_preview_io` 4 096 B — hors IRQ, D1 non justifiée.

Ne pas considérer le ring comme gain gratuit tant que le benchmark worst-case n’est pas effectué.

---

# 23. Interdictions de chantier

Le futur pilote ne doit pas :

1. déplacer des buffers streaming calibrés pour « gagner de la D1 » ;
2. envoyer `g_seq_project` en SDRAM par défaut ;
3. mettre le moteur de modulation sur M4 ;
4. partager directement `track_sound_state` entre cores ;
5. partager ParamStore comme source DSP ;
6. copier de gros états DSP dans des snapshots ;
7. utiliser D3 comme gros buffer générique ;
8. remplir D2 à 99 % sans marge ;
9. considérer le H747 comme ajoutant une deuxième D2 indépendante ;
10. utiliser `__disable_irq()` comme synchronisation inter-core ;
11. mettre un mutex/HSEM bloquant dans l’IRQ ;
12. changer page size, admission, needs ou garanties voix durant le chantier page-cache ;
13. réutiliser un asset/page avant quiescence ;
14. créer une seconde copie du pool PCM ;
15. modifier simultanément plusieurs gros sous-systèmes sans gate intermédiaire ;
16. optimiser opportunistiquement une zone hors périmètre sans audit dédié.

---

# 24. Ordre de reprise recommandé — VERSION 2

La reprise doit être **plus granulaire que le premier chantier**. Chaque passe est matériellement validée avant la suivante.

## REPRISE 0 — Sécuriser Git + baseline GOOD

- conserver `e5b29b0df` sur une branche de sauvegarde ;
- remettre la branche de travail sur `fcc6ceb09...` ;
- vérifier worktree clean ;
- build Release Low-Cost + Premium ;
- archiver les MAP ;
- confirmer matériellement le son synthé + LINE et la navigation UI de base.

**Aucun changement fonctionnel.**

## REPRISE 1 — Clean diagnostics uniquement

Réappliquer le nettoyage diagnostics déjà audité, sans RAM/linker/frontières.

Conserver `cpu_load` et les protections fonctionnelles listées dans ce document.

**Gate : hardware smoke → checkpoint Git.**

## REPRISE 2 — Contrats mémoire physiques uniquement

Objectif conceptuel identique à l’ancien PASS 1 :

- séparer SRAM1/SRAM2/SRAM3 ;
- clarifier D2 DMA / M4 / IPC / M7 ;
- clarifier D3 IPC ;
- supprimer le fourre-tout `.ram_d2_lut` ;
- sections explicites ;
- alignements/assertions ;
- aucun déplacement massif de gros objets ;
- streaming intouché.

Attention particulière : RX/TX DMA et MPU/cache doivent rester fonctionnellement identiques tant qu’un changement n’est pas démontré nécessaire.

**Gate : synth + LINE + UI → checkpoint Git.**

## REPRISE 3 — Boot AUDIO propriétaire

Créer d’abord la frontière qui a manqué dans le premier chantier :

```text
BOOT / CONTROL orchestration
→ petite config/intent de boot
→ entrée AUDIO propriétaire
→ Mixer/master/FX/synth engines/binding runtime
```

Le bootstrap commun ne doit plus muter directement les internals DSP.

Inclure dans cette passe uniquement les bypass boot de même classe :

- Mixer/master ;
- postgain/output compensation ;
- FX globaux ;
- Drum/Braids/Stack/Wave/FM ;
- binding/runtime physique initial.

Ne pas encore refondre les paramètres runtime courants.

**Gate : hardware smoke complet → checkpoint Git.**

## REPRISE 4 — Frontières CONTROL → AUDIO modulation / transport

Fermer en une classe auditée globalement :

- LFO ;
- ENV3 ;
- MULTI/SLEW ;
- Matrix → ParamRegistry ;
- tempo/transport ;
- écritures AUDIO → états CONTROL.

Avant patch, rechercher aussi init/reset/reapply de cette classe afin de ne pas créer publication + ancien setter direct.

**Gate : hardware smoke + modulation de base → checkpoint Git.**

## REPRISE 5A — Mixer / Filter / VCA / FX runtime

Séparer Control vs Runtime sans déplacer les historiques DSP côté M4.

Inclure dès le premier patch de cette passe :

- gain/pan/mute/sends ;
- Filter/VCA ;
- FX track/globaux ;
- routing et insert slots ;
- boot déjà raccordé à l’entrée AUDIO de REPRISE 3 ;
- recherche globale des setters directs avant de déclarer fermé.

**Gate : synth + LINE impératifs → checkpoint Git.**

## REPRISE 5B — UI waveform CONTROL → AUDIO

Router les commandes UI waveform par une petite publication/commande pointer-free.

Éliminer les appels UI directs vers :

- `audio_waveform_capture_set_entity` ;
- `audio_waveform_capture_set_fast_refresh` ;
- `audio_waveform_capture_disable`.

**Gate : Settings/Sample Browser/UI waveform → checkpoint Git.**

## REPRISE 5C — Legacy dangereux

Après preuve d’absence d’appelants actifs :

- supprimer/neutraliser `param_backend_reapply_tone_{prism,fm,stack,wave}_runtime()` ;
- classifier/nettoyer ancien Matrix/rebuild ;
- nettoyer les dead paths capables de réintroduire un bypass.

Ne pas mélanger avec une refonte fonctionnelle.

## REPRISE 6 — Audit global de fermeture des frontières générales

Audit lecture seule exhaustif. Résultat attendu avant la RAM réelle :

- catégorie A (fuites générales actives) = **0** ;
- `AUDIO → CONTROL canonical write = 0` ;
- `CONTROL → AUDIO runtime direct write = 0` hors chantiers explicitement différés ;
- seules les catégories Looper/Recorder et Sampler/Multi/page-cache restent ouvertes.

## REPRISE 7 — p-lock 512 puis placement RAM réel

Deux sous-passes recommandées :

### 7A — p-lock 1024 → 512

Valider persistence et sémantique, puis appliquer seul. Gain attendu historique : 49 152 B D2.

### 7B — placement des owners réels

Une fois l’espace D2 récupéré :

- déplacer les gros CONTROL prouvés hors D1 ;
- ranger DMA/IPC ;
- nettoyer D3 ;
- ne pas toucher aux buffers streaming protégés ;
- refaire MAP/budgets.

Chaque sous-passe possède son propre hardware gate et checkpoint.

## REPRISE 8 — Looper / Recorder

Implémenter commands/acks/générations et producer M7 / writer M4 conformément aux sections 12 et 19.

**Risque élevé.**

## REPRISE 9 — Sampler / Multi / Page-cache

Implémenter l’architecture B figée, source records compacts, pré-résolution Multi, worker I/O M4, READY M7, leases et quiescence.

**Risque très élevé. Dernier gros chantier.**

## REPRISE 10 — Stress H743 complet

Valider le produit complet et les MAP finales sur H743.

## REPRISE 11 — H747 physique

Seulement lorsque le matériel est disponible : images CM4/CM7, linker final, MPU/cache, notifications, concurrence, contention et stress inter-core.

---

# 25. Pilotage / parallélisation — règle de reprise

## Audits

Les audits indépendants peuvent être parallélisés avant patch. L’écriture simultanée dans un même sous-système est interdite.

## Implémentation

Une seule passe structurelle à la fois :

```text
Audit lecture seule ciblé/global de la classe
        ↓
Codex implémente UNE passe
        ↓
build LC/Premium + diff/MAP
        ↓
audit lecture seule indépendant
        ↓
USER teste le H743 réel
        ↓
si PASS : checkpoint Git unique de cette passe
        ↓
passe suivante
```

Le user est l’autorité du **hardware gate**. Codex/ChatGPT ne doit jamais déduire « matériel OK » d’un build réussi.

## Git — OBLIGATOIRE

- une passe validée = un commit/checkpoint identifiable ;
- jamais regrouper plusieurs passes non testées dans un même commit ;
- ne jamais commencer la passe suivante avant validation matérielle du user ;
- aucun push automatique ;
- conserver le commit BAD uniquement comme référence d’audit ;
- si une passe casse quelque chose, analyser/revert **le seul diff de cette passe**, pas continuer en empilant des fixes.

---

# 26. Gates obligatoires après chaque passe

## Gate statique / build

Vérifier selon pertinence de la passe :

1. build Release Low-Cost ;
2. build Release Premium ;
3. `git diff --check` hors artefacts ;
4. MAP avant/après si la passe peut modifier le layout ;
5. RAM par région si pertinent ;
6. recherche négative sur les APIs supprimées ;
7. absence de double source de vérité ;
8. aucun buffer streaming déplacé sans autorisation ;
9. aucune dépendance H747 prématurée.

Ne pas imposer instrumentation/benchmarks lourds à toutes les passes. Mesurer IRQ uniquement lorsqu’un hot path est réellement modifié et que la mesure apporte une décision.

## Gate matériel H743 — NOUVEAU / OBLIGATOIRE

Avant de passer à la passe suivante, le user vérifie au minimum :

- synth : une note produit un son propre ;
- LINE input : signal propre ;
- UI : navigation générale ;
- Settings : entrée fonctionnelle ;
- Sample Browser : entrée fonctionnelle ;
- fonction spécifique touchée par la passe.

Le smoke test doit rester court et discriminant. Il ne remplace pas les stress tests finaux.

## En cas d’échec matériel

1. STOP immédiat ;
2. ne pas appliquer la passe suivante ;
3. ne pas empiler des micro-patches exploratoires ;
4. auditer exclusivement le diff depuis le dernier checkpoint GOOD ;
5. localiser la première frontière corrompue ;
6. corriger ou revert avant de poursuivre.

---

# 27. Validation H743 — À FAIRE AVANT H747

Le H743 doit devenir une simulation stricte de l’architecture logique dual-core.

Même si CONTROL et AUDIO s’exécutent physiquement sur le même M7 :

```text
CONTROL logique
→ ABI
→ AUDIO logique
```

Le code doit déjà respecter :

- absence de lecture directe CONTROL mutable depuis le rendu ;
- absence d’écriture AUDIO vers l’autorité CONTROL ;
- pointer-free messages ;
- générations ;
- ownership ;
- page lifecycle ;
- command/ack looper.

Cette validation permet de séparer les bugs d’architecture des futurs bugs de cache/concurrence H747.

---

# 28. Validation nécessitant H747 réel

Ne pas prétendre valider sur H743 :

- cohérence réelle D-cache M4/M7 ;
- contention simultanée D2/D3 ;
- HSEM/SEV/IPCC ;
- boot indépendant ;
- ownership réel des périphériques ;
- SDMMC/DMA entre cores ;
- contention FMC/SDRAM ;
- reset d’un cœur ;
- atomicité réelle inter-core ;
- latence réelle des notifications.

Ces sujets sont **À VALIDER H747**, pas des raisons de rouvrir les décisions conceptuelles.

---

# 29. Risques majeurs

## Rouge — très élevé

### Sampler / Multi / page-cache

Risques :

- race unload/rebind ;
- completion tardive ;
- ABA sur ID réutilisé ;
- page recyclée sous une voix ;
- cohérence cache ;
- underrun ;
- dégradation de la garantie 8 voix.

Réponse :

- dernier chantier ;
- protocole figé ;
- invariants streaming intouchables ;
- benchmark dédié.

## Rouge / orange — élevé

### Looper / Recorder

Risques :

- overflow ring ;
- état demandé ≠ état effectif ;
- finalisation SD ;
- mauvais ack ;
- faux verrou monocœur ;
- contention.

## Orange — moyen à élevé

### Mixer / FX / Filter

Risques :

- régression sonore affectant à la fois synthés et LINE ;
- smoothing interrompu ;
- reset runtime mal placé ;
- paramètres appliqués deux fois ;
- latence structurelle.

## Orange — moyen

### Modulation / ParamStore

Risques :

- valeur base ≠ valeur modulée ;
- snapshot incomplet ;
- destination Matrix reconstruite à partir d’un état stale ;
- publication tardive.

## Jaune — faible à moyen

### Contrats RAM/linker

Risques :

- section mal bornée ;
- attribut MPU incohérent ;
- objet DMA hors fenêtre ;
- mauvais alignement ;
- déplacement RX/TX/cache qui compile mais régresse sur matériel.

---

# 30. Critères de réussite finaux

La refonte est réussie si :

1. D1 est majoritairement AUDIO M7.
2. D2 est partitionnée explicitement par rôle.
3. D3 est petite et spécialisée IPC.
4. Aucun gros CONTROL mutable n’est lu directement par le M7 audio.
5. Aucun runtime DSP M7 n’est modifié directement par M4.
6. Le séquenceur reste déterministe en SRAM.
7. Le pool p-lock réduit, s’il est appliqué, ne casse aucune sémantique.
8. Le streaming conserve ses capacités et sa garantie 8 voix.
9. Le page-cache ne peut plus recycler une page utilisée.
10. Un unload/rebind est générationnel et quiescent.
11. Looper/Recorder est producer/consumer sans blocage IRQ.
12. H743 fonctionne avec les mêmes frontières logiques que H747.
13. Le passage H747 ne nécessite plus une refonte fonctionnelle.
14. Les marges D1/D2/D3 sont saines et documentées.
15. Les MAP LC/Premium sont cohérentes et expliquées.

---

# 31. Statut des décisions

| Sujet | Statut |
|---|---|
| M4 CONTROL / M7 AUDIO | **FIGÉ** |
| Modulation calculée M7 | **FIGÉ** |
| ParamStore M4 | **FIGÉ** |
| Track states M4 | **FIGÉ** |
| Mixer/Filter/VCA runtime M7 | **FIGÉ** |
| FX runtime M7 | **FIGÉ** |
| Séquenceur / PLAY / NoteFX M4 | **FIGÉ** |
| `g_seq_project` SRAM interne | **FIGÉ** |
| Pool p-lock 512 | **CIBLE RETENUE — valider avant patch** |
| Tempo/transport snapshot vers M7 | **FIGÉ** |
| Looper Control M4 / Audio M7 | **FIGÉ** |
| Recorder producer M7 / writer M4 | **FIGÉ** |
| Sampler metadata M4 / playback M7 | **FIGÉ** |
| Multi pré-résolu côté M4 | **FIGÉ** |
| Page-cache runtime owner M7 | **FIGÉ** |
| READY owner M7 | **FIGÉ** |
| Eviction owner M7 | **FIGÉ** |
| PCM pool unique | **FIGÉ** |
| Streaming dimensions/policy | **PROTÉGÉ / FIGÉ** |
| Contrats DTCM/D1/D2/D3 | **FIGÉS conceptuellement** |
| Placement linker exact H747 | **À VALIDER H747** |
| MPU/cache/HSEM exacts | **À VALIDER H747** |
| DJ EQ3 suppression | **DIFFÉRÉ** |
| Clean 1A | **À REFAIRE DEPUIS `fcc6ceb09`** |
| Ancien chantier `e5b29b0df` | **ABANDONNÉ COMME IMPLÉMENTATION — référence audit uniquement** |
| Boot AUDIO propriétaire | **À IMPLÉMENTER AVANT LES FRONTIÈRES DSP ÉTENDUES** |
| UI waveform publication | **À IMPLÉMENTER** |
| Hardware gate par passe | **OBLIGATOIRE** |
| Checkpoint Git par passe validée | **OBLIGATOIRE** |

---

# 32. Instruction au futur pilote

Tu reprends le rôle de pilote technique du chantier RAM / dual-core BRICK.

## Avant tout nouveau patch

1. lire ce document intégralement ;
2. vérifier que la reprise part réellement de `fcc6ceb092d423fa9403ff2c22511429d9bbaacc` ou d’un checkpoint descendant explicitement validé matériellement ;
3. considérer `e5b29b0df` comme **BAD de référence**, jamais comme base fiable ;
4. reconstruire la baseline LC/Premium du checkpoint GOOD ;
5. ne pas rouvrir les décisions `FIGÉ` sans contradiction démontrée ;
6. identifier la classe complète de bypass avant patch afin d’éviter les corrections fuite-par-fuite ;
7. appliquer une seule passe cohérente ;
8. demander un audit lecture seule indépendant du diff ;
9. arrêter et laisser le user faire le hardware gate ;
10. ne proposer le checkpoint Git qu’après verdict matériel PASS ;
11. ne jamais enchaîner deux passes architecturales non testées ;
12. ne documenter comme « appliqué/fermé » que ce qui existe dans le checkpoint matériellement validé.

## Style de pilotage

Pour le user :

- réponses courtes ;
- verdict clair ;
- expliquer simplement les problèmes quand une décision est nécessaire ;
- fournir des prompts Codex précis et copiables ;
- privilégier les audits lecture seule avant les patches ;
- ne pas ajouter de tests/instrumentation lourds sans raison ;
- laisser les tests physiques au user ;
- ne pas faire de push automatique.

## Principe directeur

**Architecture propre d’abord, octets gagnés ensuite — mais aucun progrès architectural n’est considéré acquis tant que le H743 réel n’a pas passé le gate de la passe.**
