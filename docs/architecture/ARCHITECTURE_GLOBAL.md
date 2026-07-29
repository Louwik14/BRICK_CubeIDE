# ARCHITECTURE_GLOBAL.md

## 1. Rôle

Ce document est une carte d’orientation.

Il ne détaille pas l’architecture locale.
Il sert uniquement à :
- identifier la bonne zone
- comprendre les dépendances principales entre zones
- savoir quels documents lire avant de modifier le code

Lecture cible du projet :
- état canonique / contrôle
- projection runtime track-aware
- exécution bornée

La trajectoire d’architecture vise des seams explicites et une préparation propre au futur split dual-core, sans créer de bus central ou d’IPC prématuré.

Le détail réel vit dans :
- `docs/architecture/z0_plateforme_cadence.md`
- `docs/architecture/z1_audio_hard_rt_mix.md`
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z4_seq_clock_scheduler.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/architecture/z6_state_persistence_patterns_projects.md`

---

## 2. Zones d’architecture

## Z0 — Plateforme / Cadence
Lit ce document si le sujet touche :
- boot
- init système
- ordre d’initialisation
- superloop
- tasklets / cadence hors audio IRQ

Doc :
- `docs/architecture/z0_plateforme_cadence.md`

## Z1 — Audio Hard-RT / Mix
Lit ce document si le sujet touche :
- DMA / IRQ audio
- blocs audio
- conversion I/O audio
- callback DSP
- mixage
- taps recorder Looper dans le pipeline audio
- playback Looper via pages RAM pretes dans le pipeline mixer
- runtimes synth mono externes Prism / Stack / Wave / DELUGE et leurs imports DSP
- metronome MAIN monitor-only post-capture/post-MasterFX

Doc :
- `docs/architecture/z1_audio_hard_rt_mix.md`

## Z2 — Track Runtime Authority
Lit ce document si le sujet touche :
- track_state autoritatif
- family / type effectifs
- binding runtime
- mix target runtime
- capacités runtime
- statut effectif d’un domaine param
- logique track-aware centrale

Doc :
- `docs/architecture/z2_track_runtime_authority.md`

## Z3 — Param / Modulation / Control
Lit ce document si le sujet touche :
- écriture param
- clamp / dispatch / apply
- staging / commit
- modulation LFO
- coexistence global / track-aware / legacy
- modèle paramétrique par track
- base commune `track_sound_state` + base TONE `track_tone_sound_state` (Input1/2/3 Hybrid gate + Sampler + Wave + Stack + DELUGE + MIDI simple + TRX BD reserve + BD Analog)

Doc :
- `docs/architecture/z3_param_modulation_control.md`

## Z4 — Seq / Clock / Scheduler
Lit ce document si le sujet touche :
- transport
- tempo / clock
- progression musicale
- boundaries
- scheduling sample-accurate
- evenements audio metronome sample-accurate
- live-rec lié au transport

Doc :
- `docs/architecture/z4_seq_clock_scheduler.md`

## Z5 — UI / Navigation / Interaction
Lit ce document si le sujet touche :
- état UI
- navigation
- hall modes
- track select
- résolution contextuelle des pages
- raccourcis
- clipboard UI

Doc :
- `docs/architecture/z5_ui_navigation_interaction.md`

## Z6 — State / Persistence / Patterns / Projects
Lit ce document si le sujet touche :
- snapshot live
- queue/apply pattern
- save/load pattern
- save/load project
- boot context
- restore global d’état
- writer Looper, reservoirs RAW systeme, export SAVE RAW -> WAV durable, paths finaux

Doc :
- `docs/architecture/z6_state_persistence_patterns_projects.md`

---

## 3. Dépendances principales entre zones

- Z0 initialise et cadence tout le reste
- Z1 consomme surtout Z2, Z3 et Z4
- En clock interne/externe sequencer, Z1 fournit la consommation finale d'avance step de Z4 (domaine audio bloc)
- Z2 fournit la vérité runtime aux autres zones
- Z3 applique des valeurs en s’appuyant sur Z2
- En PLAY+REC actif, les edits param track-aware sont redirigés vers Z4 (écriture p-lock live), sans write runtime direct Z3 en parallèle
- Z4 produit les événements temporels consommés par Z1
- Z4 et Z5 peuvent notifier Z3 des note/trig runtime via le seam explicite LFO, sans devenir autorité de modulation
- Z5 pilote Z2, Z3, Z4 et Z6 via l’interaction utilisateur
- Z6 capture/restaure de l’état qui réimpacte Z2, Z3, Z4 et Z5

Règle de lecture transversale :
- Z2 porte la projection runtime track-aware
- Z3 et Z4 consomment cette projection sans recréer d’autorité locale parallèle
- Z5 expose des choix utilisateur, mais ne doit pas devenir une seconde autorité de structure
- Z1 et Z0 restent des zones d’exécution / orchestration bornée, pas des lieux de décision métier

---

## 4. Comment choisir quoi lire

### Si le prompt parle de :
- **boot / ordre d’init / boucle principale** ? Z0
- **IRQ audio / mix / buffer audio / DMA** ? Z1
- **family / type / mix target / runtime bind** ? Z2
- **paramètres / LFO / apply / staging** ? Z3 (hors redirection live-rec PLAY+REC)
- **transport / tempo / scheduler / live rec séquenceur** ? Z4
- **UI / halls / navigation / pages / clipboard** ? Z5
- **save/load / patterns / projects / restore** ? Z6

### Cas transverses fréquents
- **Input Audio vs Hybrid** ? Z2 + Z3 + Z5
- **Master/FX MacroFX** ? Z1 + Z2 + Z3 + Z5
- **bug track-aware transversal** ? commencer par Z2
- **bug après load/restore** ? Z6 puis Z2/Z3/Z4/Z5 selon symptôme

### Philosophie d’architecture attendue
- track-aware avant global
- ownership clair avant confort d’implémentation
- canonical state avant projection
- projection avant exécution
- seams explicites avant abstractions larges
- future dual-core par séparation réelle, pas par surcouche de transport
- pas de nœud central ambigu
- pas d’infra prématurée

---

## 5. Annexes utiles (non canoniques)

Regle de navigation: un seul document canonique par zone Z0..Z6 (listee en sections 1 et 2 ci-dessus).

Annexes utiles conservees:
- docs/architecture/annexe_z3_param_authority_matrix.md: matrice d'autorite d'ecriture param (detail Z3).
- docs/architecture/annexe_z5_mode_transition_contracts.md: details de transitions de modes UI (detail Z5).
- docs/architecture/annexe_z5_ui_core_tick_order_contracts.md: contrat d'ordre ui_core_tick et priorites de consommation (detail Z5).
- docs/architecture/annexe_z5_ui_restore_track_config_bulk_flow_2026-04-14.md: flow detaille de restore bulk track config (detail Z5).
- docs/architecture/annexe_z5_ui_system_sync_boundary_2026-04-15.md: frontiere et extraction du noyau de sync systeme UI (detail Z5).

## 6. Historique / non autoritatif

Documents conserves pour tracabilite uniquement:
- docs/architecture/historique_z3_param_write_map_audit_2026-04-14.md (passe d'audit ciblee).
- docs/architecture/historique_z4_quant_swing_runtime_contract_2026-04-14.md (note de chantier pre-consolidation).
- docs/architecture/historique_z5_ui_orchestration_cartographie_2026-04-14.md (cartographie de passe initiale).

## 7. Addendum 2026-05-13

## Addendum 2026-07-29 - remplacement Synth/Daisy par Synth/DELUGE

- Z1 remplace le moteur de test DaisySP par un port GPL-3.0 des oscillateurs basic-wave Deluge, scalarise pour Cortex-M7, a phase/tables fixed-point et comportement fixe non degrade.
- La reference figee du port est le commit upstream `0d9cbf0440f0555e2544cc1eb019b31675637008` de `SynthstromAudible/DelugeFirmware`.
- Z2 conserve le point d'ownership mono par track sous les nouvelles identites DELUGE; Z3 porte sept params dedies, Z5 deux pages TONE, Z6 leur snapshot sans migration prototype.
- L'ancien runtime/math/oscillateur Daisy et les sources `Drivers/Daisy_SP/Source/Synthesis` exclusives sont retires; la Utility DaisySP reste consommee par le compresseur et le Moog ladder, hors moteur DELUGE.

## Addendum 2026-07-26 - MOD operators Matrix

- Z3 étend l'autorité Matrix avec des opérateurs control-rate `MULTI/SLEW` et des destinations `LFO rate`; Z5 expose cette surface en `MOD 2/2`.
- Les opérateurs restent dans le flux modulation existant, sans nouveau backend audio ni calcul par sample.

- Le buffer master dedie est retire des zones Z1/Z2/Z3/Z5/Z6.
- `audio_xfade` reste rattache au Looper, sans backend buffer master.
- Le recorder legacy dormant `live_recorder` / `recorder_transport` est retire de Z0/Z1; le record produit conserve uniquement Looper RAW + `multi_record_writer`.

## Addendum 2026-05-27 - Patch V1

- Patch V1 ajoute un seam Z5/Z6: UI Patch Assign + persistence de snapshot canonique d'une track sous slots fichiers separes de Project/Pattern.
- Kit reste l'extension future multi-track; Set est retire du contrat produit.

## Addendum 2026-05-29 - Kit V1 etape 3

- Kit V1 etend le seam Z5/Z6 avec `PAGE2 APPLY`: le Kit charge un payload complet, pre-valide les assets Sampler deja presents, neutralise les notes/voix, applique la structure tracks en bloc via les autorites track_state/runtime, puis restaure sound/tone/LFO et reprojette les params track-aware.
- Le contrat reste sans apply partiel, target mask, preview, rollback, Set, sequence, pattern, p-lock, playhead ni transport.

## Addendum 2026-05-29 - Patch Poly v2

- Patch v2 etend le seam Z5/Z6 de mono-track vers `P1..P4`, en consommant uniquement l'autorite `voice_group_role` de Z2 pour les groupes master/slaves contigus.
- `P1` conserve l'apply multi-target Patch Assign; `P2/P3/P4` s'appliquent uniquement vers une target master dont le groupe declare a la meme largeur.
- Set reste supprime; Kit reste le snapshot sonore complet machine.


## Addendum 2026-05-29 - Kit V1 etape 2


- Kit V1 ajoute un seam Z5/Z6 distinct de Patch: storage/capture/save/browser/rename/delete pour snapshot sonore complet machine, sans apply dans cette etape.
- HALL1 devient l'entrée workflow Kit en overlay: single tap ouvre le browser après fenêtre double tap, double tap sauvegarde directement; aucun hall mode Kit persistant n'est ajouté.
## Addendum 2026-05-29 - Kit lie au Pattern

- Z6 etend le seam Pattern/Kit: un Pattern porte une reference de slot Kit, tandis que la banque Kit `B6KT` reste l'autorite durable du contenu sonore complet.
- Z5 expose ce lien dans le header principal (`Kit: nom`, dirty `*`, Pattern dessous) et le browser Kit lie le slot applique au pattern actif.
- Set reste retire; Kit reste full-machine sonore, Patch reste separe.

## Addendum 2026-07-25 - TRACK CFG voice group

- Z2 porte les attributs de groupe master/slaves `SPREAD` et `LINK` dans `track_state`; Z5 les expose uniquement en `CFG 2/2` sur une master avec slaves.
- Z3 applique SPREAD via le pan MIX existant et intercepte LINK au point unique d'edition manuelle UI; `PLAY`, p-locks et scheduler restent exclus.
- Z6 persiste ces attributs dans Pattern/Project et les payloads Patch Poly/Kit selon leurs autorites respectives.

## Addendum 2026-07-28 - decision SEQ LINK

- `SEQ LINK` est un attribut structurel de voice group distinct de `CFG GROUP LINK`.
- Autorite cible: `track_state` en Z2, avec lecture master-effective exposee aux consumers. Z4 peut consulter cette projection pour les p-locks non-PLAY, mais le pipeline PLAY conserve son adressage historique par track cible.
- Z5 route l'edition utilisateur vers `PARAM_CFG_GROUP_SEQ_LINK`, Z3 commit dans `track_state`, Z6 persiste l'attribut avec la configuration de groupe. Aucun stockage p-lock PLAY n'est modifie par cette decision.

## Addendum 2026-07-28 - correction pipeline PLAY master group

- `SEQ LINK` ne modifie pas l'ensemble PLAY d'une master de voice group: ON et OFF gardent le meme comportement PLAY.
- En PLAY, une master de groupe schedule les membres, mais chaque membre lit ses propres p-locks/base PLAY `V1` comme avant; la source master ne remplace jamais les notes PLAY des cibles.
- La route commune Z4 peut rester consommee pour la liste bornee des cibles, mais la provenance des p-locks PLAY reste locale a la track cible.

## Addendum 2026-07-28 - Multi Spread Keytrack

- Z2 porte `CFG GROUP SPREAD KEYTRK` comme attribut transient de voice group; Z3 expose `PARAM_CFG_GROUP_SPREAD_KEYTRK` et conserve `LINK` separe.
- Z1 applique le keytrack uniquement dans le rendu `Sampler/Multi`; `KEYTRK=OFF` garde le spread historique par pan MIX.
- Z5 affiche `SPREAD` en double-widget `AMT` + `KEY`, avec `LINK` et `SEQ LINK` conserves sur `CFG/GROUP`.
- Etat courant: le stockage brut `track_state`, le contrat commit unique `param_registry_commit_voice_group_seq_link*()`, la projection `track_runtime_get_voice_group_seq_link()`, l'edition `CFG/GROUP > SEQ LINK`, la persistence Z6, la route logique Z4 pour boundary non-PLAY, les cibles PLAY de groupe et la reconciliation RUNNING post-commit existent.
