# ARCHITECTURE_GLOBAL.md

## 1. Rôle

Ce document est une carte d’orientation.

Il ne détaille pas l’architecture locale.
Il sert uniquement à :
- identifier la bonne zone
- comprendre les dépendances principales entre zones
- savoir quels documents lire avant de modifier le code

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
- taps recorder / buffer dans le pipeline audio

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
- base commune `track_sound_state` + base TONE `track_tone_sound_state` (Sampler + MIDI simple + TRX BD + TRX Claves + TRX HiHat + FM Kick + FM Snare + FM Tom + FM Rimshot + FM Clap + FM Cowbell + FM Cymbal)

Doc :
- `docs/architecture/z3_param_modulation_control.md`

## Z4 — Seq / Clock / Scheduler
Lit ce document si le sujet touche :
- transport
- tempo / clock
- progression musicale
- boundaries
- scheduling sample-accurate
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

Doc :
- `docs/architecture/z6_state_persistence_patterns_projects.md`

---

## 3. Dépendances principales entre zones

- Z0 initialise et cadence tout le reste
- Z1 consomme surtout Z2, Z3 et Z4
- En clock interne/externe sequencer, Z1 fournit la consommation finale d'avance step de Z4 (domaine audio bloc)
- Z2 fournit la vérité runtime aux autres zones
- Z3 applique des valeurs en s’appuyant sur Z2
- En PLAY+REC actif, les edits param track-aware sont rediriges vers Z4 (ecriture p-lock live), sans write runtime direct Z3 en parallele
- Z4 produit les événements temporels consommés par Z1
- Z5 pilote Z2, Z3, Z4 et Z6 via l’interaction utilisateur
- Z6 capture/restaure de l’état qui réimpacte Z2, Z3, Z4 et Z5

---

## 4. Comment choisir quoi lire

### Si le prompt parle de :
- **boot / ordre d’init / boucle principale** → Z0
- **IRQ audio / mix / buffer audio / DMA** → Z1
- **family / type / mix target / runtime bind** → Z2
- **paramètres / LFO / apply / staging** → Z3 (hors redirection live-rec PLAY+REC)
- **transport / tempo / scheduler / live rec séquenceur** → Z4
- **UI / halls / navigation / pages / clipboard** → Z5
- **save/load / patterns / projects / restore** → Z6

### Cas transverses fréquents
- **Input Audio vs Hybrid** → Z2 + Z3 + Z5
- **Master/Buffer** → Z1 + Z2 + Z3 + Z4 + Z5
- **bug track-aware transversal** → commencer par Z2
- **bug après load/restore** → Z6 puis Z2/Z3/Z4/Z5 selon symptôme

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

