# Architecture globale BRICK6

Le code courant est l'autorite finale. Ce document est l'unique porte d'entree documentaire; les documents de domaine portent les contrats detailles sans les repeter ici.

## Invariants produit

- Seize identites logiques stables existent: huit entites top-level `0..7` et huit children GROUP `8..15`, actives uniquement lorsque l'entite 7 est GROUP master.
- `entity_topology` derive activite, role, parent et capacites. `track_state` possede la configuration CONTROL; `track_runtime` la projette vers les moteurs, ressources et voies physiques.
- Le Streamer est le seul lecteur de boucles et choisit une source POOL ou REC. External est un moteur dont l'entree physique est arbitree par `track_input_ownership`. Ressource physique et voie mixer ne sont jamais des identites logiques.
- CONTROL possede UI, preparation Pattern et catalogue Sample. SEQ possede transport musical, Pattern actif, ROLL, Note FX, p-locks, lifetimes, futures, admission et stealing musical. STORAGE possede le Stream, son I/O et son page-cache. AUDIO possede IRQ, mapping d'execution logique vers slot DSP, RELEASE physique, lecteurs, positions, moteurs et mixer.
- Les objets nommes utilisent le contrat canonique partage: 32 caracteres visibles maximum, buffer de 33 octets, alphabet espace/ASCII alphanumerique/`_`/`-`, validation finale avec trim des espaces externes, sans allocation ni remplacement silencieux.
- L'ordre fonctionnel CONTROL vers AUDIO traverse exclusivement la FIFO SPSC
  unique PROGRAM/PARAM/NOTE/TRANSPORT/RECORD/PANIC. Aucun pointeur ne traverse
  cette FIFO; les data planes monocoeur utilisent des pointeurs locaux lorsque
  leur duree de vie est protegee.
- CONTROL valide l'etat logique final et ses budgets avant publication. AUDIO
  applique sans negociation; l'impossibilite d'appliquer une commande admise est
  un fatal source et non une erreur recuperable ou une commande depilee.
- Un restore Pattern/Project publie une projection AUDIO fraiche des autorites
  CONTROL finales. Project libere puis reconstruit toutes les installations;
  Pattern ne remplace que les PROGRAM modifies et conserve les outputs vivants.
- `STOP(output_handle)` rend l'output musicalement mort dans CONTROL. L'identite semantique NoteFx/SEQ ne traverse pas l'ABI AUDIO. AUDIO peut conserver une tail RELEASE et libere ou reutilise physiquement le slot sans ACK musical.
- Pattern, Project et Patch utilisent exclusivement le codec CONTROL explicite version 7.
- Le Pattern vivant reste unique. Lors d'un Recall, CONTROL capture le Pattern
  sortant dans un unique DTO SDRAM avant le commit cible; STORAGE reconcilie
  ensuite cet etat avec sa base et conserve les seuls overrides sous
  `BRICK/WORKING/PATTERNS/`.
- Save Project publie `PROJECT.B6C` et les seuls Patterns dirty comme une
  transaction globale journalisee sous `BRICK/TRANSACTIONS/PROJECT/`; Working
  n'est nettoye qu'apres le marqueur global COMMITTED.
- Save As construit un dossier candidat autonome avec `Working -> Saved ->
  Default absent`, puis change la base sans reload musical apres son renommage
  atomique. New engage au contraire un Blank neuf par le pipeline Project Load.
- L'extinction volontaire fige l'ingress, reconcilie le Pattern actif et publie
  un Resume double-slot sous `BRICK/RESUME/`; ce snapshot reference les
  overrides de `BRICK/WORKING/` sans les dupliquer ni modifier le Project saved.

## Flux principaux

```text
configuration CONTROL -> decision produit canonique -> contrat de domaine
-> FIFO mecanique -> validation physique et application AUDIO
SEQ/live -> identite semantique -> allocation handle et transitions CONTROL ordonnees -> AUDIO
capture TIM5 -> conversion audio -> file datee -> segmentation -> rendu
credit de fenetre stream AUDIO -> I/O Storage tokenisee -> page AUDIO
Save/Load -> decode safety -> safe quiesce -> progressive install -> publication
```

Restore reste CONTROL et republie les effets AUDIO par la FIFO unique; aucune
completion AUDIO ne reconstruit ou ne confirme l'etat musical.

La configuration modulation/ENV3/Matrix, le routing Audio REC, l'ownership des
entrees, le bus Recorder, la selection Wavetable et le lifecycle Preview sont
des decisions CONTROL ordonnancees dans cette meme FIFO. Les rings PCM,
tables/mipmaps et compteurs de data plane restent separes; ils ne portent
aucune seconde chronologie fonctionnelle.

## Arborescence physique

```text
Inc/ et Src/
|-- App/                 bootstrap et orchestration produit
|-- Audio/               runtime AUDIO, mixer, DSP et effets
|   `-- Engines/         moteurs Prism, Stack, FM, TB-303, Wavetable et Sampler AUDIO
|-- Contracts/           contrats sans owner unique (Storage/AUDIO Preview)
|-- Recorder/            ring monocoeur AUDIO IRQ vers STORAGE
|-- Platform/            configuration physique, memoire, faults et diagnostics
|-- Track/               identites, topologie et etat/runtime de piste
|-- Param/               catalogue, valeurs CONTROL et migration PARAM
|-- Mod/                 configuration Matrix/LFO et projection AUDIO derivee
|-- Seq/                 modele, edition, scheduler et runtime sequenceur
|-- NoteFx/              transformations musicales CONTROL
|-- Sampler/             pools, caches, readers et preparation de ressources
|-- Storage/             persistence, projets, capture et services I/O
`-- UI/                  navigation, pages, interactions et rendu
```

Les contrats publics vivent sous `Inc/<domaine>` et les implementations sous
`Src/<domaine>`. Les headers strictement internes restent pres de leur
implementation dans `Src`. Aucun domaine generique `Core` ne subsiste.

## Documents proprietaires

- [z0_plateforme_cadence.md](z0_plateforme_cadence.md): plateforme, memoire, cache, cadence et frontieres IRQ/superloop.
- [z1_audio_hard_rt_mix.md](z1_audio_hard_rt_mix.md): moteurs, voix, mixer, GROUP et effets.
- [z2_track_runtime_authority.md](z2_track_runtime_authority.md): identites, topologie, programmes, Streamer et External.
- [z3_param_modulation_control.md](z3_param_modulation_control.md): parametres, valeur canonique, p-locks, modulation et commandes AUDIO datees.
- [z4_seq_clock_scheduler.md](z4_seq_clock_scheduler.md): sequence, Note FX, horodatage live, files et Undo/Redo.
- [note_fx_capacity_lifetime_audit.md](note_fx_capacity_lifetime_audit.md): transitions NoteFx, identites, handles, admission et revoice.
- [z5_ui_navigation_interaction.md](z5_ui_navigation_interaction.md): navigation, modes, selection, Master et ordre des handlers.
- [ui_render_cooperative.md](ui_render_cooperative.md): rendu OLED fractionne, annulation, coalescence et atomicite de frame.
- [z6_state_persistence_patterns_projects.md](z6_state_persistence_patterns_projects.md): modele, codec, cles, Pattern, Patch, Project et transactions Storage.
- [internal_flash_map.md](internal_flash_map.md): partage Flash firmware et reservation de la future banque Groove.
- [crash_library.md](crash_library.md): capsules diagnostiques best-effort sur SD.
- [hall_calibration_persistence.md](hall_calibration_persistence.md): calibration globale SD chargee en RAM au boot.
- [stream_need_contract.md](stream_need_contract.md): Sampler RAM, Wavetable, Multi, streaming, page-cache et transport I/O.
- [recorder_sd.md](recorder_sd.md): bus AUDIO REC unique, Recorder, REC_SOURCE A/B, export cooperatif et lecture Streamer.
- [stream_rec_perf_baseline.md](stream_rec_perf_baseline.md): protocole et limites de la baseline CPU/DMA Streamer et Recorder H743.
- [sampler_stream_ram_unification_audit.md](sampler_stream_ram_unification_audit.md): audit strict de l'UX SAMPLE unifiee par trajectoire, admission zero-raté, cache et limites physiques.
- [control_audio_functional_contract.md](control_audio_functional_contract.md): FIFO locale unique et consumer AUDIO.
- [monocore_data_planes.md](monocore_data_planes.md): data planes locaux, cache, DMA et recyclage.
- [audio_service_publications.md](audio_service_publications.md): publications AUDIO vers les services cooperatifs.
# Optimisation Release

Les unités BRICK des manifestes de domaine sont compilées en `-O2` avec LTO.
Les noyaux AUDIO et les boucles de rendu Sampler explicitement identifiés restent
en `-O3` avec LTO. Les sources HAL, générées et tierces gardent leur politique
distincte. Les données D2 réparties entre SRAM1, SRAM2 et SRAM3 utilisent des
sections nommées : leur placement ne dépend plus du nom du fichier objet, qui
disparaît lors de la compilation LTO.
