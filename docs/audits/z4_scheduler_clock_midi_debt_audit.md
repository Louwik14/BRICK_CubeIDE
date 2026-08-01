# Audit local Z4 — scheduler, clock externe et sorties MIDI

Branche observee : `main_doublemcu_monocore`  
Perimetre : Z4 ; lectures directes limitees a `Src/Audio/audio.c`, Stack, NoteFx, `Src/MIDI/midi.c` et `Src/Seq/seq_output_guard.c`.  
Methode : analyse statique, aucun patch fonctionnel, aucune mesure cible H743 et aucun test lourd.

## 1. Verdict Z4

Les trois problemes de surete evenementielle existent, mais `Z4-001` doit etre nuance : le compteur est borne a `65535`, donc il n'est pas mathematiquement non borne ; il n'existe en revanche aucun plafond compatible hard-RT par bloc ni politique de surcharge. `Z4-002` et `Z4-003` sont confirmes. Un Note On planifie peut etre accepte sans que son Note Off soit planifie, et une fermeture MIDI peut etre comptee comme terminee alors que le transport USB l'a refusee.

| Ticket | Statut | Conclusion |
|---|---|---|
| `Z4-001` | `PARTIAL` | Le drain illimite a l'echelle du bloc est confirme ; la borne brute est `65535` pulses par appel, pas l'infini. Le depassement de deadline exige une mesure H743. |
| `Z4-002` | `CONFIRMED` | Une seule place scheduler restante accepte le Note On puis rejette silencieusement le Note Off. |
| `Z4-003` | `CONFIRMED` | Le guard decremente apres un appel MIDI `void`, sans savoir si USB a accepte le paquet. |
| `Z4-004` (nouveau) | `CONFIRMED` | Les evenements scheduler appliques a un offset du bloc donnent a NoteFx le sample de fin de bloc ; un ARP peut donc etre decale jusqu'a 64 frames. |
| `Z4-005` (nouveau) | `CONFIRMED` | Le changement de source clock appelle `seq_play_scheduler_clear()` sans fermeture des notes deja actives hors ARP. |

## 2. Producteurs, files, consommateurs et contextes

### Clock externe / `Z4-001`

```text
USB RX IRQ -> midi_usb_rx_queue[128] -> midi_poll() superloop
          -> midi_internal_receive_with_source(0xF8)
          -> seq_runtime_midi_clock_from_source()
          -> seq_clock_bridge_on_external_clock_pulse() (6 F8 = 1 step)
          -> g_seq_runtime_exec_external_step_pulses_pending (uint16_t, PRIMASK)
DMA audio IRQ -> process_half() -> seq_runtime_audio_collect_block_events()
              -> seq_runtime_exec_drive_external_steps_for_block()
              -> seq_runtime_exec_process_step_pulse_at_sample_q16()
              -> boundary engine -> scheduler -> events audio / NoteFx / sorties
```

Preuves : `Src/MIDI/midi.c::{USBD_MIDI_OnPacketsReceived,midi_usb_rx_submit_from_isr,midi_poll,midi_process_usb_rx,midi_internal_receive_with_source}` ; `Src/Seq/seq_runtime.c::seq_runtime_midi_clock_from_source` ; `Src/Seq/seq_clock_bridge.c::seq_clock_bridge_on_external_clock_pulse` ; `Src/Seq/seq_runtime_exec.c::{seq_runtime_exec_increment_external_step_pulses_pending,seq_runtime_exec_drive_external_steps_for_block}` ; `Src/Audio/audio.c::process_half`.

Le producteur normal USB est le superloop, non l'IRQ USB : l'IRQ ne fait qu'enfiler la RX. La file RX USB a 128 paquets utilisables (`midi_usb_rx_count`, pas de slot vide) et le superloop en traite au plus 16 par `midi_poll()`. Le compteur de steps ne contient ni timestamp ni compteur d'overflow ; il sature silencieusement a `65535`.

### Scheduler / `Z4-002`

```text
boundary engine / seq_play_scheduler_schedule_step()
  -> seq_play_scheduler_push_note_pair()
  -> g_seq_play_events[512] (compteur, section PRIMASK)
DMA audio IRQ -> seq_play_scheduler_audio_collect_block_events()
  -> seq_play_scheduler_audio_apply_event()
  -> note_fx_pipeline_submit() -> terminal -> MIDI + moteur + output guard
```

Preuves : `Src/Seq/seq_play_scheduler.c::{SEQ_PLAY_SCHEDULER_EVENT_CAP,seq_play_scheduler_push,seq_play_scheduler_push_note_pair,seq_play_scheduler_audio_collect_block_events,seq_play_scheduler_audio_apply_event}`.

La capacite utile est exactement 512 evenements : tableau et compteur, sans slot reserve. Les producteurs sont les hits de boundary/lookahead, dont les retrigs (`seq_play_scheduler_push_note_retrigs`) et changements de programme ; le consommateur est l'IRQ audio. Les sections PRIMASK rendent chaque push coherent, mais pas la paire semantique.

### Sortie MIDI / `Z4-003`

```text
scheduler ou terminal NoteFx (DMA audio IRQ, et aussi cleanup main)
  -> midi_note_on/off(MIDI_DEST_BOTH) [void]
  -> backend_din_send() [stub] + backend_usb_device_send()
  -> USB immediate si non-IRQ et idle, sinon midi_usb_tx_queue[128]
  -> midi_poll()/PendSV/USBD completion -> USBD_MIDI_SendPackets()
```

Preuves : `Src/Seq/seq_play_scheduler.c::{seq_play_scheduler_emit_midi_note_raw,seq_play_scheduler_dispatch_terminal_note_to_channel}` ; `Src/Seq/seq_output_guard.c::{seq_output_guard_note_on_seen,seq_output_guard_note_off_seen,seq_output_guard_panic}` ; `Src/MIDI/midi.c::{MIDI_USB_TX_QUEUE_LEN,usb_tx_queue_push,usb_device_enqueue_packet,backend_usb_device_send,midi_note_on,midi_note_off,midi_usb_try_flush_internal}`.

La file TX USB a exactement 128 paquets utilisables. Elle est pleine lorsque `midi_usb_tx_count >= 128`; les channel-voice Note On/Off passent par `usb_device_enqueue_packet()`, qui incremente seulement `midi_tx_stats.tx_mb_drops` si `usb_tx_queue_push()` retourne faux. Seuls les messages realtime clock/transport ont une insertion frontale qui evince le plus ancien paquet ; les Note Off n'ont ni priorite ni reservation. Le DIN UART est un stub : `MIDI_DEST_BOTH` ne fournit pas de seconde sortie effective.

## 3. Cycle complet d'une note et point d'acquittement

1. Un hit de step alloue un token, puis `seq_play_scheduler_push_note_pair()` pousse separement On et Off.
2. La collecte audio selectionne les events dus, ecartant les generations et tracks suspendues devenues stale. Les events en retard sont places a l'offset 0.
3. `audio.c` les applique a leur offset. Le scheduler installe le token actif sur On ; Off ne passe que si le token correspond, puis efface ce token.
4. `note_fx_pipeline_submit()` synchronise NoteFx et transmet la source. Sans ARP, le terminal est immediat ; avec ARP, `note_fx_engine_source()` retient la note, puis `note_fx_engine_process()` emet les On/Off possedes.
5. `note_fx_pipeline_terminal()` appelle `seq_play_scheduler_dispatch_terminal_note_to_channel()`. Cette fonction appelle d'abord `midi_note_on/off()`, puis `seq_output_guard_note_on_seen/off_seen()`, puis le moteur local.

Le guard considere donc une note fermee exactement apres le retour synchrone de `midi_note_off()`, avant toute confirmation d'enqueue USB, de transfert USB ou de reception par le peripherique. Il ne represente pas l'etat du transport ; il represente une intention locale deja emise.

## 4. Bornes de files et travail maximal par demi-buffer

| Ressource | Borne source exacte | Travail declenche |
|---|---:|---|
| Demi-buffer audio | 64 frames | `process_half()` peut etre decoupe en 1 a 64 blocs, les fallbacks imposant au moins 1 frame. |
| Pending steps externes | 0..65535 | un appel de `drive_external_steps_for_block()` traite tout le snapshot, sans quota. |
| RX USB | 128 ; drain main de 16/poll | un F8 sur 6 peut ajouter un step pending. |
| Scheduler | 512 | collecte limitee par l'appelant a 128 events par bloc audio ; un event est recherche par scan de toute la liste. |
| Boundary events | 32 | la collecte partage la limite d'export de 128 events par bloc. |
| TX USB | 128 | burst de flush : 16 paquets ; pas de garantie de progres si USB non idle/deconnecte. |
| NoteFx | 8 tracks x 4 slots x 16 owned | budget normal recree : 8 emissions/track/appel `note_fx_engine_process()`, plus releases hors budget. |
| Stack (interaction Z1-001) | ring 256, capacite utile 255 | drain integral jusqu'a 255 commandes par appel `brick6_audio_runtime_process()`, potentiellement a chaque sous-segment. |

Pour `Z4-001`, le maximum exact par appel est 65535 traitements de step. Avec jusqu'a 64 blocs dans une demi-IRQ, le maximum syntaxique est `64 x 65535 = 4 194 240` si un producteur de priorite superieure peut recharger le compteur entre blocs ; le code seul ne permet pas d'etablir cette interleaving sur H743. Avec le producteur USB normal (superloop), une IRQ audio continue ne peut pas etre rechargee par le superloop : une mesure de priorites/latences est necessaire pour transformer cette borne syntaxique en borne executable.

Le travail Step n'est pas constant : chaque pulse avance jusqu'a 14 tracks, construit des hits, peut ajouter marker/metronome et planifier du PLAY/lookahead. Les 65535 pulses sont tous traites dans la meme collecte audio et tous recoivent le meme `now_sample` de timeline. La file scheduler limitera les nouvelles paires a 512, mais ne limite ni les 65535 passages boundary ni les scans/rejets associes.

Interaction `Z1-003` : la segmentation NoteFx peut porter la demi-IRQ a 64 blocs ; son budget est recree a chaque appel, soit jusqu'a `8 tracks x 8 x 64 = 4096` emissions normales par demi-buffer, sans compter `release_slot()` (jusqu'a 16 Off par slot). `Z4-001` peut donc declencher de nombreux schedules au debut de chacun de ces blocs, mais ne rend pas ce budget global.

## 5. Saturation a 0 ou 1 place restante

### Scheduler

- 0 place : On et Off sont tous deux refuses ; seul `queue_overflow_drop_count` augmente deux fois. Aucune note n'est acceptee.
- 1 place : On est insere et Off est refuse. Le token existe, l'On peut etre applique puis devenir note moteur/MIDI active ; aucune fermeture planifiee ne lui est attachee. C'est le defaut confirme `Z4-002`.
- 2 places ou plus : la paire est inseree, mais aucune garantie aval n'existe encore (NoteFx, Stack et USB peuvent echouer).

`seq_play_scheduler_push()` est `void`; les deux echecs sont invisibles a `push_note_pair()`. Le token est alloue avant les pushes, y compris lorsque l'On est refuse. Le compteur overflow est la seule trace.

### TX USB

- 0 place : la Note On/Off channel-voice est refusee par `usb_tx_queue_push()` et perdue ; `tx_mb_drops` augmente. `midi_note_*()` et les appelants ne recoivent rien.
- 1 place : un seul message peut etre retenu. Deux sorties consecutives peuvent accepter On puis perdre Off (ou perdre une fermeture de panic apres une autre sortie). Une completion USB entre les deux peut changer ce resultat, mais aucune reservation de fermeture n'existe.
- Deconnexion/non-idle : l'immediat est impossible et le flush retourne sans consommer ; la queue accumule jusqu'a 128, puis les messages channel-voice sont perdus. Aucun clear explicite TX n'a ete trouve sur deconnexion ; une reconnexion peut transmettre des messages anciens encore en queue.

### Stack / `Z1-001`

La ring Stack laisse un slot vide : 0 place utile signifie 255 commandes en attente, 1 place signifie 254. `brick6_stack_runtime_submit_*()` retourne 0 quand pleine. Le clavier et le panic `seq_output_guard_panic()` ignorent ce retour, mais le scheduler Z4 appelle les API directes `brick6_stack_runtime_note_on/off()` dans l'IRQ, sans passer par cette ring. Ainsi `Z4-002` ne se transforme pas directement en paire Stack non atomique, mais les memes fermetures de panic/cleanup peuvent etre perdues dans Stack selon `Z1-001`.

## 6. Refus aval, clear, panic, generation et deconnexion

- **NoteFx / Z1-002 :** `note_fx_engine_source()` peut refuser un Note On lorsque l'ARP est sature et retourne 0 ; `seq_play_scheduler_audio_apply_event()` ignore ce retour. Les mutations main et IRQ de NoteFx restent multi-ecrivains, notamment cleanup/suspend/configuration face a process. Un cleanup peut emettre des Off hors budget et via le meme chemin MIDI non acquitte.
- **Moteurs :** `seq_play_scheduler_emit_engine_note()` abandonne silencieusement si `track_runtime_resolve_track()` echoue. Les APIs des moteurs usuels sont `void`; Sampler Multi retourne un statut mais il est ignore. Stack direct, Prism, Wave, Deluge et Drum n'offrent pas ici d'accuse de reception de l'evenement. Aucun resultat moteur ne peut donc conditionner le guard.
- **Clear/generation :** `seq_play_scheduler_clear_tracks()` incremente la generation de track, nettoie NoteFx, force les Off des tokens actifs puis compacte la file. Ces Off restent vulnerables a `Z4-003`. La collecte ecarte les generations stale, ce qui est volontaire et protege contre un replay tardif. `seq_play_scheduler_clear()` ne force pas les Off des tokens actifs : il nettoie NoteFx puis efface file/tokens. `seq_runtime_set_clock_source()` l'appelle pendant un changement de source sans panic ; un On non-ARP deja applique peut alors rester actif. C'est `Z4-005`.
- **Panic/stop :** stop appelle clear puis `seq_output_guard_panic()`. Le panic nettoie NoteFx, envoie autant de `midi_note_off()` que le count guard, puis met immediatement les counts a zero ; il peut aussi perdre chaque Off USB et ignore le retour Stack All Notes Off. Le panic local des moteurs est en revanche execute directement pour les moteurs applicables.
- **Source clock/transport :** start, continue, stop et changement de source remettent `pending_steps` a zero. Les pulses pending sont donc abandonnes volontairement lors de ces transitions ; cela est correct comme politique de transport, mais sans compteur de drops et sans documentation de cette politique.
- **Deconnexion MIDI :** pas de rejection remontee au scheduler/guard. L'absence de `MIDI_IDLE` retient la file, puis les drops deviennent silencieux hors statistiques. La reconnexion conserve les paquets non purges ; l'ordre musical apres reconnexion n'est pas garanti.

## 7. Position temporelle reelle de la clock externe

`seq_runtime_exec_begin_audio_block()` avance d'abord la timeline, puis `seq_runtime_exec_drive_external_steps_for_block()` consomme le snapshot. Chaque pulse rattrape est appele avec `pulse_sample_q16 = block_start_sample << 16`; `block_frames` est explicitement ignore. Tous les pulses d'un backlog, y compris le 65535e, sont donc places au sample 0 du bloc logique, pas a leur instant USB, ni repartis dans le bloc. Les markers boundary et les events dus sont collectes a offset 0 (les retards sont eux aussi clamps a 0).

`Z4-004` est une consequence directe distincte : apres la collecte, `seq_runtime_exec_get_audio_timeline_sample()` vaut deja la fin du bloc. `seq_play_scheduler_audio_apply_event()` passe cette valeur a `note_fx_pipeline_submit()` meme si `audio.c` applique l'event a l'offset 0. Pour un ARP, `next_sample` est alors la fin du bloc et la premiere emission est traitee au bloc suivant : retard de 1 a 64 frames selon l'offset/event. Sans ARP le terminal est immediat, donc le defaut vise le chemin ARP.

## 8. Qualification des constats

| Nature | Constats |
|---|---|
| Defauts confirmes par le code | `Z4-002`, `Z4-003`, absence de quota RT `Z4-001`, `Z4-004`, `Z4-005`, retours NoteFx/Sampler/Stack/MIDI ignores aux frontieres indiquees. |
| Risques seulement theoriques | Le nombre de recharges du compteur externe pendant une meme demi-IRQ et l'enchainement exact 1-place USB dependent des interruptions/completions. |
| Mesure H743 requise | Cycles des 65535 pulses, cout boundary/scheduler/NoteFx/Stack cumule, priorites USB/DMA, nombre de blocs NoteFx reel, deadline audio et phase percue de sync externe. |
| Comportement volontaire mais mal documente | Drop des pulses pending aux transitions de transport/source et rejet stale par generation sont des politiques coherentes. `z4_seq_clock_scheduler.md` annonce encore une queue scheduler capee a 256 alors que `SEQ_PLAY_SCHEDULER_EVENT_CAP` vaut 512 ; c'est une divergence documentaire, pas une preuve de defect runtime. |

## 9. Causes racines et micro-corrections locales recommandees

1. **`Z4-002` d'abord :** faire retourner la reservation scheduler et reserver les deux cases sous une seule section critique ; si moins de deux places, ne pas emettre le Note On. Conserver la file et ses capacites fixes.
2. **`Z4-003` :** donner a l'enqueue MIDI channel-voice un resultat explicite jusqu'au scheduler/guard ; ne decremener le guard que selon un contrat d'acceptation local explicite. Ajouter une petite politique locale terminale (Off/panic) dans la file USB, sans bus generique.
3. **`Z4-001` :** borner les steps externes par bloc, conserver/compter l'excedent et choisir explicitement drop, coalescence ou rephase. Une instrumentation de high-water et de pulses deferes doit preceder le quota final.
4. **`Z4-004` :** transmettre a `note_fx_pipeline_submit()` le `event_sample_time` deja connu par `audio_apply_seq_event_at_sample()`, au lieu du getter timeline de fin de bloc.
5. **`Z4-005` :** sur changement live de source, reutiliser la fermeture locale existante avant `seq_play_scheduler_clear()`, ou rendre `clear()` contractuellement terminal et corriger ses appelants. Ne pas modifier le FSM ni introduire une infrastructure de messages.
6. **Interactions Z1 :** traiter ensuite les retours Stack (`Z1-001`), puis le seul owner IRQ et le budget demi-buffer NoteFx (`Z1-002`/`Z1-003`). Ces corrections ne remplacent pas les garanties Z4 : elles ferment les refus aval restants.

## 10. Validations necessaires

| Sujet | Validation ciblee |
|---|---|
| Paire scheduler | Saturation a 0, 1, 2 places ; retrigs ; chaque On accepte a exactement un chemin Off ou refus complet. |
| USB terminal | TX a 127/128, USB non-idle, deconnexion/reconnexion, Note On/Off/panic/clock melanges ; verifier guard, queue et messages effectivement remis. |
| Clock externe | Rafales F8, pause superloop, start/continue/stop/source switch, timestamp de chaque pulse ; cycles et jitter sur H743. |
| NoteFx | ARP avec event aux offsets 0 et 63, deadlines chaque frame, saturation 16 notes, cleanup/model/p-lock concurrents. |
| Clear/source | Clock source changee avec notes locales, MIDI et ARP actives ; aucun sustain ni replay tardif. |
| Z1 cumule | Stack 254/255, NoteFx 64 sous-segments, events scheduler et external catch-up dans la meme demi-IRQ ; mesurer cycles/underrun. |

## 11. Nouveaux tickets Z4 a preuve forte

### Z4-004 — horodatage NoteFx pris a la fin du bloc audio

- **Statut :** `CONFIRMED`.
- **Preuve :** `seq_runtime_exec_begin_audio_block()` incremente la timeline avant application ; `seq_play_scheduler_audio_apply_event()` appelle `note_fx_pipeline_submit(..., seq_runtime_exec_get_audio_timeline_sample())`; `audio.c` applique pourtant l'event a son offset dans le bloc. Avec ARP, `note_fx_engine_source()` pose `next_sample` a cette fin de bloc.
- **Impact prouve :** le premier evenement ARP peut etre reporte au bloc suivant, jusqu'a 64 frames ; les events externes rattrapes a offset 0 subissent le meme decalage.
- **Micro-patch :** propager l'horodatage d'application explicite au submit NoteFx.

### Z4-005 — changement de source clock efface les fermetures planifiees sans fermer les notes actives

- **Statut :** `CONFIRMED`.
- **Preuve :** `seq_runtime_set_clock_source()` appelle `seq_play_scheduler_clear()` ; ce clear nettoie NoteFx et efface `g_seq_play_active_event_token`, mais n'emet pas les Off des tokens actifs. Il n'appelle pas `seq_output_guard_panic()`. `seq_play_scheduler_clear_tracks()`, lui, possede une boucle explicite de forced Off, ce qui confirme que `clear()` ne la reutilise pas.
- **Impact prouve :** une note non-ARP deja appliquee peut perdre son Off planifie lors d'un changement de source en cours de lecture et rester active jusqu'a une fermeture ulterieure/panic.
- **Micro-patch :** fermeture locale avant clear sur ce chemin, ou contrat terminal explicite de `clear()` avec ajustement minimal des appelants.

## 12. Limites de preuve

Cet audit ne prouve ni underrun, ni note bloquee reproduite, ni retard audible sur cible. Il prouve les chemins de refus, de perte d'acquittement et les positions temporelles source. Les couts et priorites effectifs doivent etre mesures sur H743 avant de fixer les quotas.
