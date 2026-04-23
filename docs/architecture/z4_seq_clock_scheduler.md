# Z4 - Seq / Clock / Scheduler

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z4):
- `Src/Seq/seq_runtime.c`
- `Inc/Seq/seq_runtime.h`
- `Src/Seq/seq_play_scheduler.c`
- `Inc/Seq/seq_play_scheduler.h`
- `Src/Seq/seq_boundary_engine.c`
- `Inc/Seq/seq_boundary_engine.h`
- `Src/Seq/seq_model.c`
- `Inc/Seq/seq_model.h`

Elargissements necessaires (preuves de frontieres et contrats):
- `Src/Seq/seq_clock_bridge.c` + `Inc/Seq/seq_clock_bridge.h`: autorite tempo interne/externe, conversion ticks<->step.
- `Src/Seq/seq_transport_fsm.c` + `Inc/Seq/seq_transport_fsm.h`: autorite etats transport STOPPED/START_PENDING/RUNNING.
- `Src/MIDI/midi.c`: source reelle des evenements MIDI clock/start/continue/stop vers `seq_runtime_*_from_source` et IRQ TIM12 qui cadence l'horloge interne.
- `Src/Audio/audio.c`: preuve de consommation audio-block des evenements sequenceur (`seq_runtime_audio_collect_block_events`, `seq_runtime_audio_apply_event`).

Dependances de Z4 sans appartenir a Z4:
- `track_runtime` (eligibilite/gating runtime par track dans le scheduler PLAY).
- `seq_param_iface` (mapping set/param et apply/restore de locks).
- `seq_output_guard` (anti doublons note-on/note-off et panic stop).
- engines/mixer/MIDI output (`monob`, `drum`, `brick6_sampler_runtime`, `mixer`, `midi`) appeles par le scheduler applique.
- `ui_core` (source de contexte UI pour les commandes explicites envoyees vers Z4, sans lecture implicite du focus UI par `seq_param_iface`).
- `seq_live_rec_capture` et `seq_edit` pour le chemin capture live-rec.

Exclusions explicites:
- UI pages/rendering et persistence (`Src/UI/*`, `Src/Storage/*`) ne portent pas l'autorite clock/scheduler; elles configurent/consomment seulement.
- `seq_edit`, `seq_clipboard`, `seq_led` ne possedent pas la progression temporelle ni le scheduler bloc audio.

Sous-roles concentres dans `seq_runtime.c`:
- Orchestrateur transport+clock runtime.
- Pilotage des boundaries et scheduling pas.
- Bridge vers domaine sample/audio bloc.
- Pilotage live-rec pattern/overdub autour du transport.

## 2. Autorite(s) de verite

Autorite transport:
- `seq_runtime_start`, `seq_runtime_stop`, `seq_runtime_toggle_play_stop`.
- Decisions d'etat via `seq_transport_fsm_request_start/stop/continue`, `seq_transport_fsm_on_step_pulse`, `seq_transport_fsm_abort_pending`.

Autorite clock/tempo:
- Source clock active: `seq_runtime_set_clock_source` (mutations sur `g_seq_runtime.clock_src` via `seq_clock_bridge_set_source`).
- Tempo interne: `seq_runtime_set_tempo_bpm_milli` -> `seq_clock_bridge_set_internal_tempo`.
- Tempo externe: `seq_clock_bridge_on_external_clock_pulse` (appele depuis `seq_runtime_midi_clock_from_source`).
- Cadence interne effective (steps): `seq_runtime_audio_collect_block_events` -> `seq_runtime_audio_drive_internal_steps_for_block` (domaine audio sample, IRQ DMA).
- Tick interne auxiliaire: `seq_runtime_time_adapter_process_internal_from_irq` (IRQ TIM12) conserve un compteur de temps, sans autorite d'avance step en clock interne.

Autorite position musicale (step/boundary):
- Avance step: `seq_boundary_engine_advance_one_step`.
- Detection/changement boundary: `seq_boundary_engine_process`.
- Orchestration boundary: `seq_runtime_process_step_boundaries`.

Autorite scheduling d'evenements:
- Generation note events: `seq_play_scheduler_schedule_step`.
- Queue sample-domain: `g_seq_play_events[]` dans `seq_play_scheduler.c`.
- Collecte audio bloc: `seq_play_scheduler_audio_collect_block_events`, exposee via `seq_runtime_audio_collect_block_events`.

Autorite collecte evenements audio par bloc:
- `seq_runtime_audio_collect_block_events` (met a jour timeline audio et rapatrie les evenements dus dans le bloc).
- Application evenement: `seq_runtime_audio_apply_event` -> `seq_play_scheduler_audio_apply_event`.

Seconde autorite concurrente:
- Aucune seconde autorite concurrente de meme niveau n'est observee pour transport/clock/step progression/scheduling bloc.
- `seq_clock_bridge` et `seq_transport_fsm` sont des sous-autorites internes a Z4, invoquees par `seq_runtime`.

## 3. API entrantes

Entrees directes de Z4:
- Boot/init: `seq_runtime_init` (appele par `Src/Core/brick6_app_init.c`).
- Cadence interne: `seq_runtime_time_adapter_process_internal_from_irq` (IRQ TIM12, `midi.c`, source de tick interne).
- Traitement runtime (interne + externe): `seq_runtime_time_adapter_process` (superloop).
- Transport utilisateur: `seq_runtime_toggle_play_stop`, `seq_runtime_start`, `seq_runtime_stop` (UI/param).
- Realtime MIDI clock transport: `seq_runtime_midi_clock_from_source`, `seq_runtime_midi_start_from_source`, `seq_runtime_midi_continue_from_source`, `seq_runtime_midi_stop_from_source` (depuis `midi_internal_receive_with_source` dans `midi.c`).
- Configuration runtime: `seq_runtime_set_clock_source`, `seq_runtime_set_tempo_bpm_milli`, `seq_runtime_set_track_div/quant/swing`, `seq_runtime_on_track_length_changed`.
- Contexte pattern-rec explicite depuis UI: `seq_runtime_set_pattern_rec_target_track` (la UI pousse le focus d'edition, Z4 ne lit plus directement `ui_get_active_track`).
- Live-rec entree notes: `seq_runtime_live_rec_note_on/off` (keyboard engine).
- Live-rec entree param: `seq_runtime_live_rec_param_write` (edits param track-scoped en PLAY+REC).
- Consommation audio: `seq_runtime_audio_collect_block_events`, `seq_runtime_audio_apply_event` (audio IRQ `process_half` dans `audio.c`).

Contrats implicites d'entree:
- En clock interne, la progression step ne depend plus du superloop: elle est drivee au debut de chaque bloc audio dans `seq_runtime_audio_collect_block_events`.
- TIM12 reste un ticker auxiliaire (metriques/temps), non autorite d'avance step.
- En source externe, les pulses MIDI clock (0xF8) arrivent via `midi_internal_receive_with_source` et sont convertis en pending step-pulses; leur consommation effective pour l'avance step se fait en debut de bloc audio dans `seq_runtime_audio_collect_block_events`.
- `seq_runtime_audio_collect_block_events` est suppose appele une fois par bloc audio dans l'ordre de timeline; il incremente `audio_timeline_sample`.

## 4. API sortantes

Sorties vers autres zones:
- Vers audio runtime: paquet d'evenements sequenceur sample-offset (`seq_runtime_audio_event_t`) via `seq_runtime_audio_collect_block_events`.
- Vers moteurs/sorties note: `seq_play_scheduler_audio_apply_event` envoie MIDI (`midi_note_on/off`) et notes engines (`drum_synth_*`, `brick6_sampler_runtime_*`), plus gate mixer (`mixer_track_filter_*`, `mixer_track_vca_*`).
- Vers param domaine lock: `seq_boundary_engine_*` appelle `seq_param_iface_apply_lock`, `seq_param_iface_restore_base`.
- Vers UI param (PLAY+REC): `ui_param` route l'edit track-scoped vers `seq_runtime_live_rec_param_write` (ecriture p-lock), sans write runtime direct concurrent.
- Vers UI param (hors PLAY+REC): `ui_param` emet une commande explicite `seq_param_iface_commit_base_after_authoritative_apply(cmd)` apres `param_registry_apply_track_value(...)`; `seq_param_iface` ne relit pas `ui_get_active_track()` pour valider ce commit.
- Vers clock MIDI sortant: `seq_runtime_send_transport_realtime`, `midi_clock`, `midi_clock_set_*`.
- Vers securite sortie: `seq_output_guard_*` (`panic`, note state).

Contrats implicites de sortie:
- Les evenements collectes sont exprimes en `sample_offset_in_block`, relies a la taille `block_frames` fournie par audio.
- Les callbacks audio appellent `seq_runtime_audio_apply_event` exactement aux offsets produits, pour conserver l'alignement temporel.

## 5. Etats structurants possedes

Etat runtime principal (`seq_runtime.c`):
- `g_seq_runtime` (`seq_runtime_state_t`, defini dans `Inc/Seq/seq_runtime.h`)
  - Champs structurants: `running`, `clock_src`, `play_step[]`, `prev_step[]`, `prev_step_valid[]`, `track_div[]`, `track_div_phase[]`, `track_quant[]`, `track_swing[]`, `tick_accum`, `ticks_per_step`, `ext_clock_tick_accum`, `step_sample_q16`, `samples_per_step_q16`, `audio_block_start_sample`, `audio_timeline_sample`, `active_locks[][]`, `active_lock_count[]`.
  - Ecriture: `seq_runtime_init/start/stop/process_core/process_step_pulse`, `seq_boundary_engine_process/advance_one_step/restore_all_active_locks`, setters track.*.
  - Lecture: getters runtime, scheduler/boundary internes, `brick6_master_buffer`, UI/param/storage (etat expose).

Etat transport:
- `g_seq_transport_fsm` (`seq_transport_fsm_t`)
  - Ecriture: `seq_transport_fsm_init/request_start/request_stop/request_continue/on_step_pulse/abort_pending`.
  - Lecture: `seq_transport_fsm_is_*`, `allow_*`, `get_rec_count_in_remaining_steps`.
  - Role: autorite STOPPED/START_PENDING/RUNNING et count-in.

Etat clock bridge:
- `g_seq_clock_bridge` (`seq_clock_bridge_t`)
  - Ecriture: `seq_clock_bridge_init/set_source/set_internal_tempo/on_external_clock_pulse/on_process/prepare_internal_run`.
  - Lecture: `seq_clock_bridge_get_internal_tempo_bpm_milli`, `is_external_tempo_valid`, `get_external_tempo_bpm_milli`, `internal_next_step_ticks`.
  - Role: conversion tempo<->ticks et estimation tempo externe.

Etat tick/tempo auxiliaire:
- `g_seq_internal_time_tick` (tick interne incremente en IRQ TIM12).
- `g_seq_midi_clock_tick_accum`.
- `g_seq_midi_clock_audio_enabled`, `g_seq_midi_clock_period_q16`, `g_seq_midi_clock_next_sample_q16` (clock MIDI TX aligne sur timeline audio bloc).

Etat live-rec runtime:
- `g_seq_rec_armed`, `g_seq_rec_count_in_mode`, `g_seq_rec_len_mode`.
- `g_seq_pattern_rec_pending_start`, `g_seq_pattern_rec_active`, `g_seq_pattern_rec_track`, `g_seq_pattern_rec_steps_remaining`.
- Ecriture via `seq_runtime_rec_toggle_arm`, `seq_runtime_set_rec_*`, `seq_runtime_pattern_rec_*`, stop lifecycle.
- Lecture via getters et conditions de capture note on/off.

Etat scheduler audio (`seq_play_scheduler.c`):
- `g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP]` (`seq_play_scheduler_evt_t`: `due_sample_time`, `track`, `note`, `velocity`, `type`, `audio_dispatched`, `generation`).
- `g_seq_play_event_count`, `g_seq_play_generation`.
- Ecriture: `seq_play_scheduler_push`, `seq_play_scheduler_clear`, compaction dans `seq_play_scheduler_audio_collect_block_events`.
- Lecture: collecte bloc et apply event.

Etat modele sequenceur (`seq_model.c`):
- `g_seq_project` (`seq_project_data_t`): tracks/steps/trigs/plock pool/free list.
- Ecriture: APIs `seq_model_set_*`, `seq_model_step_plock_*`, `seq_model_load_project`, `seq_model_init_defaults`.
- Lecture: runtime boundary/scheduler/edit/UI/storage.

## 6. Flux runtime

Flux nominal prouve:
1. Source tempo/clock
- Interne: au debut de chaque bloc audio, `seq_runtime_audio_collect_block_events` appelle `seq_runtime_audio_drive_internal_steps_for_block`; les pulses de step sont derives de `audio_timeline_sample`/`samples_per_step_q16`.
- Externe MIDI/USB: `midi_internal_receive_with_source` route 0xF8/0xFA/0xFB/0xFC vers `seq_runtime_midi_*_from_source`.

2. Start/stop/continue transport
- `seq_runtime_start` initialise accumulators, clear scheduler/output guard/live-rec puis requete FSM.
- `seq_runtime_stop` applique `seq_runtime_stop_lifecycle_apply` (flush live-rec, clear scheduler, restore locks, panic sortie conditionnelle).
- `seq_runtime_midi_continue_from_source` reprend RUNNING sans reset complet du modele et rebase timeline musicale si reprise depuis STOP.

3. Progression temporelle
- Interne: `seq_runtime_audio_drive_internal_steps_for_block` produit les pulses strictement dans la timeline audio absolue.
- Externe: `seq_runtime_midi_clock_from_source` met en file des pending step-pulses; `seq_runtime_audio_drive_external_steps_for_block` les consomme dans le domaine audio bloc.
- L'avance step (interne/externe) converge sur `seq_runtime_process_step_pulse_at_sample_q16`.

4. Detection boundary / advance pattern
- `seq_runtime_process_step_pulse`:
  - si RUNNING et autorise: `seq_boundary_engine_advance_one_step` (respect `track_div`/`track_div_phase`).
  - incremente `step_sample_q16`.
  - appelle `seq_runtime_process_step_boundaries`.
- `seq_runtime_process_step_boundaries` -> `seq_boundary_engine_process` detecte boundaries track par track et gere apply/restore locks.

5. Generation/collecte des evenements
- Pour chaque `seq_boundary_hit_t`, `seq_play_scheduler_schedule_step` lit trig/plocks/param defaults et pousse NOTE_ON/NOTE_OFF horodates en sample-domain.
- Quand aucun plock `PLAY` n'est present, la valeur de base vient maintenant de l'autorite seq canonique (`seq_param_iface_get_base_value`) et non d'un fallback default descriptor.

6. Scheduling bloc audio
- Au debut de chaque bloc audio, `seq_runtime_audio_collect_block_events`:
  - capture `block_start_sample = g_seq_runtime.audio_timeline_sample`.
  - incremente `audio_timeline_sample += block_frames`.
  - emet clocks MIDI audio-alignes (`seq_runtime_midi_clock_audio_emit_for_block`).
  - collecte depuis `seq_play_scheduler_audio_collect_block_events` les events dus dans `[block_start, block_end)` avec `sample_offset_in_block`.

7. Consommation aval audio/runtime/param
- `audio.c` applique les events aux offsets via `seq_runtime_audio_apply_event`.
- `seq_play_scheduler_audio_apply_event` envoie MIDI note et note engine + gate mixer.
- Les locks de pas affectent domaine param via `seq_boundary_engine` + `seq_param_iface_apply_lock/restore_base`.

## 7. Contraintes RT/CPU/memoire

Contraintes RT observees:
- Pas de malloc dans les chemins critiques Z4: etats statiques (`g_seq_runtime`, `g_seq_project`, `g_seq_play_events`, FSM/bridge).
- Sections critiques IRQ explicites dans runtime et scheduler (`__disable_irq`/`__enable_irq`).
- Mutations du pool/listes p-lock (`seq_model_step_plock_upsert/delete/clear`) executees en section critique IRQ pour garantir la coherence avec la lecture boundary runtime en IRQ.
- Chemin audio-bloc borne par capacites fixes:
  - scheduler collect par tranches de 16 events dans `seq_runtime_audio_collect_block_events`.
  - queue globale capee a `SEQ_PLAY_SCHEDULER_EVENT_CAP` (256).

Contraintes CPU/ordre:
- Cohesion temporelle dependante de l'ordre:
  - progression `audio_timeline_sample` dans `seq_runtime_audio_collect_block_events`.
  - application des events dans le meme bloc audio en ordre d'offset.
- `seq_runtime_process_core` reste requis pour transport, supervision bridge externe et etats transport.
- En source interne comme externe, la regularite des steps depend de la cadence audio bloc (pas du jitter superloop).

Memoire/statique:
- Stockage modele/plocks et queues integralement statiques, sans allocation dynamique runtime.

## 8. Invariants a ne pas casser

Invariants prouves par le code:
- Autorite unique transport/clock/scheduling: `seq_runtime` orchestre, `seq_transport_fsm` et `seq_clock_bridge` sous-ordonnes.
- Separation modele sequence vs scheduling audio:
  - `seq_model` porte donnees pattern/plocks.
  - `seq_play_scheduler` porte file d'evenements sample-domain.
- Pas de mutation cachee dans getters principaux Z4:
  - `seq_runtime_get_state`, `seq_runtime_get_playhead_step`, `seq_runtime_get_track_div/quant/swing`, `seq_runtime_get_tempo_bpm_milli`, `seq_model_get_*` lisent sans muter.
- Conditions de boundary explicites:
  - boundary hit si `prev_step_valid==0` ou `prev_step != current_step`.
  - `seq_boundary_engine_process` fait apply/restore locks avant emission hit.
- Integrite de parcours p-lock:
  - les parcours de listes p-lock cote modele (`find/mask/get_at`) sont bornes par la capacite pool track pour eviter toute boucle non bornee en presence de structure corrompue.
- Cohérence bloc audio / temps musical:
  - step scheduling en `due_sample_time` absolu.
  - conversion vers offset relatif au bloc lors de la collecte.

- Contrat quant/swing runtime:
  - `track_quant` reste applique avant scheduling audio des notes, sans modifier `play_step` ni boundaries.
  - `track_quant` est un pourcentage `0..100` (0 = neutre) qui resserre progressivement le micro-timing des notes vers la grille (`mictim -> 0`).
  - `track_swing` est conserve pour compatibilite UI/persistence, mais n'a plus d'effet sur `due_sample_time`/offsets runtime.

## 9. Dependances inter-zones

Entrees vers Z4:
- Z5 UI/Interaction: commandes transport, track settings, active track/MIDI channel.
- Z5 UI/Param: en PLAY+REC actif et sans hold-step manuel, les edits param track-scoped sont rediriges vers Z4 (`seq_runtime_live_rec_param_write`).
- Z3 Param/Control: tempo/clock source/track div-quant-swing via `param_registry`.
- Z1 Audio Hard-RT: rythme de collecte/apply des events au bloc.
- MIDI I/O: source d'horloge externe et transport realtime.

Sorties de Z4:
- Z1 Audio Hard-RT: evenements notes sample-offset par bloc.
- Z2 Track Runtime Authority: consultation context/param status dans scheduler PLAY.
- Z3 Param domain: apply/restore locks via `seq_param_iface`.
- Sorties MIDI et engines audio (via scheduler apply).

## 10. Dette technique observee

Points factuels observes:
- Concentration de responsabilites dans `seq_runtime.c` (transport, clock, boundary orchestration, scheduler bridge, live-rec, MIDI clock audio TX) augmente le couplage interne.
- Dependance forte a l'ordre d'appel:
  - sans pulses externes, la progression externe stoppe.
  - la progression step (interne/externe) depend d'un appel audio regulier a `seq_runtime_audio_collect_block_events`.
  - `seq_runtime_time_adapter_process` reste necessaire pour transport et supervision bridge externe, mais n'est plus autorite d'avance step.
- Couplage implicite avec UI dans le coeur runtime:
  - Le bind pattern-rec ne lit plus l'etat UI directement; il consomme une cible explicite (`seq_runtime_set_pattern_rec_target_track`) fournie par Z5.
  - Le focus UI reste un contexte d'entree (source du target track), plus une verite runtime lue depuis Z4.
  - Le commit base post-apply UI->Seq (`seq_param_iface_commit_base_after_authoritative_apply`) consomme un contrat explicite (source + target track + set/param + precondition apply) sans relecture de focus UI global.
  - Le channel MIDI runtime du scheduler/live-rec/output-guard passe desormais via Z2 (`track_runtime_get_midi_channel_*`) et non par lecture directe UI.
  - Le scheduler note-engine consomme `track_runtime_resolve_track` (descriptor + cibles resolues) au lieu de re-resoudre localement filter/mix/gate.
- Double logique tempo interne/externe assumee mais pas concurrente active; bascule source explicite via `seq_runtime_set_clock_source`.
- Indice de dette documente dans le code:
  - commentaire `TODO(clock-source)` dans `seq_runtime_init` sur branchement source clock globale/menu.
- Parametres runtime quant/swing:
  - l'avance du playhead reste sous autorite `seq_boundary_engine_advance_one_step`,
  - seul `quant` deplace l'horodatage sample-domain des events (pas la progression musicale),
  - `swing` reste stocke/expose pour compatibilite mais est inerte dans le scheduling runtime.

## 11. Impact eventuel sur la cartographie globale

- Z4 est confirmee comme zone coherente unique pour l'autorite temporelle sequencer (transport/clock/position/scheduler bloc).
- Pas de justification code pour scinder transport et scheduler en deux zones separees a ce stade; les frontieres internes sont nettes mais l'orchestration est centralee dans `seq_runtime`.
- Couplages UI (active track, MIDI channel) et audio-bloc doivent etre references explicitement dans la carte globale comme dependances entrantes critiques de Z4.



## 12. Contrat Program v1 (MIDI + Hybrid)

- Perimetre Z4 actif:
  - emission Program Change runtime depuis `seq_play_scheduler`/`seq_runtime`,
  - concerne les tracks `MIDI` et `Input/Hybrid` (pas de backend audio supplementaire).
- Semantique Program:
  - `OFF` (`PARAM_MIDI_PROGRAM==0`) => aucune emission,
  - changement live => emission immediate conditionnelle une fois,
  - start transport => emission forcee une fois si Program != OFF,
  - pattern change => emission forcee une fois,
  - aucune reemission automatique a chaque loop.
- Semantique p-lock Program:
  - resolution plock/default au scheduling du step,
  - emission planifiee avant note sur un meme step,
  - emission seulement si Program differe du dernier Program effectivement envoye,
  - pas de reemission a la loop si rien n'a change,
  - reemission si un autre Program a ete envoye entre-temps.
- Autorite d'etat minimale:
  - memoire `dernier_program_envoye` par track concernee dans `seq_play_scheduler` (etat statique borne, sans allocation dynamique).
- Limite explicite v1:
  - le "pattern change" est detecte via reset playhead track a `0` en `RUNNING` (`seq_runtime_set_playhead_step`),
  - donc un reset manuel playhead en `RUNNING` declenche aussi cette emission Program forcee.

## 13. Contrat Hybrid Gate v1 (canonique)

- `Input/Hybrid` participe au gate note cote scheduler (`seq_play_scheduler_emit_engine_note`).
- Alignement clavier/sequenceur:
  - scheduler et clavier appliquent le meme principe d'ouverture/fermeture du gate VCA pour `Input/Hybrid`.
- `Sampler` passe par le meme helper central de gate VCA (`track_runtime_supports_vca_gate`) pour ouvrir/fermer le mixer gate sur note-on/off.
- Gate partage (cote mixer/VCA):
  - premiere note active ouvre le gate,
  - le gate reste ouvert tant qu'au moins une note est active,
  - la derniere note relachee ferme le gate.
- Contraintes explicites:
  - pas de vraie polyphonie audio ajoutee (toujours un flux input unique gate),
  - `panic` / `all notes off` referme proprement le gate (`mixer_track_vca_all_notes_off`).

