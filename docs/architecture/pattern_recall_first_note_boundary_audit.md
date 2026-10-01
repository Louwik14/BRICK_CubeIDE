# Pattern Recall : premiere NOTE_ON a la boundary

## Verdict

Cause racine **B -- le premier horizon sautait le step courant**.
`PreparedSeq` contenait bien le step 0, son trig et ses PLAY. Lors d'un Recall
RUNNING, `seq_engine_control_commit_prepared(FLUSH)` publiait une nouvelle
generation sans changer `transport_epoch`. Dans
`seq_engine_core_process_block()`, le changement de generation recalait les
curseurs, mais `schedule_boundary()` au debut du premier horizon etait garde
uniquement par `seed_frontier`, lui-meme vrai seulement pour un core non
initialise ou un changement d'epoch. Le step 0 courant etait donc adopte sans
etre visite; la boucle partait de `step_sample_q16 + samples_per_step_q16`.

La correction fait du commit PreparedSeq FLUSH une nouvelle frontiere
d'execution et incremente `transport_epoch`. Le transport reste RUNNING et sa
date/phase ne sont pas decalees. Au premier horizon, `seed_frontier` vaut 1,
les anciens ledgers emettent leurs NOTE_OFF a l'offset 0, le core est reseme,
puis `schedule_boundary(core, pattern, start, UINT16_MAX, start, out)` visite
le step courant et produit la NOTE_ON de B a l'offset 0.

## Date et conventions

`boundary_sample` est capture une seule fois par
`seq_engine_pattern_cycle_boundary()`. Il vaut :

```text
(step_sample_q16 + pulses * samples_per_step_q16 + 0x8000) >> 16
```

Le Pattern entrant est actif a `sample == boundary_sample`. Les comparaisons
pertinentes sont :

- attente CONTROL : `boundary_sample <= first_unpublished_sample`;
- horizon CONTROL : rejet si `first_sample < first_unpublished_sample`;
- commandes d'un horizon : `sample >= first` et `sample < first + frames`;
- pas SEQ ordinaires : `next < end_q16`, puis publication si
  `next >= begin_q16`;
- evenement terminal : offset requis `< frames`;
- acquisition AUDIO : `block.start_sample == block_start_sample`; un vieux
  bloc est retire avec `block.start_sample < block_start_sample`;
- consommation AUDIO : due si `event_sample <= sample`, attente seulement si
  `event_sample > sample`;
- rejet xrun FIFO : strictement `event_sample < discard_transient_before`.

Aucun filtre stale n'emploie `<= boundary_sample` pour la NOTE terminale. La
comparaison fautive etait logique : `seed_frontier` ignorait une generation
PreparedSeq FLUSH parce que `core.transport_epoch == pattern.transport_epoch`.

## Chemin de la premiere note

| Etape | Fonction / fichier | Identite temporelle et rejet |
|---|---|---|
| DTO vers PreparedSeq | `persistent_pattern_prepare_seq()` dans `persistent_pattern_control.c` | copie les 64 steps, PLAY, trig, locks et Note FX; echec de preparation si un step ne peut etre compile |
| slot inactif | `seq_engine_control_prepare_begin/track/step/finish()` dans `seq_pattern_owner.c` | `g_prepared_slot`, masque complet tracks/steps; generation reservee issue de `g_edit_generation` |
| capture boundary | `seq_engine_pattern_cycle_boundary()` dans `seq_engine_port_h743.c` | sample absolu arrondi Q16; cycle de la lane globale deterministe |
| commit | `seq_engine_control_commit_prepared(FLUSH)` | nouvelle generation et, apres correction, nouvel epoch; slot prepare devient publie |
| premier horizon | `seq_service()` puis `seq_engine_core_process_block()` | bloc `[start,start+64)`; generation du Pattern publie |
| premier step | branche `seed_frontier`, puis `schedule_boundary()` | step derive de `traversal_phase`; step courant visite a `start`, y compris step 0 |
| NOTE brute / Note FX | `schedule_step()`, `source_add()`, `collect()` | note invalide, velocite nulle, mute/capacite nulle ou timing final hors horizon sont les rejets explicites |
| terminal | `terminal_push()` | NOTE_ON classee apres TRANSITION_PARAM, NOTE_OFF et PARAM; rejet seulement hors offset/capacite |
| READY / AUDIO | `seq_service()`, `seq_engine_audio_boundary()`, `seq_engine_audio_pop_due()` | slot READY de meme `block_start`; generation active; due accepte avec `<=` |
| moteur | `audio_command_executor_apply_seq_event()` | occurrence et owner qualifient les OFF; un ON remplace atomiquement l'ancien owner du meme slot logique |

Le slot PreparedSeq contient la note si `present_mask` contient NOTE, si le trig
du step est actif et si note/velocity sont valides. Le chemin de preparation
copie ces champs avant le swap; aucune transformation Generator/Voicer/Scaler,
Trig ou Note FX ne supprimait specifiquement le step 0.

## Avant / apres

Avant, le premier bloc de generation B adoptait les curseurs B, mais sans appel
a `schedule_boundary()` a son debut : `event_count` ne contenait pas la NOTE_ON
du step 0 et le premier evenement de B provenait du step suivant. Apres, le
changement d'epoch force le reseed : les NOTE_OFF du ledger A sont publiees a
l'offset 0, puis les PARAM et NOTE_ON B au meme offset selon l'ordre terminal.

AUDIO traite d'abord toutes les commandes CONTROL dues, dont
`AUDIO_STATE_COMMIT`, puis les evenements SEQ dus au meme sample. Le nouveau
PROGRAM est donc installe avant NOTE_ON B. Une NOTE_OFF A ne peut pas fermer la
nouvelle note : elle est appliquee avant l'ON et reste qualifiee par
`occurrence_id` et `owner_tag`; tout OFF tardif de A devient idempotent apres le
handoff.

## Bugs freres

Transport START/CONTINUE et le chemin REPLACE changent deja l'epoch; les wraps
de loop/cycle passent par `advance()` puis `schedule_boundary()` avec
`next >= begin_q16`. Groove, swing/microtiming et Note FX utilisent le calendrier
commun et ne possedent pas ce garde generation/epoch. Aucun bug frere demontre
n'a ete corrige.

La trace RAM supplementaire n'est pas necessaire : le garde fautif et l'absence
de `schedule_boundary()` sont deterministes dans le code, et le diagnostic
Pattern Recall existant prouve deja les generations PreparedSeq/PreparedAudio.

Validation sans materiel : build Release `BRICK6_CUBE.elf` reussi; firewall M7
`domain_dependency_check` passe sur 311 translation units.
