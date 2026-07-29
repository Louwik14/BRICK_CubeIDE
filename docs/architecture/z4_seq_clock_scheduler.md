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
- `seq_runtime_set_rec_start_mode` / `seq_runtime_set_rec_len_mode`
- `seq_runtime_set_pattern_rec_target_track`
- `seq_runtime_live_rec_param_write`

Queries:
- `seq_runtime_get_state`
- `seq_runtime_is_running` / `seq_runtime_is_start_pending`
- `seq_runtime_get_rec_start_mode` / `seq_runtime_get_rec_len_mode`
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
- markers `SEQ_RUNTIME_AUDIO_EVENT_METRO_CLICK` comme projection metronome sample-accurate depuis l'autorite transport/clock Z4; ils sont emis sur les beats et accents au debut de mesure 16 steps quand le transport reel tourne, sans timer parallele ni recalcul tempo local en Z1.
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
- `keyboard_arp`: rend pour `seq_play_scheduler` les notes/velocites ARP derivees d'un step actif; le scheduler reste proprietaire de l'horodatage sample-domain et de la queue audio.

Contrat:
- ces consumers ne font pas de refresh implicite;
- tout refresh requis reste au bord de l'orchestrateur appelant;
- les getters runtime restent des projections pures ou des miroirs explicites, jamais des commandes cachees.

## 2.f Consommateurs non-UI de commande

Commandes runtime explicites, avec readback miroir quand le caller doit resynchroniser son store/UI:
- `param_registry_apply_wrappers.c`: `apply_cfg_start`, `apply_cfg_tempo`, `apply_cfg_sync`, `apply_cfg_rec_len`, `apply_seq_div`, `apply_seq_quant`, `apply_seq_swing`.
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
- `g_seq_rec_armed`, `g_seq_rec_start_mode`, `g_seq_rec_len_mode`.
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
- Lecture boundary p-lock: `seq_boundary_engine` collecte les locks d'un step via `seq_model_step_plock_collect`, en un seul parcours lineaire de la liste chainee du step.
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
  - `Prism` -> `brick6_braids_runtime`
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
  - les parcours de listes p-lock cote modele (`find/mask/collect/get_at`) sont bornes par la capacite pool track pour eviter toute boucle non bornee en presence de structure corrompue.
  - le boundary runtime ne doit pas collecter tous les locks d'un step par appels repetes a `seq_model_step_plock_get_at`; il utilise `seq_model_step_plock_collect` pour conserver un cout O(n) sur le nombre de locks du step.
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

## Addendum 2026-05-14 - STOP transport et Stream Launch

- STOP transport reste l'autorite de coupure globale cote sequenceur via `seq_output_guard_panic()`.
- Pour les tracks `Sampler/Stream`, le panic appelle `brick6_sampler_runtime_stop_transport_clips()`: seuls les streams sont stoppes, leur reader/playhead est remis au debut, et le PLAY suivant repart du debut du fichier/stream.
- RAM garde son contrat note/scheduler existant, y compris en slicing grille via `Slice Count`; Looper reste hors de ce chemin.

## Addendum 2026-05-15 - PLAY vers Sampler/Multi

- Le scheduler PLAY conserve son modele existant `V1..V4`: chaque sous-page PLAY correspond a un slot note/vel/len/mictim distinct, stocke comme p-lock PLAY sur le step.
- Pour une track resolue `Sampler/Multi`, `seq_play_scheduler_emit_engine_note()` route NOTE ON vers `brick6_sampler_runtime_trigger_multi_track_note_velocity(track,note,velocity)`.
- Le NOTE OFF planifie par la duree PLAY route vers `brick6_sampler_runtime_note_off_multi_track_note(track,note)` afin de reutiliser le lifecycle release/VCA Multi.
- Les autres types Sampler gardent le chemin Classic existant (`brick6_sampler_runtime_trigger_note_velocity`, puis note-off Classic/Stream selon contrat).
- Aucun FatFs, malloc, cache/streaming ou import Multi n'est ajoute au scheduler; le trigger Multi reste le meme seam RAM/page-cache que le clavier.

## Addendum 2026-05-26 - prototype grille PLAY partagee retire de Z4

- `BRICK6_AUDIO_EVENT_GRID_FRAMES` ne fait plus autorite musicale pour Z4.
- `seq_runtime` conserve `samples_per_step_q16` dans le domaine BPM/Q16 exact, sans snap sur la grille audio.
- `seq_runtime_exec` conserve les start/pending-start/pulses/anchors dans le domaine sample/Q16 exact.
- `seq_play_scheduler` planifie note-on/note-off/program en `due_sample_time` reel; la collecte audio convertit seulement en offset relatif au bloc courant.
- Z1 peut toujours utiliser une grille audio pour ses traitements propres, mais les boundaries et events PLAY exposent des offsets intra-buffer reels.
- Le microtiming negatif n'est pas traite par snap au boundary: il passe par le contrat lookahead dedie ci-dessous.

## Addendum 2026-05-27 - microtiming negatif par lookahead PLAY

- `seq_runtime_exec` planifie, a chaque boundary PLAY reelle, le step courant pour les voix a microtiming nul/positif et le prochain step de la track pour les voix a microtiming negatif.
- Le lookahead est borne a un step musical de track, calcule avec `samples_per_step_q16 * div`; il ne scanne pas la pattern complete et ne cree pas d'allocation.
- `seq_play_scheduler_schedule_step()` ignore les voix PLAY negatives; `seq_play_scheduler_schedule_step_lookahead_negative()` ignore les voix nulles/positives. Cette separation evite le double trigger.
- Le wrap utilise le meme referentiel que les steps: le prochain step est calcule modulo `seq_model_get_track_playback_length()`. Un step 0 avec microtiming negatif est donc planifie depuis la boundary du dernier step du tour precedent.
- Les note-on, note-off et program PLAY d'une voix negative sont horodates depuis le vrai `note_on_sample_time` anticipe. La duree note part de cet instant reel; le token note-on/note-off conserve le guard anti double-off existant.
- Les p-locks PLAY restent lus sur le step cible anticipe et sont donc groupes avec la note anticipee. Les p-locks non-PLAY restent appliques uniquement par `seq_boundary_engine` a la boundary officielle du step.
- L'ordre a sample identique reste: boundary collectee avant scheduler, puis dans le scheduler note-off avant program avant note-on. Le tri runtime conserve l'ordre stable pour les events de meme offset.
- Les retrigs dedies ne sont pas un sous-systeme separe dans le code courant; le comportement couvert ici concerne les voix PLAY existantes, leurs note lengths, program changes et forced note-off/token guards.

## Addendum 2026-05-28 - REC START

- Le parametre global REC `START` remplace l'ancien mode de lancement REC.
- Valeurs: `DEFAULT` lance comme l'ancien mode sans roll, `TRIG` arme un etat explicite `waiting trigger start`, `ROLL 1/4`, `ROLL 1/2` et `ROLL 1` conservent les delais roll existants de 4/8/16 steps.
- `START=TRIG` est porte par `seq_live_rec_session`: armer REC ou demander PLAY en REC arme depuis STOP place l'attente trigger; le premier note-on interne ou MIDI externe consomme l'attente, demarre le transport via `seq_runtime_start`, puis continue dans le chemin live-rec normal.
- STOP, desarmement REC et changement de START hors `TRIG` clear l'attente. Changer START vers `TRIG` pendant REC arme et STOP recree l'attente de facon deterministe.
- Les chemins clavier et MIDI ne decident pas du start: ils notifient seulement `seq_runtime_live_rec_note_on`, le domaine REC consomme eventuellement le trigger.

## Addendum 2026-07-23 - default SEQ Length

- `SEQ_DEFAULT_LENGTH_STEPS` vaut 16 et devient la longueur initiale de chaque track dans `seq_model_init_defaults()`.
- Les projets/patterns deja sauvegardes gardent leur `length_steps` persiste tant que la valeur est valide; aucune migration ou bump de format n'est introduit.

## Addendum 2026-07-23 - ARP Hold declenche par steps

- Quand `ARP Hold` est actif sur une track, un step sequenceur actif de cette meme track fournit une fenetre source au runtime ARP de la track.
- Cette fenetre reste track-locale et ne modifie pas les autres etats ARP; `ARP Hold=Off` conserve le pilotage clavier uniquement.
- Le scheduler PLAY collecte les notes du step actif, conserve la fenetre `SEQ_STEP`, demande a `keyboard_arp` de rendre seulement la tranche temporelle due au boundary courant, puis queue les note-on/note-off ARP dans la meme queue sample-domain que PLAY.
- Le chemin `SEQ_STEP` ne depend plus du tick UI/systeme ni de `HAL_GetTick` pour son horodatage: note-on, strum et note-off ARP sont collectes/appliques par `seq_runtime_audio_collect_block_events` avec offsets intra-buffer.
- Les divisions ARP sont converties depuis la cadence sequencer audio (`samples_per_step_q16`, base 16th = 6 PPQN24), sans accumulation en millisecondes et sans recalage au wrap pattern.
- La fenetre ARP `SEQ_STEP` est bornee par la duree PLAY effective du step: une note `LEN=16` rend progressivement tous les pas ARP dus dans ces 16 steps, sans note source brute concurrente.
- Les parametres ARP sont relus par tranche de scheduler; une revision de config ARP force la prochaine tranche a repartir du boundary courant avec les nouveaux reglages, sans reappui sur le step source.
- Le rendu `SEQ_STEP` de `keyboard_arp` est transitoire: seule la phase ARP avance, puis la source/pattern `KBD` eventuelle est restauree; `keyboard_arp_tick` ne devient pas proprietaire de cette cadence.
- Au STOP transport, `seq_runtime` demande uniquement le clear de la source ARP `SEQ_STEP`: la phase `SEQ_STEP` repartira du debut au prochain PLAY si aucune source `KBD` n'est active, tandis que la source `KBD` latchee reste disponible pour le jeu manuel hors sequenceur.

## Addendum 2026-07-25 - LENGTH rapide low-cost et DIV

- Le geste low-cost `STEP A maintenu, vide ou occupe + STEP B vide apres A` ecrit un p-lock PLAY `LEN` sur le step A, sans toucher au transport ni au moteur audio.
- Si A est vide ou seulement porteur de p-locks, le geste materialise A en trig actif avant le feedback; B reste une borne vide non materialisee.
- La duree utilise la meme base que les boundaries runtime: un pas temporel de track vaut `samples_per_step_q16 * DIV`. La valeur `LEN` stockee restant exprimee en steps de base scheduler, la conversion est `LEN = (B - A + 1) * DIV`.
- La valeur encodee passe par `seq_param_iface_encode_param_value(PARAM_SEQ_PLAY_Vx_LEN, value)`, donc la quantification et les bornes du catalogue PLAY existant (`1..64 stp`) restent l'autorite.
- Hors groupe, si plusieurs voix PLAY sont deja materialisees sur A, leurs `LEN` correspondants sont mis a jour; sinon le fallback borne est `V1_LEN`.
- Sur une master de voice group, la meme duree temporelle est ecrite comme `V1_LEN` distinct sur chaque membre collecte par Z2 dans l'ordre master puis slaves, plafonne a 8. Les pages PLAY groupe restent independantes et `LINK` n'intervient pas.
## Addendum 2026-07-25 - PLAY voice group master/slaves

- Le scheduler ignore les tracks `SLAVE` comme sources autonomes.
- Quand une source `MASTER` possede un groupe, le step de la master pilote les membres collectes par Z2 dans l'ordre master puis slaves, plafonnes a 8.
- Chaque membre lit ses propres bases et p-locks PLAY voix 1 (`NOTE`, `VEL`, `LEN`, `MicTim`) sur sa track membre, puis emet vers sa propre track runtime; hors groupe, le chemin historique 4 voix de la track reste inchange.
- Le gate de scheduling d'une master groupe teste les p-locks PLAY des tracks membres du groupe, pas seulement ceux de la master, afin que les p-locks poses depuis les pages PLAY groupe restent audibles et independants.
- La persistence reste le format pattern existant: comme les p-locks groupe sont stockes sur la track membre cible avec les IDs PLAY historiques, save/load conserve l'independance sans ajouter d'ID artificiel par page.

## Addendum 2026-07-25 - clipboard step PLAY voice group

- Le clipboard de steps conserve le format historique mono-track pour une track seule: trig + tous les p-locks du step source sur la track source, puis paste avec clear complet du step cible.
- Quand la source est une master de voice group, le clipboard ajoute un payload PLAY borne a 8 membres, ordonne master puis slaves, indexe par position logique de membre et non par liste speciale de tracks.
- Pour chaque membre, le payload PLAY copie tous les p-locks `SEQ_PLOCK_SET_PLAY` du step membre et materialise au minimum `V1 NOTE`, `V1 VEL`, `V1 LEN`, `V1 MicTim` depuis le lock existant ou la base PLAY du membre.
- Au paste vers une master de groupe, le membre source N est applique au membre destination N. Si les largeurs source/destination divergent, les membres communs sont appliques et le resultat est marque partiel; les membres destination excedentaires ne sont pas touches.
- Les slaves restent sans ensemble PLAY direct: le paste ecrit seulement leurs p-locks PLAY internes. Les locks PLAY d'un membre slave sont clears/reposes sur sa track, sans toucher les locks non-PLAY ni les pages PLAY des autres membres.
- `LINK` n'intervient pas dans ce chemin: aucune propagation PLAY n'est declenchee par copy/paste.
## Addendum 2026-07-26 - ROLL par step

- `seq_step_t.roll` porte le roll/retrig du step, separe de `LEN` et des p-locks.
- Valeurs stockees: `OFF`, `1/20`, `1/24`, `1/32`, `1/40`, `1/48`, `1/64`, `1/80`.
- Le scheduler lit le roll du step source et ajoute de vrais `NOTE_ON`/`NOTE_OFF` sample-domain dans la queue existante; le premier trig reste le trig principal et aucun sous-trig n'est emis a l'offset zero.
- Les sous-trigs reutilisent les memes resolutions NOTE/VEL/LEN/MicTim et p-locks PLAY du step original; aucun effet audio de repetition, aucun changement Prism/Stack/Matrix n'est introduit.
- Les pending events roll sont nettoyes par les clears scheduler existants au STOP, changement de pattern ou lifecycle transport.

## Addendum 2026-07-27 - Synth/Wave scheduler

- `Synth/Wave` est exposable comme identite PLAY via Z2, mais cette passe ne branche aucun dispatch note vers un runtime wavetable.
- Le scheduler ne redirige jamais Wave vers Prism/Braids; le branchement note-on/off Wave appartient a l'etape runtime audio Wave.

## Addendum 2026-07-28 - decision d'autorite SEQ LINK cote Z4

- Z4 n'est pas proprietaire de `SEQ LINK`.
- Z4 consultera uniquement la projection Z2/track_state master-effective pour choisir la source de lecture p-lock playback.
- Le futur routage `SEQ LINK` ne devra pas modifier `seq_model`, ne devra pas ecrire de p-lock, et ne devra pas synchroniser les locks des slaves.
- Les consumers Z4 concernes par les prochaines etapes seront classes avant migration: boundary non-PLAY, scheduler PLAY, live-rec, edit/feedback et clipboard ne doivent pas etre supposes equivalents.
- La decision de restauration runtime des locks actifs reste hors de cette etape: elle dependra de l'audit du cycle apply/restore existant.

## Addendum 2026-07-28 - audit consommateurs p-lock pour SEQ LINK

Audit code reel effectue avant conception du resolver:
- API modele centrale:
  - `seq_model_step_plock_find`, `seq_model_step_plock_collect`, `seq_model_step_plock_count`, `seq_model_step_plock_get_at`, `seq_model_step_has_play_plock`, `seq_model_step_has_non_play_plock` lisent le stockage local par track/step;
  - `seq_model_step_plock_upsert`, `seq_model_step_plock_delete`, `seq_model_step_plock_clear` ecrivent ce stockage local;
  - `seq_model_get_step_content`, `seq_model_get_step_visual`, `seq_model_get_step_state`, `seq_model_step_is_empty`, `seq_model_step_is_quick_note_eligible` lisent indirectement les sets de p-lock pour UI/LED/edit, pas pour playback audio.
- Interface param p-lock:
  - `seq_param_iface_param_to_slot` / `slot_to_param` portent le mapping `set/slot <-> param` et la validation supportee par track;
  - `seq_param_iface_get_base_value`, `get_play_base_value`, `apply_lock`, `restore_base`, `set_play_base_value` gerent base/runtime temporaire, mais ne lisent pas les p-locks de step dans `seq_model`;
  - `param_registry` lit/ecrit les bases PLAY via `seq_param_iface`, pas les locks de step; `param_macro` utilise seulement l'autorite de p-lockabilite.
- Lecture playback actuelle:
  - `seq_boundary_engine` collecte les locks du step courant via `seq_model_step_plock_collect`, filtre par support runtime, capture/restaure les bases via `seq_param_iface_get_base_value` / `restore_base`, puis applique via `seq_param_iface_apply_lock`;
  - `seq_play_scheduler` teste `seq_model_step_has_play_plock`, lit les locks PLAY par `seq_model_step_plock_find` dans `seq_play_scheduler_get_locked_or_default`, puis retombe sur `seq_param_iface_get_play_base_value`;
  - le scheduler groupe lit actuellement les p-locks PLAY des membres master+slaves, tandis que les slaves ne schedulent pas comme sources autonomes.
- Lecture/ecriture live-rec:
  - `seq_live_rec_session` et `seq_live_rec_capture` lisent des locks PLAY existants pour selection de voix, sauvegarde/restauration temporaire et detection de note;
  - ces modules ecrivent/suppriment aussi des p-locks PLAY par `seq_model_step_plock_upsert/delete`;
  - `seq_runtime_live_rec_param_write` route l'ecriture p-lock live vers la track cible/playhead courante, sans notion de source playback distante.
- Edition UI:
  - `ui_param_try_apply_seq_plock` lit l'eventuel lock existant du step maintenu puis upsert/delete le lock sur la track d'edition effective;
  - `ui_param_try_apply_live_rec_plock` lit l'eventuel lock au playhead avant d'appeler `seq_runtime_live_rec_param_write`;
  - `ui_param_try_get_seq_plock_feedback_with_frame` lit un lock local pour feedback visuel d'edition;
  - `ui_renderer_template` consomme ce feedback pour widgets et previews; ce chemin est affichage/edition, pas playback.
- Edition steps et gestes:
  - `seq_edit` expose des wrappers directs vers `seq_model_step_plock_*`, utilise les reads pour quick note, hold, clear, undo et gestes low-cost;
  - le geste LENGTH low-cost lit les locks PLAY existants et ecrit `LEN` localement sur les membres du groupe courant;
  - `seq_led` lit les visuels de step via `seq_model_get_step_visual`, donc indirectement les sets de p-lock locaux.
- Clipboard:
  - `seq_clipboard` lit les locks d'un step par `count/get_at` ou `collect`, copie tous les locks mono-track, et ajoute un payload PLAY par membre de voice group;
  - le paste clear/repose les locks par `clear/delete/upsert`, avec validation `seq_param_iface_slot_is_supported`;
  - ce chemin reste stockage/edition explicite, pas lecture playback.
- Persistence / undo:
  - `pattern_live_ram` capture tous les p-locks par `count/get_at` et restaure par `clear/upsert`;
  - `undo_v2` enregistre des deltas p-lock et les applique via `seq_edit_step_plock_apply_state`;
  - ces chemins doivent conserver la topologie locale des donnees, sans suivre une source `SEQ LINK`.

Chemins caches ou facilement oubliables:
- les visuels de step (`seq_model_get_step_content/visual/state` puis `seq_led`) lisent les sets de locks sans appeler explicitement une API nommee playback;
- `param_registry_get_track_value` pour le domaine PLAY lit la base PLAY `seq_param_iface`, pas le lock du step;
- `seq_boundary_engine` collecte aujourd'hui tous les sets supportes, meme si le scheduler lit PLAY separement;
- les chemins live-rec/capture lisent les p-locks PLAY pour choisir une voix et ne doivent pas etre confondus avec la lecture scheduler.

Conclusion d'audit pour les etapes suivantes:
- seul `seq_boundary_engine` suit le routage de source `SEQ LINK` pour les p-locks non-PLAY; `seq_play_scheduler` conserve la lecture PLAY locale par membre cible;
- les chemins UI, live-rec, clipboard, undo, persistence, LED/visual et base PLAY doivent etre classes explicitement avant toute migration et ne doivent pas etre bascules automatiquement vers une source master.

## Addendum 2026-07-28 - classification des chemins p-lock pour SEQ LINK

Classification cible avant conception des resolvers:
- Doit suivre `SEQ LINK`:
  - `seq_boundary_engine` pour la lecture playback des p-locks non-PLAY appliques au boundary;
  - uniquement dans ce chemin, la source de lecture non-PLAY peut devenir la master effective, tandis que la cible d'application reste la track membre.
- Doit ignorer `SEQ LINK`:
  - `seq_play_scheduler` pour la lecture playback des p-locks PLAY qui produisent notes, velocity, length et microtiming;
  - `seq_model_step_plock_upsert/delete/clear` et tout le stockage `seq_model`;
  - `pattern_live_ram` capture/apply Pattern/Project;
  - `undo_v2` deltas p-lock et snapshots;
  - `seq_clipboard` copy/paste/clear de steps;
  - `seq_edit` clear/toggle/quick-note/LENGTH low-cost et wrappers d'ecriture p-lock;
  - `ui_param_try_apply_seq_plock` et `ui_param_try_apply_live_rec_plock`;
  - `seq_runtime_live_rec_param_write`, `seq_live_rec_session`, `seq_live_rec_capture`;
  - `ui_param_try_get_seq_plock_feedback_with_frame` et `ui_renderer_template`;
  - `seq_led` et les visuels derives de `seq_model_get_step_content/visual/state`.
- Doit rester local a la track:
  - bases PLAY dans `seq_param_iface_get_play_base_value` / `set_play_base_value`;
  - bases non-PLAY et runtime-temp dans `seq_param_iface_get_base_value`, `apply_lock`, `restore_base`;
  - validation `seq_param_iface_param_to_slot` / `slot_to_param` / `slot_is_supported`, qui doit valider la track cible d'application ou d'edition, pas une semantique inter-moteur;
  - affichage UI de valeur editable et feedback de p-lock en edition;
  - donnees PLAY stockees sur les slaves, meme lorsque leur lecture playback est ignoree.
- Necessite une decision de design ulterieure:
  - si les p-locks non-PLAY `PLAY` presents dans la collecte boundary doivent etre exclus explicitement avant routage, ou si la separation actuelle boundary/scheduler suffit;
  - comment le scheduler traite `Program Change` en groupe master: emission sur master seulement ou sur membre cible selon capacite;
  - comment `ARP Hold` step consomme les notes source en groupe master: fenetre master uniquement ou fenetre par membre cible;
  - si la desactivation/activation de `SEQ LINK` pendant RUNNING requiert un flush/restore explicite des locks actifs, decision reportee a l'audit runtime dedie.

Regles de migration:
- aucun chemin d'ecriture ne doit etre route vers la source master par `SEQ LINK`;
- aucun chemin de persistence, undo ou clipboard ne doit materialiser une copie issue de `SEQ LINK`;
- aucun chemin UI ne doit afficher la valeur playback liee comme base editable;
- tout consommateur nouveau de p-lock playback devra declarer explicitement s'il suit `SEQ LINK` ou s'il reste local.

## Addendum 2026-07-28 - principe de routage p-lock playback SEQ LINK

Principe commun avant design des resolvers:
- Le routage p-lock playback separe toujours quatre notions:
  - `target_track`: track qui recoit l'application runtime ou l'evenement PLAY;
  - `source_track`: track dont les p-locks sont lus;
  - `source_step`: step lu dans la source;
  - `logical_address`: adresse positionnelle du lock, composee de `set`, `page` et `slot`.
- En mode local ou `SEQ LINK=OFF`, `source_track == target_track` et le comportement reste le stockage local historique.
- En `SEQ LINK=ON`, pour les membres d'un voice group, `source_track` devient la master effective et `target_track` reste le membre courant.
- La source de lecture ne modifie jamais la cible d'application: un lock lu sur la master est valide/applique contre la track cible.
- La correspondance reste strictement positionnelle. Le routage ne compare pas les noms, ne cherche pas de param equivalent entre moteurs, ne merge pas et ne copie pas.

Adresse logique:
- `set` reste l'ensemble p-lock existant: `COLORS`, `TONE`, `PLAY`, `MOD`, `MIX`.
- `page` represente la page logique exposee par le consumer quand elle existe. Pour les surfaces actuellement stockees sans page explicite, la page peut etre derivee du slot ou fixee a `0` par le resolver specialise.
- `slot` est le rang logique dans la page ou le slot p-lock deja persiste selon le set.
- L'adresse logique ne devient pas un `param_id` global. La resolution finale `slot -> param` reste locale a la track cible via les autorites existantes, notamment les tables TONE par type runtime.

Contrat source/cible:
- Le resolver de lecture doit pouvoir retourner au minimum:
  - cible d'application;
  - source de lecture;
  - step source;
  - set;
  - slot source;
  - slot cible si une traduction positionnelle est necessaire;
  - statut `local`, `linked`, `unsupported`, `no_lock`.
- La validation de support runtime se fait cote cible. Une position presente sur la master mais absente/non supportee cote cible est ignoree proprement.
- Les bases capturees/restaurees restent celles de la cible, jamais celles de la source.
- Les valeurs lues restent les `value16` de la source. Leur decode/application utilise le param resolu cote cible afin de conserver le contrat positionnel volontaire.

Portee:
- Ce principe vaut pour la lecture playback uniquement.
- Les prochaines etapes peuvent definir deux resolvers specialises si necessaire:
  - un resolver boundary non-PLAY pour application/restauration runtime temporaire;
  - un resolver PLAY pour scheduling note/program/ARP/roll/lookahead.
- La philosophie commune est le couple `source de lecture` / `cible d'application`; elle n'impose pas une API unique si PLAY a des besoins differents.

Contraintes:
- pas de refresh implicite cache;
- pas d'allocation;
- cout borne par constantes existantes de tracks, steps et locks;
- pas de dependance UI;
- pas de mutation `seq_model`;
- pas de stockage derive de `SEQ LINK`.

## Addendum 2026-07-28 - design resolver non-PLAY SEQ LINK

Objet:
- Le resolver non-PLAY couvre uniquement la lecture playback boundary des sets `COLORS`, `TONE`, `MOD` et `MIX`.
- `PLAY` est explicitement exclu de ce resolver et reste traite par le design PLAY dedie.
- Le resolver ne possede ni transport, ni playhead, ni stockage p-lock, ni etat runtime applique. Il resout seulement une source de lecture et une cible d'application pour un boundary donne.

Entrees minimales:
- `target_track`: track dont le boundary est en cours de traitement et qui recevra l'apply/restauration runtime temporaire.
- `target_step`: step courant de cette track, utilise comme step local en mode non linke.
- `set_id`: set p-lock demande, limite aux sets non-PLAY autorises.
- `source_entry`: entree p-lock candidate lue depuis la source, ou requete de collecte selon l'API retenue a l'etape de validation.
- Contexte groupe lu via Z2: role, master effective, membres, et projection `SEQ LINK` master-effective.

Sorties minimales:
- `target_track`: inchange, pour rappeler que la cible d'application ne migre jamais.
- `source_track`: track dont le p-lock est lu.
- `source_step`: step lu cote source.
- `set_id`.
- `source_slot`: slot persiste lu dans la source.
- `target_slot`: slot logique applique cote cible; pour les sets non-PLAY courants, il est identique au slot source sauf si une future page/slot explicite impose une traduction positionnelle.
- `value16`: valeur lue sur la source.
- statut: `LOCAL`, `LINKED`, `NO_LOCK`, `UNSUPPORTED`, `INVALID`.

Resolution source:
- Si `target_track` n'appartient pas a un groupe linke operationnel, `source_track = target_track` et `source_step = target_step`.
- Si `scheduler_track` est la master d'un groupe avec `SEQ LINK=ON`, `source_track = master`, `source_step = scheduler_step`.
- Si `target_track` est une slave d'un groupe avec `SEQ LINK=ON`, elle est appliquee par la boundary de sa master effective; `source_step` est donc le `scheduler_step` master, pas le step local de la slave.
- Si la master effective est absente, orpheline, hors limites ou sans groupe exploitable, le resolver retombe en `LOCAL`.
- Le resolver ne change pas les compteurs de step, ne recalcule pas les longueurs et ne force aucun alignement de pattern; l'eventuelle divergence de longueurs master/slave sera tranchee dans l'etape de validation d'interface.

Resolution set/slot:
- Les locks de la source sont lus comme `set_id + param_slot + value16`, jamais comme nom de parametre.
- `set_id=PLAY` est ignore par ce resolver, meme si un lock PLAY est present dans la collecte brute du step source.
- La validation de support se fait toujours avec `target_track`, `set_id`, `target_slot`.
- Pour `TONE`, la conversion `slot -> param` reste celle du type runtime cible via Z2; un slot TONE master peut donc piloter une fonction differente sur la cible.
- Pour `MIX`, seuls les slots MIX p-lockables existants restent admissibles cote cible.
- Pour `MOD`, seuls les slots deja p-lockables restent admissibles; les selecteurs Matrix exclus du p-lock ne sont pas reintegres par `SEQ LINK`.
- Pour `COLORS`, la cible effective filtre/engine reste resolue par les autorites Z2/Z3 existantes, pas par la source master.

Apply/restore:
- La capture de base se fait toujours sur `target_track + target_slot`.
- L'apply temporaire se fait toujours sur `target_track`.
- La restauration se fait toujours sur `target_track`.
- `source_track` ne sert qu'a lire `value16`.
- Un lock source non supporte cote cible est ignore sans fallback vers un param du meme nom ou un autre slot.

Collecte boundary cible:
- Option de design privilegiee: le boundary engine demande au resolver de produire une liste de locks non-PLAY applicables pour chaque `target_track/target_step`.
- Cette liste reste bornee par `SEQ_STEP_MAX_LOCKS`.
- En mode `LINKED`, la collecte brute se fait sur la source master mais le filtrage/support/base/apply se fait sur la cible.
- En mode `LOCAL`, le comportement doit rester equivalent a la collecte actuelle.

Limites volontaires:
- Pas de merge master+slave: si `SEQ LINK=ON`, les locks non-PLAY de la slave sont ignores en playback.
- Pas d'heritage: une absence de lock sur la master signifie absence de lock playback, meme si la slave possede un lock local.
- Pas de copie ni de synchronisation.
- Pas de mapping intelligent entre moteurs.
- Pas de changement de l'affichage UI ni du feedback p-lock local.

## Addendum 2026-07-28 - validation interface resolver non-PLAY SEQ LINK

Interface validee pour migration future:
- Le resolver non-PLAY doit etre une query pure appelee par `seq_boundary_engine`.
- Il ne modifie ni `seq_model`, ni `seq_param_iface`, ni `track_state`, ni les locks actifs runtime.
- Il ne declenche pas de refresh runtime implicite. Le caller garde la responsabilite d'avoir une projection Z2 coherente avant lecture.
- Il ne lit aucune source UI et ne depend pas de la track active UI.

Forme d'API cible:
- Une API de collecte est preferee a une API `find` appelee en boucle.
- Entree cible: `target_track`, `target_step`, buffer de sortie, capacite de sortie.
- Sortie: liste bornee de locks applicables, chaque item portant `target_track`, `source_track`, `source_step`, `set_id`, `source_slot`, `target_slot`, `value16` et statut `LOCAL/LINKED`.
- Les statuts `NO_LOCK`, `UNSUPPORTED` et `INVALID` restent des resultats internes/diagnostics possibles mais ne doivent pas grossir la liste appliquee.
- Capacite maximale: `SEQ_STEP_MAX_LOCKS`, identique au boundary actuel.

Responsabilites acceptees:
- choisir la source de lecture selon `SEQ LINK` master-effective;
- collecter au plus une fois les locks du step source;
- exclure strictement `SEQ_PLOCK_SET_PLAY`;
- traduire/porter l'adresse positionnelle `set/slot` vers la cible;
- filtrer par support runtime cote cible;
- retourner uniquement les locks applicables.

Responsabilites refusees:
- appliquer ou restaurer une valeur;
- capturer une base;
- resoudre une valeur UI visible;
- creer un fallback vers les locks locaux de la slave quand la master n'a pas de lock;
- deduire une correspondance par nom ou par moteur;
- changer la progression de step, la longueur de pattern, le DIV ou le playhead;
- decider de la politique de flush/restore lors d'un changement de source.

Cas limites valides:
- Master sans slave effective: resultat operationnel local.
- Slave orpheline ou groupe invalide: resultat operationnel local.
- `SEQ LINK=ON` mais slot source non supporte par la cible: lock ignore.
- `SEQ LINK=ON` mais aucun lock non-PLAY sur la master: aucun lock applique, les locks locaux de la slave restent ignores en playback.
- Longueurs master/slave divergentes: le resolver lit le `source_step` decide par le caller; aucune correction automatique n'est faite dans le resolver.
- Lock source `PLAY` rencontre pendant collecte brute: ignore.

Points reportes:
- La politique exacte de `source_step` quand les longueurs master/slave divergent reste a valider avec le scheduler/boundary lors de la migration.
- La necessite d'un flush/restore explicite lors d'activation/desactivation de `SEQ LINK` pendant RUNNING reste reportee a l'audit runtime dedie.
- Les diagnostics publics du resolver ne sont pas requis pour la premiere migration; un compteur local peut etre ajoute seulement si utile et borne.

## Addendum 2026-07-28 - migration boundary non-PLAY SEQ LINK

Etat implemente:
- `seq_boundary_engine` consomme maintenant une collecte dediee aux locks non-PLAY applicables au boundary.
- La collecte separe `source_track/source_step/source_slot` et `target_track/target_slot`; l'application, la capture de base et la restauration restent cote cible.
- Le resolver non-PLAY consulte maintenant la projection Z2 `track_runtime_get_voice_group_seq_link()` et route les membres d'un groupe `SEQ LINK=ON` vers la master effective comme source de lecture.
- `SEQ_PLOCK_SET_PLAY` est explicitement exclu de cette collecte boundary; PLAY reste un consommateur distinct porte par `seq_play_scheduler`.
- La validation de support reste faite contre la track cible via `seq_param_iface_slot_is_supported`, sans mapping par nom ni correspondance intelligente entre moteurs.

Limites volontaires de cette etape:
- la lecture playback non-PLAY du boundary suit la route commune Z4; le scheduler PLAY consomme la meme resolution `scheduler_track/scheduler_step -> source_track/source_step`;
- aucun chemin d'ecriture `seq_model`, live-rec, clipboard, undo, persistence, UI ou LED n'est migre;
- aucune politique de flush/restore runtime lors d'un changement de source n'est ajoutee.

## Addendum 2026-07-28 - branchement effectif resolver non-PLAY SEQ LINK

- `seq_plock_route_resolve()` garde le mode local par defaut.
- Si la route commune retourne `linked=1`, la source non-PLAY devient la master effective du voice group et les cibles sont les membres collectes du groupe.
- `source_step` est le `scheduler_step` de la master qui produit aussi les events PLAY du groupe; aucune projection de longueur slave n'est ajoutee.
- La collecte lit les p-locks non-PLAY uniquement sur `source_track/source_step`, ignore toujours `SEQ_PLOCK_SET_PLAY`, puis valide chaque slot contre la cible par position logique `set/page/slot`.
- Les p-locks non-PLAY locaux des slaves sont donc ignores pendant la lecture quand `SEQ LINK=ON`, sans copie, merge, suppression ni fallback local.

## Addendum 2026-07-28 - design resolver PLAY SEQ LINK

Objet:
- Le resolver PLAY couvre uniquement la lecture playback du scheduler `seq_play_scheduler`.
- Il ne remplace pas le resolver boundary non-PLAY: il partage le principe `source de lecture` / `cible d'emission`, mais garde une API adaptee au scheduling note/program/ARP/roll.
- Les chemins live-rec, edit, clipboard, undo, persistence, UI feedback et LED restent hors de ce resolver.

Unites de resolution:
- `scheduler_track`: track appelee par le boundary Z4 courant; les tracks `SLAVE` restent ignorees comme sources autonomes.
- `source_track`: track dont le step actif, les p-locks PLAY et le roll sont lus.
- `source_step`: step lu dans `source_track`.
- `target_track`: track qui recevra l'evenement note/program ou la fenetre ARP.
- `target_voice`: voix PLAY logique cote cible, utilisee pour resoudre les bases et les slots `NOTE/VEL/LEN/MicTim`.
- `source_voice`: voix PLAY logique lue cote source.

Mode local:
- Hors groupe master, `source_track == target_track == scheduler_track` et les voix `V1..V4` restent lues localement.
- Pour une master de groupe sans `SEQ LINK`, le comportement existant reste volontairement conserve: chaque membre du groupe lit son propre slot `V1` local, et la master orchestre seulement l'emission vers les membres.
- Le gate d'existence PLAY reste compatible avec ce modele: une master de groupe est schedulable si au moins un membre expose un lock PLAY sur le step courant.

Mode `SEQ LINK=ON`:
- Le comportement PLAY est identique au mode local de master de groupe.
- Chaque membre cible lit ses propres p-locks PLAY et sa propre base PLAY `V1`.
- La source master ne remplace jamais les notes PLAY des membres.
- Les p-locks PLAY locaux des slaves restent lus via la page membre de la master; ils ne sont ni copies, ni merges, ni supprimes.

Correspondance positionnelle PLAY:
- La correspondance reste strictement par position logique, jamais par nom de moteur.
- En groupe, la position de membre selectionne seulement `target_track`; elle ne devient pas une voix PLAY source implicite.
- La source PLAY privilegiee reste la meme page/voix logique que celle consommee par la cible. Dans le chemin groupe actuel, cela signifie `V1` pour chaque membre cible.
- La cible d'emission reste le membre a la position logique de groupe courante.
- Hors groupe, la position logique reste la voix `V1..V4` historique de la track.
- La validation de support et les bases sont toujours lues cote cible via `seq_param_iface_param_to_slot`, `seq_param_iface_get_play_base_value` et les gardes `track_runtime_get_effective_param_status`.

Roll:
- Le roll est une donnee de step, pas un p-lock.
- En master de groupe, le roll reste celui du step scheduler master, que `SEQ LINK` soit `ON` ou `OFF`.

Program Change:
- `PARAM_MIDI_PROGRAM` est lu par la meme resolution PLAY/TONE existante que le scheduler utilise aujourd'hui, mais la cible d'emission doit rester la track cible supportant le Program Change.
- En groupe `SEQ LINK=ON`, une lecture master ne doit pas forcer un Program Change sur une slave qui ne supporte pas cette capacite.
- Le miroir `g_seq_play_midi_program_valid/last` reste indexe par cible d'emission, pas par source de lecture.

ARP Hold:
- `ARP Hold` reste une capacite de la track cible.
- Les notes source qui alimentent la fenetre ARP viennent des items PLAY resolus; en master de groupe, elles restent lues sur chaque membre cible.
- Le resolver PLAY doit donc produire des notes resolues par cible avant l'appel aux chemins `keyboard_arp_*`; il ne doit pas rendre l'ARP lui-meme.

Forme d'API cible:
- Une API de resolution d'item PLAY est preferee: elle retourne pour une position scheduler la paire `source_track/source_step/source_voice` et `target_track/target_voice`.
- Une API de gate peut etre separee pour savoir si un scheduler step contient une matiere PLAY audible sous le routage courant.
- Les helpers de lecture de valeur doivent accepter explicitement `source_track/source_step/source_voice` et `target_track/target_voice`, afin que la lecture du lock et la lecture de base ne puissent pas etre confondues.
- Les sorties doivent rester bornes par `SEQ_PLAY_SCHEDULER_VOICE_COUNT` hors groupe et par le plafond groupe existant de 8 membres.

Responsabilites acceptees:
- determiner les items cible a scheduler sans modifier la source PLAY historique;
- lire les locks PLAY sur la source par slot positionnel;
- lire les bases PLAY cote cible quand le lock source est absent;
- fournir le roll source applicable;
- laisser le scheduler calculer timing, quant, queue, tokens et dispatch audio.

Responsabilites refusees:
- ecrire ou supprimer des locks PLAY;
- modifier les donnees PLAY stockees sur les slaves;
- modifier les roles de groupe ou la longueur des tracks;
- capturer/restaurer des valeurs runtime non-PLAY;
- fusionner master et slaves;
- rendre l'ARP, pousser des events ou appliquer des notes directement.

Points transmis a validation d'interface:
- la confirmation de `source_voice` en groupe quand la largeur de groupe depasse la surface PLAY voix historique;
- le traitement d'une longueur master/slave divergente: garder le `source_step` fourni par le boundary courant ou projeter le step master;
- le maintien du gate actuel `has_group_play_plock`, qui doit tester les locks locaux des membres;
- l'emission Program Change en groupe, qui devra etre indexee par cible et filtree par capacite.

## Addendum 2026-07-28 - validation interface resolver PLAY SEQ LINK

Interface validee pour migration future:
- Le resolver PLAY doit rester une query pure appelee par `seq_play_scheduler`.
- Il ne modifie ni `seq_model`, ni `seq_param_iface`, ni `track_state`, ni les queues scheduler, ni les fenetres ARP.
- Il ne declenche pas de refresh runtime implicite; le caller garde le refresh explicite au bord du scheduler.
- Il ne lit aucune source UI et ne depend pas de la track active UI.

Forme d'API cible:
- Une API de preparation de contexte de scheduling est retenue pour isoler la logique groupe/SEQ LINK du corps de scheduling.
- Entree contexte: `scheduler_track`, `scheduler_step`, `negative_lookahead`.
- Sortie contexte:
  - `source_track` et `source_step` pour le gate de trig et le roll;
  - `linked` pour diagnostics internes;
  - liste bornee d'items PLAY, chaque item portant `target_track`, `target_voice`, `source_track`, `source_step`, `source_voice`;
  - `item_count`, plafonne par `SEQ_PLAY_SCHEDULER_VOICE_COUNT` hors groupe et par le plafond groupe existant de 8 membres.
- Une API de lecture de valeur PLAY doit accepter explicitement:
  - source: `source_track`, `source_step`, `source_voice`, `param_kind`;
  - cible: `target_track`, `target_voice`, `param_kind`;
  - sortie: `value16` resolue.
- `param_kind` doit etre une enumeration interne simple (`NOTE`, `VEL`, `LEN`, `MICTIM`) plutot qu'un `param_id` source global, pour eviter de melanger lecture source et decode cible.

Decisions validees:
- En groupe master, `source_track` reste le membre cible pour les p-locks PLAY et `target_track` reste ce meme membre.
- En groupe, l'index de membre ne doit pas etre converti en `source_voice`. Le modele actuel de pages PLAY groupe selectionne une track membre et expose son `V1`; le resolver doit donc lire `source_voice=V1` pour ces items tant que cette surface reste en place.
- Hors groupe, `source_voice == target_voice` pour les voix `V1..V4`.
- Le gate de scheduling doit tester le step actif master puis les locks PLAY locaux des membres.
- Le roll en master de groupe est lu sur le step scheduler master.
- La quantification, le DIV, les fenetres ARP et les compteurs de notes actives restent cibles/runtime locaux; ils ne sont pas portes par la source master.

Lecture valeur:
- Le lock est cherche sur `source_track/source_step` avec le slot PLAY derive de `source_voice + param_kind`.
- Si le lock source est absent, la base PLAY de repli est lue sur `target_track/target_voice + param_kind`.
- Le decode utilise le `param_id` cible, jamais un nom ou une fonction moteur source.
- Une adresse source absente ou non supportee retombe seulement sur la base PLAY de la cible.
- Une cible dont le param PLAY est `BLOCKED_TRANSITIONAL` reste ignoree avant lecture.

Program Change:
- Le Program Change ne fait pas partie des quatre items voix PLAY et doit rester un sous-chemin explicite du scheduler.
- `SEQ LINK` ne change pas la lecture PLAY des notes; le Program Change conserve son sous-chemin explicite historique.
- L'emission est filtree par `seq_play_scheduler_track_supports_program_change` sur la cible.
- Si plusieurs cibles produisent une premiere note au meme boundary, chaque cible admissible peut recevoir son propre Program Change schedule au premier note-on cible.

ARP Hold:
- La decision `ARP Hold` est lue sur la cible.
- Les notes qui alimentent la fenetre ARP viennent des items PLAY resolus, donc des membres cibles en master de groupe.
- `seq_play_scheduler_begin_arp_window` et `seq_play_scheduler_schedule_arp_window_slice` restent indexes par `target_track`.
- Le resolver ne rend pas l'ARP et ne manipule pas la phase ARP.

Cas limites valides:
- Master sans slave effective: contexte local.
- Slave appelee directement par le boundary scheduler: aucun item autonome, comme aujourd'hui.
- Groupe invalide ou master effective absente: contexte local si la track n'est pas slave, sinon aucun item autonome.
- Groupe de plus de 8 membres: plafond existant a 8 conserve.
- Master de groupe sans lock PLAY sur aucun membre: aucune matiere PLAY source.
- Lock source manquant pour `VEL`, `LEN` ou `MICTIM`: base cible correspondante utilisee comme repli.
- Longueurs master/slave divergentes: pour cette migration, `source_step` reste le step fourni par le boundary de `scheduler_track`; aucune projection de longueur master n'est introduite.

Responsabilites explicitement hors resolver:
- calcul des offsets microtiming et quant;
- allocation de tokens;
- push de note-on/note-off/retrig/program;
- dispatch audio/MIDI;
- flush/restore runtime lors d'un changement de source;
- affichage et edition des pages PLAY.

## Addendum 2026-07-28 - migration scheduler PLAY SEQ LINK

Etat implemente:
- `seq_play_scheduler` prepare maintenant un contexte PLAY explicite avant le scheduling d'un step.
- Ce contexte separe `scheduler_track`, `source_track/source_step`, `target_track`, `source_voice` et `target_voice`.
- La lecture des valeurs `NOTE`, `VEL`, `LEN` et `MicTim` passe par un helper qui lit le lock sur la source et la base PLAY de repli sur la cible.
- Le gate de presence PLAY lit les sources declarees par les items du contexte, au lieu de dupliquer la logique groupe directement dans le corps du scheduler.
- Le roll est lu via la source du contexte.
- Correction 2026-07-28: `SEQ LINK` ne modifie plus la source PLAY. En groupe master, chaque membre cible lit ses propres p-locks/base PLAY `V1`, que `SEQ LINK` soit `ON` ou `OFF`.

Etat volontairement local a cette etape:
- En master de groupe, le contexte conserve le comportement existant hors `SEQ LINK`: chaque membre cible lit sa propre voix `V1` locale.
- Les slaves appeles directement restent sans item autonome.
- Le Program Change reste un sous-chemin explicite du scheduler; il lit la source du contexte mais conserve l'emission historique sur la track scheduler tant que le routage cible par item n'est pas branche.
- L'ARP Hold conserve son indexation scheduler historique; la migration ARP par cible reste separee pour eviter une refonte implicite pendant cette etape.

Limites volontaires:
- aucun chemin d'ecriture `seq_model`, live-rec, clipboard, undo, persistence, UI ou LED n'est migre;
- aucune copie, synchronisation, fusion ou suppression de locks PLAY slave n'est introduite;
- aucune politique de flush/restore runtime lors d'un changement de source n'est ajoutee;
- le routage cible du Program Change et des fenetres ARP reste une etape separee.

## Addendum 2026-07-28 - branchement effectif resolver PLAY SEQ LINK

- Correction 2026-07-28: ce branchement ne s'applique pas au pipeline PLAY.
- `seq_plock_route_resolve()` peut signaler `linked=1` pour les consumers p-lock non-PLAY, mais `seq_play_scheduler` utilise seulement la liste de cibles de groupe.
- En master de groupe, chaque item membre conserve `target_track=member`, `target_voice=V1`, `source_track=member`, `source_step=scheduler_step`, `source_voice=V1`.
- Le gate de matiere PLAY teste les locks PLAY des membres cibles, comme avant le refactoring SEQ LINK.
- Les bases de repli restent lues cote cible pour `NOTE`, `VEL`, `LEN` et `MicTim`.
- Le roll PLAY reste celui de la boundary scheduler master, comme dans le comportement historique du groupe.
- Les longueurs divergentes ne sont pas reprojetees: `source_step` reste le step fourni au scheduler.

## Addendum 2026-07-28 - audit runtime changement de source SEQ LINK

Constat code reel:
- `seq_boundary_engine_process` applique/restaure les locks non-PLAY uniquement quand `prev_step_valid` est faux ou quand `prev_step[track] != play_step[track]`.
- `seq_runtime_exec_begin_running_at_sample_q16` et `seq_runtime_exec_stop_lifecycle_apply` restaurent explicitement tous les locks actifs via `seq_boundary_engine_restore_all_active_locks`.
- Entre deux boundaries d'une meme track, aucune reevaluation automatique des p-locks non-PLAY n'a lieu.
- `seq_runtime_active_lock_t` stocke seulement la cible appliquee (`set_id`, `param_slot`, `base_value16`) et ne stocke pas l'identite de source p-lock.
- `seq_param_iface_apply_lock` pose un etat runtime-temp verrouille; `seq_param_iface_restore_base` est le seul chemin observe qui relache explicitement ce runtime-temp.
- Le scheduler PLAY queue des events sample-domain; ces events ne sont pas recalcules si la source de lecture change apres leur planification.
- Le lookahead negatif peut deja avoir pousse des events du prochain step avant le boundary cible.

Decision:
- Le moteur ne recalcule pas naturellement tous les parametres au changement de source `SEQ LINK`.
- Un changement effectif de source pendant RUNNING doit donc declencher une reconciliation runtime explicite.
- Cette reconciliation n'est pas necessaire quand le transport est STOPPED: le prochain START restaure/reseed deja les locks actifs et reschedule depuis l'etat courant.

Contrat cible de reconciliation:
- Z2/track_state reste proprietaire du flag; Z4 recoit seulement une notification post-commit indiquant que le routage de lecture p-lock d'un groupe peut avoir change.
- La notification doit etre explicite, par exemple une future commande interne `seq_runtime_on_seq_link_changed(master_track)`.
- Cette commande ne doit pas modifier `seq_model`, ne doit pas copier de locks et ne doit pas changer les roles de groupe.
- Les tracks affectees sont la master effective et les membres collectes du groupe au moment de la notification, avec le meme plafond operationnel que le scheduler.

Reconciliation non-PLAY:
- Pour chaque track cible affectee, restaurer les locks actifs via `seq_boundary_engine_restore_all_active_locks`.
- Invalider le boundary courant de la track cible (`prev_step_valid=0`) afin que le prochain passage par `seq_boundary_engine_process` reapplique les locks du step courant depuis la nouvelle source.
- L'application immediate du step courant est acceptable uniquement si elle passe par le meme boundary engine; il ne faut pas creer un second chemin apply special pour `SEQ LINK`.

Reconciliation PLAY:
- Les events deja queues peuvent provenir de l'ancienne source; ils doivent etre consideres stale lors d'un changement RUNNING.
- La solution propre est un clear scheduler borne par cible ou par groupe, conservant les invariants de token/note-off.
- Tant qu'un clear scoped n'existe pas, un clear global du scheduler serait fonctionnel mais trop large; il doit etre evite comme premiere implementation sauf decision produit explicite.
- Les notes deja actives doivent garder une sortie coherente: le chemin de clear choisi doit soit emettre les note-off necessaires via les guards existants, soit etre couple a un panic/output-guard scoped.

Decision de timing:
- La reconciliation doit etre appelee apres commit du flag `SEQ LINK`, jamais avant.
- Si elle est appelee RUNNING, elle doit s'executer hors IRQ audio et rester bornee par constantes de groupe/track.
- Les effets audio du changement peuvent etre pris en compte au prochain boundary musical; une reapplication sample-immediate n'est pas requise pour cette feature.

Etat implemente:
- `seq_runtime_on_seq_link_changed(master_track)` est le seam Z4 explicite appele apres commit du flag par `param_registry`.
- Si le transport est STOPPED, la notification ne fait rien: le prochain START reseed deja les locks actifs et la queue scheduler.
- Si le transport est RUNNING, la notification collecte la master et ses membres bornes a 8, restaure les locks actifs non-PLAY de chaque cible et invalide leur boundary courant via `seq_boundary_engine_invalidate_track()`.
- `seq_play_scheduler_clear_tracks()` fournit un clear scheduler borne par tracks cible: il retire les events queues du groupe, coupe les notes actives connues pour ces tracks, nettoie les fenetres ARP et conserve les autres tracks.
- La relecture effective se fait au prochain boundary musical par les resolvers existants; aucune application sample-immediate ni second chemin apply special `SEQ LINK` n'est introduit.

Limites maintenues:
- la notification ne modifie ni `seq_model`, ni les p-locks stockes, ni les roles de groupe;
- la reconciliation ne materialise aucune copie issue de `SEQ LINK`;
- les races avec des events deja collectes par le bloc audio restent traitees par les guards/tokens existants, sans nouveau chemin IRQ.

## Addendum 2026-07-28 - route logique commune SEQ LINK

- `seq_plock_route` porte la resolution commune `scheduler_track/scheduler_step -> source_track/source_step + targets` pour les consumers playback Z4.
- Le boundary non-PLAY consomme la source `SEQ LINK` de cette route. Le scheduler PLAY consomme seulement ses cibles de groupe et conserve la source PLAY locale de chaque cible.
- Une slave appelee par sa propre boundary reste ignoree comme source autonome quand elle est drivee par `SEQ LINK`; ses locks locaux restent stockes mais ne sont pas lus en playback.
- Hors `SEQ LINK`, le comportement reste local: les non-PLAY suivent la boundary de chaque track et le scheduler de master de groupe conserve ses sources membres locales.

## Addendum 2026-07-28 - correction regression PLAY master group

Audit code reel:
- La track cible PLAY est resolue dans `seq_play_scheduler_resolve_play_context()`: en master de groupe, les cibles sont les membres collectes par `seq_plock_route_resolve()`.
- Chaque slot/note PLAY est resolu par `seq_play_scheduler_param_for_play_kind()` puis `seq_param_iface_param_to_slot()` dans `seq_play_scheduler_get_play_locked_or_default()`.
- Les p-locks PLAY sont lus par `seq_model_step_plock_find(source_track, source_step, SEQ_PLOCK_SET_PLAY, source_slot, ...)`.
- Les bases de repli PLAY sont lues par `seq_param_iface_get_play_base_value(target_track, target_slot, ...)`.
- Les notes finales sont appliquees par `seq_play_scheduler_push_note_retrigs()` puis `seq_play_scheduler_audio_apply_event()` vers MIDI/engines/mixer.

Cause:
- Le refactoring SEQ LINK a fait consommer `route.source_track` par le resolver PLAY.
- Quand `SEQ LINK=ON`, `seq_plock_route_resolve()` mettait `source_track=master`; `seq_play_scheduler_resolve_play_context()` recopiat cette source pour tous les items membres avec `source_voice=V1`.
- Le scheduler lisait donc le meme slot PLAY master `V1` pour toutes les tracks cibles, au lieu de lire le slot PLAY `V1` propre a chaque membre cible.
- Ce comportement etait une confusion entre master de groupe, source p-lock non-PLAY, track cible PLAY et slot PLAY; il n'etait pas une propriete de `SEQ LINK`.

Correction:
- Le resolver PLAY ignore maintenant la source `SEQ LINK` et garde `source_track=target_track` pour chaque membre.
- `SEQ LINK` peut encore modifier la provenance des p-locks non-PLAY via `seq_plock_route`, mais ne modifie plus l'adressage historique PLAY note -> track cible -> slot cible.

## Addendum 2026-07-28 - Track snapshot et sequence

- Le snapshot Track capture/restaure la sequence de la track avec trigs, rolls et p-locks par step, ainsi que `SEQ Length`, page UI, div, quant et swing.
- Avant restore/clear Track, `track_snapshot` appelle `seq_runtime_clear_tracks()` sur les tracks concernees pour retirer les evenements et notes residuels sans clear global.
- Le clipboard de steps reste separe; le clipboard Track utilise maintenant le snapshot canonique quand l'intention utilisateur cible toute la track.

## Addendum 2026-07-28 - verrou edition sequence slave SEQ LINK

- `seq_edit_track_sequence_is_locked()` est le garde d'edition utilisateur pour la sequence Pattern d'une track.
- Le verrou est actif uniquement si la track cible est `SLAVE` et que la projection `SEQ LINK` effective est `ON`.
- Les chemins d'edition steps, rolls, p-locks, paste/clear de steps, live-rec PLAY, undo snapshot et parametres runtime de sequence (`LENGTH`, `DIV`, `QUANT`, `SWING`) consultent ce garde avant toute ecriture Pattern.
- Le verrou ne modifie pas `seq_model`, ne supprime aucune donnee locale et ne participe pas a la persistence; Pattern/Project restore restent des chemins de chargement, pas des edits utilisateur.
- Le scheduler et la boundary ignoraient deja l'emission autonome des slaves sous `SEQ LINK=ON` via `seq_plock_route`: aucune modification scheduler n'est requise pour cette regle UI.
