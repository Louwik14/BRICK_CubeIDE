# Z4 - Dual-core preparation summary
Statut documentaire: Synthese de reference.
Autorite: les documents de zone restent la source de verite detaillee.

Date: 2026-04-23
Scope: synthese de preparation dual-core pour le sequencer runtime Z4 et ses seams associes.

## 1. Vision cible

- Mono-coeur propre maintenant.
- Dual-core pret plus tard.
- Pas de bus, pas d'IPC, pas de pseudo-infra.
- Preparation par ownerships explicites et seams contractuels.
- Le split reel restera une implementation future, pas une simulation.

## 2. Ownerships principaux

| Domaine | Ownership retenu | Role |
|---|---|---|
| UI / controle | `Src/UI/*`, `ui_core_runtime_bridge`, `ui_navigation`, `ui_core_hall_*` | Entrer les commandes, lire les projections, naviguer et resynchroniser les miroirs. |
| Modele canonique / parametres | `param_registry`, `seq_param_iface`, `track_state`, `track_sound_state`, `track_tone_sound_state` | Autorite de valeur, apply, transition et post-commit sur le domaine param. |
| Projection runtime | `track_runtime`, projections de `seq_runtime`, readbacks `seq_runtime_exec`, diagnostics scheduler | Lire l'etat expose sans ownership local de mutation. |
| Sequencer orchestration | `seq_runtime`, `seq_clock_bridge`, `seq_transport_fsm` | Orchestration transport/clock, politique de cadence, route des evenements. |
| Execution / timeline / audio bloc | `seq_runtime_exec`, `seq_play_scheduler`, `audio.c` comme consommateur bloc | Avance effective, timeline, scheduling, queue et consommation bloc audio. |

## 3. Seams maintenant figes

### `param_registry`
- Surface `query` separable de `command/apply/transition/post-commit`.
- `param_registry` reste l'autorite d'ecriture track-aware.
- Les getters doivent rester purs; les reseeds et resyncs sont explicites.

### `track_runtime`
- Projection runtime lisible.
- Refresh explicite au bord des consumers.
- `get_ctx` n'est plus la voie UI normale.

### `seq_runtime` / `seq_runtime_exec`
- `seq_runtime` orchestre.
- `seq_runtime_exec` porte l'etat de progression, la timeline et l'execution.
- Les call-sites de frontiere sont explicites.

### `seq_clock_bridge` / `seq_transport_fsm`
- `seq_clock_bridge` porte la politique de clock/tempo/source.
- `seq_transport_fsm` porte l'etat transport et les transitions.
- Les helpers hybrides sont bornes comme seams internes.

### `seq_runtime_exec` / `seq_play_scheduler`
- `seq_runtime_exec` avance la timeline et converge les pulses.
- `seq_play_scheduler` porte la queue, le scheduling, l'application et les diagnostics.
- Les points hybrides sont contractualises.

### Front bloc `audio.c`
- `audio.c` consomme la projection bloc runtime.
- `audio.c` applique ensuite les evenements.
- La DSP demi-buffer reste separee de la collecte et de l'application sequencer.

### `ui_core_runtime_bridge`
- Seam explicite entre UI/controle et runtime.
- Les handlers sont classes comme commandes, queries ou notifications selon le contrat.

### Domaine hall
- Le domaine hall reste une surface UI de controle.
- Les transitions d'etat UI et leurs mirrors ne portent pas l'autorite runtime.

## 4. Regles de contrat

- `command`: mutation explicite d'une autorite.
- `query`: lecture pure d'une projection ou d'un miroir.
- `notification`: propagation d'un etat deja etabli, sans ownership de mutation.
- `projection / miroir`: readback stable pour consumers, diagnostics ou UI mirror.
- Single writer par autorite structurelle.
- Refresh explicite au bord des consumers quand le contrat l'exige.
- Pas de relecture implicite qui redevient mutation.

## 5. Ce qui n'est pas encore fait

Prêt structurellement:
- Les seams majeurs sont figees et lisibles.
- Les contrats de lecture/ecriture sont explicitement classes.
- Les consumers critiques savent ou lire et ou commander.

Pas encore fait:
- Le split reel M4/M7.
- Le protocole de transport inter-core.
- La decomposition fine de l'IPC futur.
- Les optimisations liees a la distribution physique des cores.

Volontairement non implemente maintenant:
- Bus de messages.
- Faux IPC.
- Simulation de cores.
- Refactor architectural au-dela des seams deja valides.

## 6. Prochaines etapes possibles

- Doc complementaire si une equipe doit reprendre le split reel.
- Polish des contrats si un consommateur redeviendrait ambigu.
- Optimisation locale seulement si elle ne casse pas les seams.
- Implementation future du split M4/M7 quand la contrainte produit le demandera.

## 7. Reference croisee

Documents de zone a lire en priorite:
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z4_seq_clock_scheduler.md`
- `docs/architecture/z5_ui_navigation_interaction.md`

