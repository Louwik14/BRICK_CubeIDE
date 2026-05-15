# Z4 - Seq / Clock / Scheduler

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z4):
- `Src/Seq/seq_runtime.c`
- `Inc/Seq/seq_runtime.h`
- `Src/Seq/seq_runtime_exec.c`
- `Inc/Seq/seq_runtime_exec.h`
- `Src/Seq/seq_play_scheduler.c`
- `Inc/Seq/seq_play_scheduler.h`
- `Src/Seq/seq_boundary_engine.c`
- `Inc/Seq/seq_boundary_engine.h`
- `Src/Seq/seq_model.c`
- `Inc/Seq/seq_model.h`

Elargissements necessaires (preuves de frontieres et contrats):
- `Src/Seq/seq_clock_bridge.c` + `Inc/Seq/seq_clock_bridge.h`: autorite tempo interne/externe, conversion ticks<->step.
- `Src/Seq/seq_transport_fsm.c` + `Inc/Seq/seq_transport_fsm.h`: autorite etats transport STOPPED/START_PENDING/RUNNING.
- `Src/Seq/seq_live_rec_session.c` + `Inc/Seq/seq_live_rec_session.h`: autorite session live-rec/edit, arming/count-in/pattern-rec et ecriture p-lock live.
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
- Facade vers la session live-rec; le detail edit/capture vit dans `seq_live_rec_session`.

Support execution:
- `seq_runtime_exec.c` + `seq_runtime_exec.h`: proprietaire de l'etat runtime de progression/timeline partage; `seq_runtime.c` y accede via facade interne.
- Il porte aussi la timeline audio bloc, la consommation des pulses de step, la remise en route/arrêt de l'etat d'execution, la preparation de start lifecycle, la cadence MIDI clock audio-alignee, et l'avance interne/externe du bloc audio.
- L'etat partage (`seq_runtime_state_t`) est explicitement owned par `seq_runtime_exec`; `seq_runtime.c` ne le manipule qu'en facade d'orchestration.

## 2.b Contrat de frontière `seq_runtime` / `seq_runtime_exec`

Surface `seq_runtime`:
- orchestration et policy transport/clock
- route des evenements externes vers le runtime
- commandes live-rec / rec-arming / tempo / clock source / track-control
- queries de haut niveau sur l'etat runtime, le transport et les diagnostics

Surface `seq_runtime_exec`:
- proprietaire de l'etat d'execution partage
- timeline audio bloc
- progression step / pulses / scheduling temporel
- lifecycle execution start/stop
- cadence MIDI clock audio-alignee

Ambiguite bornee restante:
- `seq_runtime_audio_collect_block_events` est une query de collecte avec avance de timeline, mais son role de seam audio bloc reste explicite.
- `seq_runtime.c` conserve des alias internes vers l'etat d'execution pour l'orchestration, mais la propriete canonique reste `seq_runtime_exec`.

Call-sites de frontiere:
- `seq_runtime.c` orchestre les appels vers `seq_runtime_exec_*` comme un controlleur, pas comme un second owner.
- `audio.c` consomme `seq_runtime_audio_collect_block_events` comme seam audio bloc unique avant application des events.

## 2.c APIs hybrides

APIs de `seq_runtime` qui traversent plusieurs natures mais restent contractuellement bornees:
- `seq_runtime_audio_collect_block_events`: query de collecte avec avance explicite de timeline audio.
- `seq_runtime_time_adapter_process_internal_from_irq`: maintenance/notification d'IRQ, sans autorite de progression step.
- `seq_runtime_time_adapter_process`: orchestration de supervision runtime, pas de proprietaire d'etat.
- `seq_runtime_midi_clock_from_source` / `seq_runtime_midi_start_from_source` / `seq_runtime_midi_continue_from_source` / `seq_runtime_midi_stop_from_source`: notifications d'entree transport/MIDI qui alimentent la policy runtime.
- `seq_runtime_live_rec_param_write`: commande live-rec routee par la policy runtime vers l'autorite p-lock.
- `seq_runtime_on_midi_program_live_change` / `seq_runtime_on_track_pattern_change`: notifications post-commit de domaine runtime.

## 2.d Petite spec de synchronisation future

Sans bus, sans IPC, sans file de messages:

Commandes:
- `seq_runtime_set_clock_source`
- `seq_runtime_set_tempo_bpm_milli`
- `seq_runtime_start` / `seq_runtime_stop` / `seq_runtime_toggle_play_stop`
- `seq_runtime_set_rec_count_in_mode` / `seq_runtime_set_rec_len_mode`
- `seq_runtime_set_pattern_rec_target_track`
- `seq_runtime_live_rec_param_write`

Queries:
- `seq_runtime_get_state`
- `seq_runtime_is_running` / `seq_runtime_is_start_pending`
- `seq_runtime_get_rec_count_in_mode` / `seq_runtime_get_rec_len_mode`
- `seq_runtime_get_rec_count_in_remaining_steps`
- `seq_runtime_rec_is_armed` / `seq_runtime_rec_is_pattern_pending_start`
- `seq_runtime_get_track_loop_generation`
- `seq_runtime_exec_state_const`
- `seq_runtime_exec_get_audio_timeline_sample`

Notifications:
- `seq_runtime_time_adapter_process_internal_from_irq`
- `seq_runtime_time_adapter_process`
- `seq_runtime_midi_clock_from_source`
- `seq_runtime_midi_start_from_source`
- `seq_runtime_midi_continue_from_source`
- `seq_runtime_midi_stop_from_source`
- `seq_runtime_on_midi_program_live_change`
- `seq_runtime_on_track_pattern_change`

Projection / miroir:
- `seq_runtime_audio_collect_block_events` comme projection bloc audio lisible par l'IRQ audio
- markers `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE` comme projection edge-based des boundaries reelles, avec offset sample dans le bloc audio
- `seq_runtime_get_samples_per_step_q16` comme miroir scalaire explicite pour les conversions step->frames
- `seq_runtime_exec_state_const` comme miroir readonly de la timeline et de la progression

## 2.e Consommateurs non-UI de projection

Lectures runtime explicites, sans autorite locale de mutation:
- `seq_boundary_engine`: lit `seq_runtime_get_track_div` comme garde de boundary.
- `seq_play_scheduler`: lit `seq_runtime_get_track_quant` et les projections `track_runtime_*` pour calculer le scheduling.
- `seq_live_rec_session`: lit `seq_runtime_is_running`, `seq_runtime_get_rec_count_in_remaining_steps`, `seq_runtime_get_track_div` et `track_runtime_get_midi_source` comme gardes de session.
- `seq_live_rec_capture`: lit `track_runtime_get_midi_source`, `track_runtime_get_midi_channel_zero_based` et `track_runtime_get_effective_param_status` comme gardes de capture.
- `seq_led`: lit `seq_runtime_is_running` et `seq_runtime_get_playhead_step` comme projections de cursor.
- `seq_output_guard`: lit `track_runtime_get_midi_channel_zero_based` et la projection runtime resolue pour le panic/cleanup.

Contrat:
- ces consumers ne font pas de refresh implicite;
- tout refresh requis reste au bord de l'orchestrateur appelant;
- les getters runtime restent des projections pures ou des miroirs explicites, jamais des commandes cachees.

## 2.f Consommateurs non-UI de commande

Commandes runtime explicites, avec readback miroir quand le caller doit resynchroniser son store/UI:
- `param_registry_apply_wrappers.c`: `apply_cfg_rec`, `apply_cfg_tempo`, `apply_cfg_sync`, `apply_cfg_rec_len`, `apply_seq_div`, `apply_seq_quant`, `apply_seq_swing`.
- `ui_core_seq_transport.c`: `seq_runtime_toggle_play_stop`, `seq_runtime_set_pattern_rec_target_track`, `seq_runtime_rec_toggle_arm`, et les commandes buffer associées.

Contrat:
- ces chemins écrivent une autorite runtime explicite;
- les lectures qui suivent servent uniquement de miroir post-apply;
- aucune lecture ne doit etre interpretee comme un trigger de mutation cache.

## 2.g Notifications / post-commit

Notifications explicites, emises apres mutation ou progression d'etat:
- `seq_runtime_on_midi_program_live_change`
- `seq_runtime_on_track_pattern_change`
- `seq_runtime_time_adapter_process_internal_from_irq`
- `seq_runtime_time_adapter_process`
- `seq_runtime_midi_clock_from_source`
- `seq_runtime_midi_start_from_source`
- `seq_runtime_midi_continue_from_source`
- `seq_runtime_midi_stop_from_source`
- `seq_runtime_audio_collect_block_events`

Contrat:
- ces fonctions ne portent pas l'autorite de mutation principale;
- elles notifient ou propagent un etat deja etabli;
- les miroirs UI/post-apply qui suivent une commande restent des copies explicites, pas des triggers caches.

## 2.h Contrat interne `seq_clock_bridge` / `seq_transport_fsm`

Surface `seq_clock_bridge`:
- politique d'horloge et de tempo
- source clock interne/externe
- conversion tempo/ticks et estimation tempo externe
- conversion des pulses transport vers la cadence runtime

Surface `seq_transport_fsm`:
- etat transport STOPPED/START_PENDING/RUNNING
- transitions start/continue/stop
- count-in et autorisations de progression
- pas de politique d'horloge ou de tempo

Ambiguite bornee restante:
- `seq_clock_bridge_on_external_clock_pulse` nourrit la progression, mais ne possede pas l'autorite transport.
- `seq_runtime` reste la facade d'orchestration entre les deux sous-autorites.

## 2.i APIs hybrides internes

APIs hybrides bornees:
- `seq_clock_bridge_on_process`: supervision de clock policy uniquement.
- `seq_clock_bridge_set_source`: reinitialisation de cadence et accumulateurs de tick.
- `seq_clock_bridge_consume_internal_step_due`: consommation du prochain step interne du budget de tick accumule.
- `seq_clock_bridge_on_external_clock_pulse`: conversion d'impulsions externes en demande de step.
- `seq_clock_bridge_set_internal_tempo`: reparametrage de la cadence interne.
- `seq_transport_fsm_reset`: remise a zero du transport sans politique d'horloge.
- `seq_transport_fsm_request_start` / `seq_transport_fsm_request_continue`: transitions de transport, pas de politique de tempo.
- `seq_transport_fsm_abort_pending`: annulation d'un start en attente.
- `seq_transport_fsm_on_step_pulse`: consommation d'une impulsion de step pour faire avancer l'etat transport.
- `seq_transport_fsm_allow_advance` / `seq_transport_fsm_allow_schedule_play` / `seq_transport_fsm_allow_live_rec`: queries de garde derivees de l'etat transport.

Contrat:
- `seq_clock_bridge` decide la cadence et la source;
- `seq_transport_fsm` decide l'etat et les transitions;
- les fonctions hybrides ne doivent pas masquer une seconde autorite.

Contrat complementaire:
- `seq_clock_bridge_consume_internal_step_due` reste un helper de cadence interne, sans autorite transport.
- `seq_transport_fsm_reset` et `seq_transport_fsm_abort_pending` restent des helpers de cycle de vie interne, sans politique d'horloge.

## 2.k Garde de progression

La progression temporelle est bornee par des gardes explicites:
- `seq_transport_fsm_on_step_pulse` consomme un pulse de step pour basculer `START_PENDING -> RUNNING`.
- `seq_transport_fsm_allow_advance` autorise l'avance musicale uniquement quand le transport est RUNNING.
- `seq_transport_fsm_allow_schedule_play` autorise le scheduling uniquement quand le transport est RUNNING.
- `seq_clock_bridge_on_external_clock_pulse` convertit un pulse externe en demande de step, sans prendre l'autorite de progression elle-meme.
- `seq_runtime_exec_process_step_pulse_at_sample_q16` est le point unique qui applique la progression musicale.
- `seq_runtime_exec_drive_internal_steps_for_block` et `seq_runtime_exec_drive_external_steps_for_block` limitent cette progression au domaine bloc audio.

Contrat:
- la cadence produit des pulses;
- le transport decide quand ces pulses avancent l'etat;
- la progression musicale n'est appliquee qu'au point de convergence runtime-exec.

## 2.j Call-sites d'orchestration

`seq_runtime` reste la facade d'orchestration entre clock policy et transport FSM:
- `seq_runtime_start`: prepare la cadence via `seq_clock_bridge`, puis delegue la transition START a `seq_transport_fsm`.
- `seq_runtime_stop`: delegue STOP a `seq_transport_fsm`, puis applique le lifecycle d'arret runtime.
- `seq_runtime_process_core`: supervise `seq_clock_bridge_on_process` et les etats transport, sans prendre la propriete de l'un ou de l'autre.
- `seq_runtime_midi_clock_from_source`: fait passer la pulsation externe par `seq_clock_bridge_on_external_clock_pulse`, puis pousse la demande de step vers le domaine runtime.
- `seq_runtime_midi_continue_from_source`: delegue la transition CONTINUE a `seq_transport_fsm`, puis rebase la timeline de progression.

Contrat:
- `seq_runtime` orchestre, il ne remplace ni la politique de clock ni l'autorite transport;
- les appels sont intensionnellement sequentiels: policy d'abord, transport ensuite, puis lifecycle si necessaire.

## 2.l Contrat interne `seq_runtime_exec` / `seq_play_scheduler`

Surface `seq_runtime_exec`:
- progression effective et timeline audio bloc;
- convergence des pulses internes / externes vers le point d'avance step;
- lifecycle execution start/stop;
- collecte bloc qui avance la timeline puis draine les evenements dus.

Surface `seq_play_scheduler`:
- scheduling note/program a partir des boundaries resolues;
- queue sample-domain des evenements dus;
- projection audio bloc des evenements dus;
- application MIDI / engine / mixer des evenements en queue;
- diagnostics de queue et de collecte.

Ambiguite bornee restante:
- `seq_runtime_exec_collect_block_events` reste le point de convergence bloc: il orchestre progression et collecte, mais ne possede pas la queue des evenements.
- `seq_play_scheduler_audio_collect_block_events` reste une projection de queue: elle n'avance ni transport ni timeline.

Contrat:
- `seq_runtime_exec` avance la timeline et produit le contexte de bloc;
- `seq_play_scheduler` produit, projette et applique les evenements;
- le seam entre les deux reste contractuel mais sans double autorite.

## 2.m APIs hybrides internes complementaires

APIs hybrides bornees cote `seq_runtime_exec`:
- `seq_runtime_exec_begin_running_at_sample_q16`: lifecycle start qui seed l'execution puis autorise le scheduler.
- `seq_runtime_exec_stop_lifecycle_apply`: lifecycle stop qui flush l'execution et vide la queue scheduler.
- `seq_runtime_exec_process_step_pulse_at_sample_q16`: point de convergence de progression, pas proprietaire du scheduler.
- `seq_runtime_exec_drive_internal_steps_for_block` / `seq_runtime_exec_drive_external_steps_for_block`: helpers de bloc qui transforment cadence en pulses puis en progression runtime.

APIs post-commit cote `seq_play_scheduler`:
- `seq_play_scheduler_live_midi_program_changed`: notification apres commit runtime, refresh des miroirs de program.
- `seq_play_scheduler_emit_midi_program_on_transport_start`: notification de reseed au transport start.
- `seq_play_scheduler_notify_track_pattern_change`: notification de reseed apres changement de pattern.

Contrat:
- `seq_runtime_exec` ne change pas la proprieté de la queue scheduler;
- `seq_play_scheduler` ne change pas la propriete de timeline/runtime;
- les helpers restent des seams explicites, pas des secondes autorites.

## 2.n Readbacks et diagnostics explicites

Projection runtime-exec:
- `seq_runtime_exec_state_const`: miroir readonly de l'etat d'execution partage.
- `seq_runtime_exec_get_audio_timeline_sample`: projection de timeline audio bloc.

Diagnostics scheduler:
- `seq_play_scheduler_diag_reset`: remise a zero du miroir de diagnostic queue.
- `seq_play_scheduler_diag_snapshot`: projection readonly des stats de queue/collecte.

Contrat:
- les readbacks exposent l'etat existant, sans mutation d'autorite;
- les diagnostics sont des miroirs de service, pas des commandes de progression;
- toute mutation reste dans les surfaces commande/seam deja contractees.

## 2.o Consommation audio bloc

`audio.c` est le consumer final de la projection bloc runtime:
- appelle `seq_runtime_audio_collect_block_events` une fois par demi-buffer audio;
- applique ensuite les evenements via `seq_runtime_audio_apply_event`;
- ne detient aucune autorite de progression ni de scheduling.

`seq_runtime_audio_apply_event` est le forwarder explicite entre projection bloc runtime et surface d'application scheduler.
La DSP audio du demi-buffer reste separee de la collecte et de l'application des evenements sequencer.

Contrat:
- la collecte runtime precede l'application audio;
- l'application des evenements reste separee du calcul de bloc;
- la timeline et la queue d'evenements restent proprietes internes des couches en amont.

## 2. Autorite(s) de verite

Autorite transport:
- `seq_runtime_start`, `seq_runtime_stop`, `seq_runtime_toggle_play_stop`.
- Decisions d'etat via `seq_transport_fsm_request_start/stop/continue`, `seq_transport_fsm_on_step_pulse`, `seq_transport_fsm_abort_pending`.

Autorite clock/tempo:
- Source clock active: `seq_runtime_set_clock_source` (etat de controle interne du runtime, propage via `seq_clock_bridge_set_source`).
- Tempo interne: `seq_runtime_set_tempo_bpm_milli` -> `seq_clock_bridge_set_internal_tempo`.
- Tempo externe: `seq_clock_bridge_on_external_clock_pulse` (appele depuis `seq_runtime_midi_clock_from_source`).
- Cadence interne effective (steps): `seq_runtime_exec_collect_block_events` -> `seq_runtime_exec_drive_internal_steps_for_block` (domaine audio sample, IRQ DMA).
- Tick interne auxiliaire: `seq_runtime_time_adapter_process_internal_from_irq` (IRQ TIM12) conserve un compteur de temps, sans autorite d'avance step en clock interne.

Autorite position musicale (step/boundary):
- Avance step: `seq_boundary_engine_advance_one_step`.
- Detection/changement boundary: `seq_boundary_engine_process`.
- Orchestration boundary: `seq_boundary_engine_process`.

Autorite scheduling d'evenements:
- Generation note events: `seq_play_scheduler_schedule_step`.
- Queue sample-domain: `g_seq_play_events[]` dans `seq_play_scheduler.c`.
- Collecte audio bloc: `seq_play_scheduler_audio_collect_block_events`, exposee via `seq_runtime_audio_collect_block_events`.

Autorite collecte evenements audio par bloc:
- `seq_runtime_audio_collect_block_events` (met a jour timeline audio et rapatrie les evenements dus dans le bloc).
- Les markers boundary edge-based emis par `seq_runtime_exec` utilisent le meme champ `sample_offset_in_block` que les events scheduler; ils servent a segmenter Z1 sans etre appliques par le scheduler.
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
- En source externe, les pulses MIDI clock (0xF8) arrivent via `midi_internal_receive_with_source` et sont convertis en pending step-pulses; leur consommation effective pour l'avance step se fait en debut de bloc audio dans `seq_runtime_exec_collect_block_events`.
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
- `g_seq_runtime` (`seq_runtime_state_t`, stockage porte par `seq_runtime_exec.c`, defini dans `Inc/Seq/seq_runtime.h`)
  - Champs structurants: `running`, `play_step[]`, `prev_step[]`, `prev_step_valid[]`, `track_div_phase[]`, `tick_accum`, `ticks_per_step`, `ext_clock_tick_accum`, `step_sample_q16`, `samples_per_step_q16`, `audio_block_start_sample`, `audio_timeline_sample`, `active_locks[][]`, `active_lock_count[]`.
  - Ecriture: `seq_runtime_init/start/stop/process_core/process_step_pulse`, `seq_boundary_engine_process/advance_one_step/restore_all_active_locks`, setters track.*.

Etat de controle runtime:
- `g_seq_runtime_control` (interne `seq_runtime.c`)
  - Champs structurants: `clock_src`, `track_div[]`, `track_quant[]`, `track_swing[]`.
  - Role: politique d'execution portee hors de `seq_runtime_state_t`; acces via `seq_runtime_control.h`.

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
- `g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP]` (`seq_play_scheduler_evt_t`: `due_sample_time`, `track`, `note`, `velocity`, `type`, `audio_dispatched`, `generation`, `event_token`).
- `g_seq_play_event_count`, `g_seq_play_generation`.
- `g_seq_play_active_event_token[track][note]`: autorite locale d'occurrence active pour l'application note-on/note-off du scheduler.
- Ecriture: `seq_play_scheduler_push`, `seq_play_scheduler_clear`, compaction dans `seq_play_scheduler_audio_collect_block_events`.
- Lecture: collecte bloc et apply event.

Etat modele sequenceur (`seq_model.c`):
- `g_seq_project` (`seq_project_data_t`): tracks/steps/trigs/plock pool/free list.
- Ecriture: APIs `seq_model_set_*`, `seq_model_step_plock_*`, `seq_model_load_project`, `seq_model_init_defaults`.
- Lecture: runtime boundary/scheduler/edit/UI/storage.

## 6. Flux runtime

Flux nominal prouve:
1. Source tempo/clock
- Interne: au debut de chaque bloc audio, `seq_runtime_exec_collect_block_events` appelle `seq_runtime_exec_drive_internal_steps_for_block`; les pulses de step sont derives de `audio_timeline_sample`/`samples_per_step_q16`.
- Externe MIDI/USB: `midi_internal_receive_with_source` route 0xF8/0xFA/0xFB/0xFC vers `seq_runtime_midi_*_from_source`.

2. Start/stop/continue transport
- `seq_runtime_start` prepare l'execution bloc via `seq_runtime_exec_prepare_start_lifecycle`, puis requete FSM.
- `seq_runtime_stop` applique `seq_runtime_exec_stop_lifecycle_apply` (flush live-rec, clear scheduler, restore locks, panic sortie conditionnelle).
- `seq_runtime_midi_continue_from_source` reprend RUNNING sans reset complet du modele et rebase timeline musicale si reprise depuis STOP.

3. Progression temporelle
- Interne: `seq_runtime_exec_drive_internal_steps_for_block` produit les pulses strictement dans la timeline audio absolue.
- Externe: `seq_runtime_midi_clock_from_source` met en file des pending step-pulses; `seq_runtime_exec_drive_external_steps_for_block` les consomme dans le domaine audio bloc.
- Dette explicite: ces pulses externes pending ne portent pas encore leur timestamp sample d'arrivee; les markers boundary externes restent donc cales sur `block_start_sample`.
- L'avance step (interne/externe) converge sur `seq_runtime_exec_process_step_pulse_at_sample_q16`.

4. Detection boundary / advance pattern
- `seq_runtime_exec_process_step_pulse_at_sample_q16`:
  - si RUNNING et autorise: `seq_boundary_engine_advance_one_step` (respect `track_div`/`track_div_phase`).
  - incremente `step_sample_q16`.
  - appelle `seq_boundary_engine_process`.
- `seq_boundary_engine_process` detecte boundaries track par track et gere apply/restore locks.

5. Generation/collecte des evenements
- Pour chaque `seq_boundary_hit_t`, `seq_play_scheduler_schedule_step` lit trig/plocks/param defaults et pousse NOTE_ON/NOTE_OFF horodates en sample-domain.
- Quand aucun plock `PLAY` n'est present, la valeur de base vient maintenant de l'autorite seq canonique (`seq_param_iface_get_base_value`) et non d'un fallback default descriptor.
- Le dispatch note moteur reste resolu par track runtime effectif:
  - `Sampler` -> `brick6_sampler_runtime`
  - `Opal` -> runtime Opal interne (`brick6_opal_runtime`)
  - `Braids` -> `brick6_braids_runtime`
  - `Drum` -> `drum_synth`
- Ce dispatch ne rederive pas de logique produit locale et reste borne a la projection Z2.

6. Scheduling bloc audio
- Au debut de chaque bloc audio, `seq_runtime_audio_collect_block_events`:
  - capture `block_start_sample = g_seq_runtime.audio_timeline_sample`.
  - incremente `audio_timeline_sample += block_frames`.
  - emet clocks MIDI audio-alignes (`seq_runtime_exec_emit_midi_clock_for_block`).
  - collecte depuis `seq_play_scheduler_audio_collect_block_events` les events dus dans `[block_start, block_end)` avec `sample_offset_in_block`.
  - ajoute les markers `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE` produits uniquement sur edge reel `step==0`, a l'offset sample du pulse.

7. Consommation aval audio/runtime/param
- `audio.c` applique les events aux offsets via `seq_runtime_audio_apply_event`.
- `audio.c` consomme les markers boundary au meme offset avant DSP de segment; ces markers ne passent pas par `seq_runtime_audio_apply_event`.
- `seq_play_scheduler_audio_apply_event` envoie MIDI note et note engine + gate mixer.
- Chaque couple NOTE_ON/NOTE_OFF planifie porte un `event_token`; un NOTE_OFF n'est applique que si son token correspond encore a l'occurrence active `track/note`.
- Un retrig de meme pitch ferme explicitement l'occurrence precedente puis arme le nouveau token, donc la fin planifiee de l'ancien trig ne peut pas couper le nouveau.
- Les locks de pas affectent domaine param via `seq_boundary_engine` + `seq_param_iface_apply_lock/restore_base`.
- Le chemin live-rec/edit ne passe plus par `seq_runtime.c` pour muter le modele: cette autorite vit dans `seq_live_rec_session`.

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
  - `seq_runtime_get_playhead_step`, `seq_runtime_get_track_div/quant/swing`, `seq_runtime_get_tempo_bpm_milli`, `seq_model_get_*` lisent sans muter.
- Conditions de boundary explicites:
  - boundary hit si `prev_step_valid==0` ou `prev_step != current_step`.
  - `seq_boundary_engine_process` fait apply/restore locks avant emission hit.
  - les markers Q Rec/Q Play sont edge-based: seulement quand un `boundary_hit` expose `step==0`, jamais parce qu'un getter playhead reste a 0.
- Integrite de parcours p-lock:
  - les parcours de listes p-lock cote modele (`find/mask/get_at`) sont bornes par la capacite pool track pour eviter toute boucle non bornee en presence de structure corrompue.
  - le modele Seq stocke `set_id + param_slot + value16`; `param_slot` est un slot local et jamais un `param_id` tronque.
  - les chemins qui encodent un parametre vers un p-lock utilisent `seq_param_iface_param_to_slot(track,set,param,&slot)`; les chemins qui appliquent/relisent un lock utilisent `seq_param_iface_slot_to_param(track,set,slot,&param)`.
  - `seq_param_iface_map_param` reste hors contrat pour UI/live-rec/scheduler et pour les chemins TONE/runtime-specific.
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
- La partie live-rec/edit a ete extraite vers `seq_live_rec_session`; `seq_runtime.c` garde la facade de transport mais ne porte plus la mutation directe du modele live-rec.
- La source clock active est maintenant portee par un etat de controle interne distinct; `seq_runtime_state_t` ne sert plus de support a cette autorite.
- Les verbes de politique/runtime control sont exposes via `seq_runtime_control.h`, pour garder `seq_runtime.h` centre sur l'execution et la collecte.
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
- Routage KBD interne:
  - une note KBD est traitee comme une entree interne sur le canal MIDI de la track focus/play-owner,
  - le dispatch note-on/note-off cible toutes les tracks moteur ou `Input/Hybrid` qui partagent ce canal et acceptent `INT`/`ALL`,
  - le dedoublonnage par owner de voice group conserve un seul trigger pour la source et evite de rejouer les slaves separement.
- Routage MIDI externe:
  - le dispatch conserve le filtrage par canal partage et source `EXT`/`ALL`;
  - le comportement externe reste aligne sur le contrat KBD interne, hors difference de source.
- `Sampler` passe par le meme helper central de gate VCA (`track_runtime_supports_vca_gate`) pour ouvrir/fermer le mixer gate sur note-on/off.
- Gate partage (cote mixer/VCA):
  - premiere note active ouvre le gate,
  - le gate reste ouvert tant qu'au moins une note est active,
  - la derniere note relachee ferme le gate.
- Contraintes explicites:
  - pas de vraie polyphonie audio ajoutee (toujours un flux input unique gate),
  - `panic` / `all notes off` referme proprement le gate (`mixer_track_vca_all_notes_off`).


## 11. Contrat queries strictes - seq_param_iface
- `seq_param_iface_is_param_supported` reste une query pure et ne refresh plus le runtime.
- `seq_param_iface_get_base_value` / `seq_param_iface_get_play_base_value` ne seedent plus d'etat implicite; la base doit etre deja materialisee par les commandes d'ecriture ou par l'initialisation explicite.
- Les call sites qui avaient besoin d'un runtime frais declenchent maintenant `track_runtime_refresh_track` explicitement avant lecture.

## 14. Contrat Drum stub temporaire

- Le scheduler conserve le dispatch note track-aware vers `drum_synth_*` pour les tracks Drum afin de ne pas refondre Z4.
- `TRX BD` est un slot reserve/futur et reste no-op RT-safe.
- `BD Analog` est le moteur Drum actif et produit via `drum_synth` quand PLAY arme un `note_on`.
- Les gates mixer/VCA restent appliques selon le contrat existant.

## Addendum 2026-05-06 - contrat resize length pendant RUNNING

- Autorite longueur active: `seq_model.tracks[track].length_steps`, lue via `seq_model_get_track_playback_length()`.
- Autorite curseur/playhead: `seq_runtime_exec` / `seq_runtime_state_t.play_step[]`.
- Autorite wrap/modulo: `seq_boundary_engine_advance_one_step()`, au pulse musical suivant, avec la longueur active courante.
- Changer `PARAM_SEQ_LENGTH` pendant RUNNING ne rotate pas les steps et ne rebase pas immediatement `play_step` / `prev_step`.
- Cote UI, `PARAM_SEQ_LENGTH` est un parametre par track: le miroir actif doit relire `seq_model_get_track_length(track)` au changement de track, et les edits ecrivent `seq_model_set_track_length(track, value)` puis notifient `seq_runtime_on_track_length_changed(track)`.
- Si le curseur courant devient hors fenetre apres shrink, il reste une phase courante transitoire jusqu'au prochain pulse; le prochain advance wrappe via la nouvelle longueur sans mutation des donnees pattern.
- Hors RUNNING, un curseur devenu hors fenetre est rabattu a 0 et `prev_step_valid` est invalide pour que la reprise reschedule proprement le step courant.
- Les steps au-dela de la longueur active restent stockes dans `seq_model.steps[0..SEQ_MAX_STEPS-1]` et redeviennent audibles si la longueur est re-elargie.

## Addendum 2026-05-06 - contrat p-lock MIX page 1

- `seq_param_iface` expose un set p-lock `MIX` reserve aux quatre params track-aware `PARAM_MIX_LEVEL`, `PARAM_MIX_PAN`, `PARAM_MIX_SEND1`, `PARAM_MIX_SEND2`.
- Le slot p-lock reste local au set `MIX`; l'application/restauration passe par `param_registry_apply_track_value` sur la track cible.
- Les autres params du domaine runtime `MIX` (`MUTE`, `HYBRID_GATE`, VCA) restent hors mapping p-lock.
- Le set `MIX` est stocke dans `seq_param_iface` comme 4 slots reels (`0=LEVEL`, `1=PAN`, `2=SEND1`, `3=SEND2`), hors tables communes 256 slots.

## Addendum 2026-05-08 - record SD et pattern load

- Z4 reste l'autorite transport/boundary pour les transitions musicales associees au futur recording SD.
- `pattern load` demande pendant active recording ne doit pas appliquer le snapshot immediatement.
- Contrat cible:
  - enregistrer l'intention de load,
  - demander l'arret des records actifs a une frontiere musicale si possible,
  - attendre drain/finalize cote writer Z6,
  - ensuite seulement autoriser load/apply pattern.
- Cette politique ne donne pas a Z4 l'autorite FatFs ou fichier; Z4 fournit seulement le seam temporel musical.
- Si aucune frontiere musicale fiable n'est disponible, le systeme doit choisir explicitement entre stop immediat borne ou refus/differ de load, sans mutation partielle de pattern.

## Addendum 2026-05-08 - Sampler/Looper skeleton sans hook transport

- `Sampler/Looper` est declare cote track/runtime/UI.
- Le bouton `REC` global conserve le flux Z4 normal (`seq_runtime_set_pattern_rec_target_track` puis `seq_runtime_rec_toggle_arm`) quelle que soit la track focus.
- Le demarrage Looper est observe hors Z4 par le seam transport/control Z5: un writer Looper ne demarre que si le transport est running, le REC global est arme, et une unique track `Sampler/Looper` est eligible (`ARM=Rec`, `ROUT` non vide).
- Le focus UI n'est pas une condition de demarrage Looper.
- STOP transport ou desarmement REC demande l'arret/finalisation du writer Looper actif via Z5, sans donner a Z4 l'autorite FatFs.
- Z4 ne possede pas FatFs, ne pousse pas d'audio et ne branche aucun hook Z1.
- `LEN` fixe du `Sampler/Looper` est applique hors Z4 par le seam Z5, via un compteur local en steps derive de la timeline audio `seq_runtime_exec` et de `seq_runtime_get_samples_per_step_q16`; Z4 fournit seulement la projection temporelle, pas l'autorite writer.
- `LEN=Free` ne demande aucun auto-stop; `LEN=1/2/4/8/16` demande l'arret du writer apres 1/2/4/8/16 mesures de 16 steps depuis le sample de demarrage writer memorise par Z5.
- Limite explicite: l'auto-stop Looper n'est pas encore cale sur un marker boundary edge sample-accurate; la decision est prise en superloop/UI tick, donc le stop request peut arriver avec une latence de service hors IRQ.
- `ARM=Overd` est accepte comme contrat produit continu mais reste non eligible au demarrage writer dans cette passe tant que l'overdub audio n'est pas implemente.

## Addendum 2026-05-09 - Sampler/Looper playback transport

- Z4 ne devient pas owner du fichier Looper ni du buffer audio; `brick6_looper_runtime` reste l'autorite playback transient.
- Le transport fournit seulement la condition temporelle produit: START/PLAY autorise la lecture des prises `PLAY=Auto` deja pretes ou en cours de chargement, STOP coupe la lecture.
- Au restart transport, le Looper ne demarre pas depuis le tick UI/superloop: Z4 conserve le marker `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE` de start transport jusqu'a la collecte audio suivante, et Z1 appelle le runtime Looper a l'offset sample du marker avant le rendu du segment suivant.
- Les notes PLAY du sequencer ne declenchent pas le Looper dans cette passe; le Looper suit le transport global, pas les trigs de pas.
- `ARM=Overd` reste no-op borne; aucune logique d'overdub audio n'est ajoutee au scheduler.

## Addendum 2026-05-12 - start transport atomique

- `seq_runtime_start` ne doit pas exposer un transport `RUNNING` a l'IRQ audio avant que `seq_runtime_exec_begin_running_at_sample_q16` ait seed le step 0, les markers boundary et les events PLAY initiaux.
- Pour un PLAY depuis STOP sans count-in, la transition FSM `RUNNING` et le seed execution restent dans la meme section critique: le premier collect audio voit soit STOPPED, soit un etat RUNNING complet.
- Le premier marker `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE` et les trigs du step 0 partagent le meme `step_sample_q16`; la segmentation Z1 applique ensuite le marker puis les notes au meme offset sample.

## Addendum 2026-05-13 - projection musicale pour metadata Looper

- Z4 reste seulement fournisseur de projection temporelle: marker `SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE`, `seq_runtime_get_samples_per_step_q16()` et BPM courant.
- La quantification musicale de `LEN=Free` Looper appartient au runtime Looper cote Z1/Z5: STOP arme un `REC_STOP`, puis le marker boundary audio fournit l'echantillon exact de fin.
- Z4 ne persiste aucune prise Looper et ne branche aucun stretch; il expose uniquement la cadence necessaire au calcul `recorded_steps_q16`.

## Addendum 2026-05-14 - STOP transport et Clip Launch

- STOP transport reste l'autorite de coupure globale cote sequenceur via `seq_output_guard_panic()`.
- Pour les tracks `Sampler/Clip`, le panic appelle `brick6_sampler_runtime_stop_transport_clips()`: seuls les clips sont stoppes, leur reader/playhead est remis au debut, et le PLAY suivant repart du debut du fichier/clip.
- OneShot/Slicer gardent leur contrat note/scheduler existant; Looper reste hors de ce chemin.

## Addendum 2026-05-15 - PLAY vers Sampler/Multi

- Le scheduler PLAY conserve son modele existant `V1..V4`: chaque sous-page PLAY correspond a un slot note/vel/len/mictim distinct, stocke comme p-lock PLAY sur le step.
- Pour une track resolue `Sampler/Multi`, `seq_play_scheduler_emit_engine_note()` route NOTE ON vers `brick6_sampler_runtime_trigger_multi_track_note_velocity(track,note,velocity)`.
- Le NOTE OFF planifie par la duree PLAY route vers `brick6_sampler_runtime_note_off_multi_track_note(track,note)` afin de reutiliser le lifecycle release/VCA Multi.
- Les autres types Sampler gardent le chemin Classic existant (`brick6_sampler_runtime_trigger_note_velocity`, puis note-off Classic/Clip selon contrat).
- Aucun FatFs, malloc, cache/streaming ou import Multi n'est ajoute au scheduler; le trigger Multi reste le meme seam RAM/page-cache que le clavier.
