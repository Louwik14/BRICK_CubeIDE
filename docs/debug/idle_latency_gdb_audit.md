# Audit latence idle par à-coups / capture GDB

État audité : `origin/main` à `c7e1d96b9f653a361d0391e2225e1132e593bad7`
(synchronisé le 2026-09-10). Les modifications locales préexistantes ne font
pas partie du verdict. Le build de validation les contient toutefois, car le
worktree était déjà modifié.

## Verdict

La cause racine n'est pas confirmable statiquement. Le premier scénario à
départager sur hardware est le FUSB302 : une réconciliation normale fait cinq
lectures I2C toutes les 100 ms, une ligne `INT_N` maintenue basse transforme ce
travail en cinq lectures à chacun des deux appels USB de chaque superloop, et
une erreur I2C transforme le cas en retry de 2 ms environ toutes les 100 ms.

Deux autres familles restent crédibles : un job Storage/SD resté actif après
le boot, et une charge/rafale d'IRQ (surtout AUDIO) propre au boot. La refonte
FIFO a aussi introduit un coût `O(nombre_de_commandes_du_snapshot * taille_du_lot)`
sur toute publication CONTROL hors horizon. Il n'est actif en idle strict que
si une source publie malgré l'absence d'action (par exemple dérive du potard
master), donc il n'est pas à lui seul la meilleure explication du symptôme.

## Causes les plus probables

1. **FUSB302 / I2C périodique ou en retry.** Signature attendue : pic USB,
   `usb_refresh_error_count` qui monte à environ 10/s, ou `usb_int_low_count`
   proche de `usb_attention_count` et anormalement élevé.
2. **État Storage résiduel.** Une state machine RAM/wavetable/waveform/project
   non idle exécute des FatFs synchrones par quanta ; certaines boucles ont un
   budget de 2 ms et les opérations de métadonnées restent synchrones.
3. **IRQ AUDIO/USB/ADC trop occupante sur certains boots.** Signature : pics
   répartis entre plusieurs frontières DWT, `cpu_peak_permille` élevé, et PC
   souvent arrêté dans la même ISR lors d'échantillonnages manuels.

## Superloop idle réelle

Ordre exact dans `main()` puis `brick6_app_process()` :

1. `power_shutdown_service(HAL_GetTick())`
2. `board_usb_process()`
3. `engine_tasklet_poll()`
4. `brick6_stream_service_task_poll()`
5. `audio_domain_background_poll(8192)`
6. `seq_runtime_time_adapter_process()`
7. bloc Storage applicatif
8. `pattern_live_service()`
9. `brick6_master_control_process()` quand AUDIO tourne
10. second `brick6_stream_service_task_poll()`
11. `ui_boot_loading_service()`
12. Hall / sélection UI / pont clavier
13. `midi_poll()`
14. second `board_usb_process()`
15. service bootloader (retour immédiat après sa phase)
16. rattrapage UI depuis `engine_tick_count`, puis rendu OLED et flush SPI

| Frontière | Condition/coût idle | Bornes et I/O | Retour réellement idle |
|---|---|---|---|
| USB | Deux appels par tour. TinyUSB sans événement est court. | FUSB : I2C bloquant ; TinyUSB : 4 événements device ou 16 host par appel ; MIDI host : 8 messages. | Pas d'attention, poll 100 ms non dû, rôle actif stable. |
| ENGINE tick | Conversion TIM5 puis 0 à 8 ticks ; boutons, encodeurs, LED. | Boucle bornée à 8, sans SD/FatFs. | Moins de 32 frames en attente. |
| STREAM | Appelé deux fois ; scans fixes en idle. | File release drainée jusqu'à vide, capacité 16 ; 2 mailboxes ; SDMMC par DMA, une progression par appel. | Pas de page réservée/lease, pending I/O = 0, scheduler owner IDLE. |
| AUDIO_BG_LOCAL | Sampler/release + scan Looper. | Looper parcourt un nombre fixe de pistes ; pas d'I/O si EMPTY. | Aucune release et toutes les pistes Looper EMPTY. |
| SEQ | Même STOP, maintient un horizon de 64 frames et exécute NoteFx. | Horizon normalement un tour ; commandes NoteFx (31) et live-rec (128) peuvent être drainées ; scheduler events en lots de 128. | Files vides, aucun événement produit. |
| STORAGE | Plusieurs state machines appelées à chaque tour. | FatFs synchrone ; données 4/8/16 KiB ; RAM/wavetable ont 2 ms de budget grossier. | Tous états IDLE/DONE, aucune page/cache/preview en attente. |
| CONTROL | Pattern puis master ADC. | Pattern n'applique rien quand transport STOP. Une variation master >=128 publie dans la FIFO et parcourt le snapshot. | Pas de pattern pending et master dans la deadband. |
| Hall/MIDI | Hall hook quasi vide ; pont Hall draine `live_event` ; MIDI RX/TX limité à 16 paquets. | `live_event` peut être drainée jusqu'à vide (capacité 64). | Files vides. |
| UI | Rattrapage plafonné à 8 appels UI ; normalement 0 à 2 après les 8 ticks ENGINE max. | `ui_core_tick()` draine la file UI (capacité 32). | Aucun input/event. |
| RENDER/FLUSH | Rendu et flush cadencés à 16 ms. | Rendu CPU potentiellement multi-passe ; commandes SPI bloquantes timeout 20 ms, payload 128 octets/page en DMA. | Job rendu fini, DMA flush fini, driver READY. |

Recorder et Looper ne font que des tests/scans fixes dans le scénario donné si
leurs états sont réellement inactifs. Project/Pattern font de même uniquement
si leurs états de service sont revenus à IDLE/DONE.

## Pourquoi des à-coups et pourquoi le boot peut compter

Le FUSB possède exactement la cadence recherchée : deadline et retry à 100 ms.
Son état initial dépend du résultat DRP, du niveau `INT_N`, de l'état du bus I2C
et du succès d'activation du rôle. Les jobs Storage dépendent du projet restauré,
du média, des erreurs et des `pending_runtime`. Une configuration AUDIO restaurée
différente ou mal stabilisée peut enfin changer la charge IRQ, même en silence.

Après un long service, ENGINE consomme jusqu'à 8 ticks et l'UI rattrape les
ticks correspondants. Ce rattrapage peut amplifier visuellement un incident,
mais il est borné et n'explique pas seul son apparition périodique.

## FUSB302 / USB quantifié

- `board_usb_process()` est appelé **deux fois par superloop**.
- Sans attention, une seule réconciliation est due toutes les **100 ms**.
- `fusb302_refresh_state(true)` lit successivement `INTERRUPT`, `INTERRUPTA`,
  `INTERRUPTB`, `STATUS0`, `STATUS1A` : **5 transactions I2C**.
- Chaque transaction a un timeout HAL de **2 ms**. Une erreur arrête la série au
  premier registre fautif ; le plafond théorique d'une série est inférieur à
  10 ms, et le cas usuel d'un timeout précoce est proche de 2 ms.
- Si `INT_N` reste bas après une série réussie, `irq_pending` reste vrai et les
  **deux appels de chaque tour** relancent chacun cinq lectures, soit dix
  transactions par superloop.
- En cas d'erreur, `fusb_retry_waiting` bloque les nouvelles tentatives pendant
  100 ms : le résultat devient typiquement un à-coup périodique.
- Un échec persistant de démarrage device peut aussi réessayer à chaque tour ;
  un échec host peut recommencer un cycle de préparation/attente de 200 ms.

Ce code rend FUSB plausible, pas coupable confirmé. Les compteurs ajoutés
permettent précisément de prouver ou exclure ces signatures.

## FIFO / queues / deadlines

- FIFO CONTROL/AUDIO : lire `g_control_audio_fifo_layout`. En idle stable,
  `head-tail` doit rester faible et revenir à zéro ; `overflow_count` et
  `invariant_failure_count` ne doivent pas progresser.
- Snapshot FIFO : `g_audio_state_snapshot_depth` doit être 0 après boot.
  `g_audio_prepared_state.count` peut rester élevé. Depuis `06975b963`, chaque
  publication hors horizon appelle `audio_state_snapshot_control_absorb()` même
  quand le snapshot n'est pas actif. Le scan est linéaire par commande et un
  PROGRAM peut rescanner tout le snapshot. C'est un multiplicateur crédible
  lors d'une rafale d'encodeur ou d'une publication parasite, mais pas une
  source autonome de travail en idle strict.
- `sample_stream_transport_worker_poll()` draine sans quantum la file release,
  mais sa capacité est 16 ; les mailboxes sont au nombre de 2.
- NoteFx draine jusqu'à 31 commandes et 31 événements live ; live-rec peut en
  drainer 128. Ces files doivent être vides dans le scénario.
- Le rattrapage de deadline ENGINE et UI est borné ; il amplifie un blocage déjà
  survenu et ne crée pas une périodicité propre.

## Instrumentation temporaire

Instrumentation ajoutée : **oui**, sans log, allocation, UART ou écriture SD.
Le DWT existait déjà (`cpu_load_init()` active et valide `CYCCNT`). Onze
frontières sont mesurées : USB, ENGINE_TICK, CONTROL, STREAM, AUDIO_BG_LOCAL,
SEQ, STORAGE, HALL_MIDI, UI, RENDER et DISPLAY_FLUSH.

`g_idle_latency_diag` expose pour chaque frontière : `call_count`,
`last_cycles`, `max_cycles`, `slow_count`, plus `last_slow_service`,
`last_slow_cycles`, `worst_service`, `worst_cycles`, fréquence CPU et seuil.
USB ajoute cinq compteurs de décision. Coût attendu : deux lectures CYCCNT,
quelques écritures/branches par frontière, sans chemin bloquant ; ordre de
grandeur inférieur à la microseconde par frontière à 480 MHz. Les IRQ survenues
à l'intérieur d'une frontière sont incluses : des pics distribués aléatoirement
sont donc un indice d'IRQ, pas nécessairement du service englobant.

Index des services :

```text
0 USB             1 ENGINE_TICK      2 CONTROL
3 STREAM          4 AUDIO_BG_LOCAL   5 SEQ
6 STORAGE         7 HALL_MIDI        8 UI
9 RENDER         10 DISPLAY_FLUSH
```

Le seuil initial est 1 ms (`SystemCoreClock / 1000`).

## Meilleure méthode GDB

La méthode la moins invasive est : laisser tourner, puis poser un watchpoint
hardware sur la publication cohérente du dernier service lent. Il ne modifie
pas le timing avant l'événement et s'arrête juste après la frontière coupable.

```gdb
file build/Release/BRICK6_CUBE.elf
# Flasher/démarrer normalement, attendre l'écran nominal, puis Ctrl-C une fois.
p g_idle_latency_diag
p g_idle_latency_diag.max_cycles
p g_idle_latency_diag.slow_count

# 2 ms si le rendu normal déclenche trop souvent le seuil 1 ms :
set var g_idle_latency_diag.threshold_cycles = g_idle_latency_diag.core_clock_hz / 500
watch -l g_idle_latency_diag.last_slow_service
continue

# À l'arrêt automatique :
p g_idle_latency_diag.last_slow_service
p g_idle_latency_diag.last_slow_cycles
p (double)g_idle_latency_diag.last_slow_cycles * 1000000.0 / g_idle_latency_diag.core_clock_hz
bt 8
```

Pour une capture sans arrêt pendant l'observation, laisser tourner une durée
fixe (par exemple 10 s), interrompre une seule fois et lire :

```gdb
p g_idle_latency_diag
p g_usb_role
p g_fusb302
p/x GPIOC->IDR
p g_control_audio_fifo_layout
p g_audio_state_snapshot_depth
p g_audio_prepared_state.count
p g_sd_scheduler_runtime
p g_sd_block_device_async_count
p g_sample_stream_manager_pending_count
p g_project_save.state
p g_project_load.state
p g_pattern_async.state
p g_sampler_ram_load_job.state
p g_wavetable_load_job.state
p g_waveform_cache.active.state
p g_seq_runtime_live_rec_count
p g_note_fx_command_head
p g_note_fx_command_tail
p g_note_fx_live_queue_count
p midi_usb_rx_count
p midi_usb_tx_count
p g_live_event_head
p g_live_event_tail
p g_ui_evt_w
p g_ui_evt_r
p cpu_last_permille
p cpu_peak_permille
p cpu_peak_recent_permille
```

`GPIOC->IDR & 0x10` vaut 0 quand `INT_N` est bas. Les symboles `static` ci-dessus
sont présents dans l'ELF Release validé ; selon l'affichage LTO du GDB, il peut
être nécessaire de sélectionner le fichier source correspondant avant `p`.

Les interruptions manuelles répétées restent utiles en second niveau : faire
20 à 50 fois `Ctrl-C`, `x/i $pc`, `bt 6`, `continue`. Une résidence longue dans
`HAL_I2C_Mem_Read`, FatFs/SD, une ISR USB ou l'IRQ AUDIO ressort statistiquement.
Chaque arrêt perturbe fortement les deadlines et provoque du catch-up au resume ;
ne pas mélanger cette série avec la mesure DWT comparative.

Les breakpoints conditionnels dans les services sont plus intrusifs et demandent
de connaître déjà le suspect. Les watchpoints sur `head/tail` sont déconseillés
au premier passage : ils arrêtent trop souvent. Les utiliser seulement après
qu'une frontière a gagné.

## Protocole BOOT SAIN vs BOOT MAUVAIS

Pour chaque boot, repartir d'un reset complet afin que la BSS diagnostique soit
nulle, attendre l'écran normal, ne rien toucher et laisser exactement 10 s.
Faire un seul halt, sauvegarder les valeurs suivantes, puis recommencer sur un
boot de l'autre classe :

```text
core_clock_hz, max_cycles[0..10], slow_count[0..10], worst_service/worst_cycles
USB: attention, int_low, periodic_poll, refresh_ok, refresh_error
FIFO: head, tail, overflow_count, invariant_failure_count
snapshot_depth, prepared_state.count
SD: scheduler owner, block pending_count, stream pending_count, états de jobs
AUDIO: last/peak/recent_peak permille
queues: NoteFx, live-rec, MIDI RX/TX, live_event, UI event
```

Interprétation minimale :

- USB max haut + erreurs ~10/s : retry I2C/FUSB confirmé comme mécanisme.
- `int_low` presque égal aux appels USB + refresh OK très élevé : `INT_N` bas
  et cinq lectures répétées à chaque appel.
- STORAGE/STREAM max haut + état non idle ou pending non nul : job résiduel.
- Pics similaires dans plusieurs services + charge AUDIO élevée : préemption
  IRQ ; confirmer par sampling PC.
- CONTROL haut avec FIFO qui avance et snapshot `count` élevé : rechercher la
  publication parasite (master ADC en premier).
- RENDER haut identique sur les deux boots : coût périodique normal, pas la
  différence recherchée.

## Correctif ciblé après mesure hardware

La mesure a isolé STORAGE à environ 1 ms presque à chaque passage. Le chemin
statique correspondant est `sampler_ram_pool_waveform_service(1024)` : chaque
sample RAM publié démarrait immédiatement un overview, puis 1024 frames SDRAM
étaient converties et agrégées à chaque superloop, même si la page waveform
n'était jamais affichée. La construction est désormais à la demande : le
getter utilisé par le renderer démarre l'overview, et le service ne visite que
les slots réellement `BUILDING`.

Le diagnostic STORAGE expose maintenant les tableaux suivants, avec les index
dans l'ordre réel d'appel :

```text
storage_call_count[0..15]
storage_last_cycles[0..15]
storage_max_cycles[0..15]
storage_slow_count[0..15]

0 recorder              1 project save       2 project load
3 patch                 4 multi priority      5 multi retire
6 RAM retire            7 wavetable retire    8 RAM loader
9 wavetable loader     10 project asset      11 RAM waveform
12 multi               13 pattern            14 waveform cache
15 preview
```

Pour FUSB302, EXTI est le chemin normal. Les registres read-to-clear sont
interprétés pour distinguer attach, detach, changement CC et erreur. Un attach
fige la configuration CC correspondant au rôle détecté. Un detach arrête le
rôle courant puis relance le toggle DRP, ce qui arme le replug suivant. Il
n'existe plus de poll permanent à 100 ms : cette cadence est réservée aux
erreurs I2C et à une ligne `INT_N` restant basse après acquittement. Le watchdog
de secours relit l'identité, `CONTROL2`, `STATUS0` et `STATUS1A` toutes les 5 s.
`usb_periodic_poll_count` compte désormais ces watchdogs et `usb_retry_count`
les retries effectivement exécutés.

Le rôle issu de `TOGSS` est latché à l'attach. Après `TOGDONE`, le firmware
arrête volontairement `TOGGLE` pour configurer la mesure CC du rôle stable ;
les lectures suivantes de `STATUS1A` ne doivent donc plus réinterpréter
`TOGSS=RUNNING/undefined` comme `NONE`. Seul un detach confirmé ou un recovery
watchdog invalide ce rôle et relance le DRP. Les compteurs `usb_attach_count`,
`usb_detach_count`, `usb_device_start_count`, `usb_device_stop_count`,
`usb_drp_restart_count` et `usb_watchdog_recovery_count` permettent de vérifier
qu'une énumération Device saine ne subit ni stop ni restart intermédiaire.

Capture GDB après reset et 10 s d'idle :

```gdb
p g_idle_latency_diag.last_cycles[6]
p g_idle_latency_diag.max_cycles[6]
p g_idle_latency_diag.slow_count[6]
p g_idle_latency_diag.storage_last_cycles
p g_idle_latency_diag.storage_max_cycles
p g_idle_latency_diag.storage_slow_count
p g_idle_latency_diag.usb_attention_count
p g_idle_latency_diag.usb_int_low_count
p g_idle_latency_diag.usb_periodic_poll_count
p g_idle_latency_diag.usb_refresh_ok_count
p g_idle_latency_diag.usb_refresh_error_count
p g_idle_latency_diag.usb_retry_count
p g_idle_latency_diag.usb_attach_count
p g_idle_latency_diag.usb_detach_count
p g_idle_latency_diag.usb_device_start_count
p g_idle_latency_diag.usb_device_stop_count
p g_idle_latency_diag.usb_drp_restart_count
p g_idle_latency_diag.usb_watchdog_recovery_count
```

Attendu en idle sans affichage waveform : `storage_last_cycles[11]` reste au
coût d'un scan de slots sans traitement de frames, et
`storage_slow_count[11]` ne progresse pas. Une construction demandée par la page
waveform reste quantifiée et visible à l'index 11. Côté USB, un `INT_N` bloqué
bas ne fait progresser `usb_refresh_ok_count` qu'à la deadline de retry 100 ms,
pas à chaque appel. En l'absence d'événement ou d'erreur, les seules transactions
FUSB sont celles du watchdog 5 s.
