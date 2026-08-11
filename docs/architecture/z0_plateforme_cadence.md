# Z0 - Plateforme / Cadence

## Addendum 2026-08-03 - traitement Hall low-cost en IRQ ADC DMA

- Sur Low-Cost, `HAL_ADC_ConvCpltCallback()` synchronise ADC1/ADC2, applique le rejet de stabilisation MUX, puis appelle directement `hall_engine_process_sample()` pour chaque mesure acceptee. La position, l'automate d'attaque, la velocite et les drapeaux note-on/off ne dependent donc plus de la cadence de la superloop.
- La cadence physique reste une mesure acceptee par touche toutes les 2,8 ms. Ce changement supprime le backlog logiciel entre acquisition et validation sans modifier les seuils, l'hysteresis, la calibration ni l'algorithme de velocite.
- `hall_keyboard_bridge_process()` reste hors IRQ: il consomme les drapeaux pending, applique les regles UI/injection puis publie vers le pipeline clavier. Aucun appel UI, MIDI, Note-FX ou moteur audio n'est effectue depuis l'IRQ Hall.
- L'IRQ audio conserve sa priorite superieure. Le traitement Hall accepte son jitter de preemption audio, mais ne peut plus etre repousse par les services SD, UI, USB ou autres travaux cooperatifs de la superloop.
- Validation materielle: sur la cible Low-Cost testee, le passage du traitement Hall dans l'IRQ a supprime la latence perceptible, y compris sous la charge audio qui amplifiait auparavant le retard.
- Premium suit désormais le même chemin borné que Low-Cost dans le callback ADC DMA. Sa FIFO ADC brute reste disponible pour le diagnostic uniquement; elle ne participe plus à la décision musicale.

## Addendum 2026-08-01 - attributs MPU de la SDRAM externe

- Les deux variantes configurent la region MPU 3 sur les 32 MiB de SDRAM a `0xC0000000` en memoire normale, full-access, non-shareable, write-back/write-allocate et XN avant activation du D-cache.
- La region MPU 4, de priorite superieure, recouvre le dernier MiB a `0xC1F00000` en memoire normale shareable, non cacheable, non bufferable et XN. Elle porte exclusivement les rings Recorder et le preroll Looper places par les linkers.
- La region 1 D2 DMA et la region 2 Backup SRAM conditionnelle restent inchangees. `Release`, `Test`, `Premium` et `TestPremium` utilisent donc les memes numeros et attributs SDRAM, sans depasser les huit regions du Cortex-M7.

## Addendum 2026-07-30 - cadence Hall low-cost sans ASC

- Le timer/MUX low-cost publie une mesure valide par touche toutes les 2,8 ms. Chaque mesure acceptee est transmise directement a `hall_engine_process_sample()` depuis le callback ADC DMA, sans moyenne numerique ASC ni FIFO de traitement en superloop.
- Le filtre analogique du PCB est l'autorite de lissage low-cost. Les seuils, l'hysteresis, la position, les transitions note-on/off et la calibration restent dans les autorites Hall existantes.
- Le mode TIME low-cost utilise des bornes comptees a 2,8 ms (`4..56` mesures) afin de conserver sa fenetre temporelle reelle historique. Le debug Hall annonce 2,8 ms et distingue la mesure ADC de la valeur recue par le moteur.
- Les deux variantes transmettent chaque mesure Hall brute calibrée à `hall_engine_process_sample()` : aucun filtre numérique ASC multi-échantillons ne retarde le press, la navigation ou le Note On.
- La cadence d'acceptation reste de 2,8 ms par touche en Low-Cost et 0,8 ms en Premium ; la calibration, les seuils relatifs et l'hystérésis restent dans `hall_engine`.

## Addendum 2026-07-30 - MT-12 replay du dernier crash

- `monkey_test_replay` regenere le flux a partir de la seed de l'archive MT-10 et refuse le replay si le breadcrumb de l'index fautif n'est plus disponible dans la fenetre des 16 actions.
- Le lifecycle repart du snapshot jetable defini par MT-05, arme la surveillance MT-06/MT-09, puis rejoue chaque prefixe a son tick logique original dans le seam MT-04. Les temporisations de press/release ne sont pas accelerees afin de conserver leur semantique.
- A l'echeance de l'action cible, l'orchestrateur passe en `REPLAY_PAUSED` avant tout commit/injection. L'injection reste une action operateur explicite; en Debug, `BKPT` n'est execute que si le debugger est attache. Le replay peut toujours etre arrete et restaure sans injecter la cible.
- Le breadcrumb regenere doit correspondre bit pour bit aux champs structurants archives. Une divergence ou une fin de generateur est un arret controle `REPLAY MISMATCH`, pas une tentative approximative.
- La seed, l'ordre et les ticks logiques sont reproductibles, mais pas l'ordonnancement fin des IRQ/DMA, les latences SD, l'etat des caches ni les delais materiels. Un fault dependant de ces evenements peut donc ne pas reapparaitre malgre un replay logique valide.

## Addendum 2026-07-30 - AUDIO TEST 2

- `audio_test2_service()` prépare REFERENCE, draine INTERNAL, vérifie les
  fichiers et cadence les comptes à rebours hors IRQ.
- Le transport et les playheads sont suspendus/restaurés à chaque lecture. Une
  annulation ou erreur désarme toujours le seam audio avant restauration.

## Addendum 2026-07-30 - MT-11 orchestration du journal

- `monkey_test_log_service()` est appele dans la superloop, hors IRQ et avant le tick Monkey. Il ne travaille qu'en presence d'un rapport ou evenement RAM pending et ne patiente jamais pour obtenir `sd_access_gate`.
- Les evenements de session sont bornes a `START`, `PERIODIC` toutes les dix minutes et `STOP`. Les actions continuent d'etre couvertes par les 16 breadcrumbs Backup SRAM; aucune ecriture SD n'est effectuee par action.
- Apres crash, le service tente d'abord le rapport archive complet. En cas d'echec, il conserve le pending et espace les nouvelles tentatives de 5 s; la reprise automatique MT-10 reste independante.

## Addendum 2026-07-30 - MT-10 classification et reprise boot

- `crash_capsule_capture_reset_flags_early()` lit `RCC->RSR` dans les deux `main()` avant `HAL_Init()`, conserve la valeur en RAM interne puis pose `RCC_RSR_RMVF`. Aucun service applicatif ne peut donc effacer la cause avant sa classification.
- Au chargement de la Backup SRAM, une capsule deja `FAULTED` est associee au reset en conservant son fault Cortex-M7. Une capsule `RUNNING` accompagnee de `IWDG1RSTF` devient `FAULTED/WATCHDOG` et incremente son compteur de crash. Une session `RUNNING` interrompue par un autre reset est fermee `STOPPED`, ce qui evite de classer plus tard un watchdog sans rapport.
- La Backup SRAM occupe desormais 2 KiB: 1 KiB double-slot pour la session courante et 1 KiB double-slot pour l'archive du dernier crash. Les deux banques utilisent generation, CRC32 et publication du commit en dernier; la nouvelle session ne peut pas ecraser la seed, l'index, les breadcrumbs ou le contexte fault de l'archive.
- `monkey_test_init()` importe l'archive pour l'affichage, le futur rapport MT-11 et le replay explicite. Si le crash vient du boot courant, `monkey_test_tick()` attend la fin de `ui_boot_loading`, puis lance une seed differente derivee par ajout de `0x9E3779B9`; aucune action n'est blacklistee.
- L'etape ne journalise pas sur SD. En cas d'echec de preparation de la session jetable ou d'armement IWDG, la reprise s'arrete proprement au lieu de boucler.

## Addendum 2026-07-30 - MT-09 IWDG et heartbeat de boucle complete

- Aucun module HAL IWDG n'etant present dans le projet, le watchdog diagnostic configure directement `IWDG1` via CMSIS. L'activation LSI et les attentes de mise a jour sont bornees a 100 ms.
- L'armement intervient apres l'ouverture de la session et de la capsule MONKEY TEST. La configuration LSI `/256`, reload `1499`, donne un timeout nominal de 12 s; une fois demarre, l'IWDG reste actif jusqu'au reset.
- Le reload est emis uniquement en fin des deux superloops carte, apres application, USB/MIDI, UI, renderer et flush display, et seulement si `engine_tick_count` a progresse. Une panne audio ou un blocage de la boucle complete interdit donc le heartbeat.
- Aucun chemin IRQ/DMA, tasklet intermediaire ou handler de fault ne reload l'IWDG. MT-08 conserve son reset systeme explicite et ne depend pas du watchdog.
- Le build `Debug` applique le freeze IWDG1 sous debugger; le build `Test` ne le fait pas. `Release` et `Premium` n'embarquent ni source, ni appel watchdog diagnostic.
- La capsule MT-07 passe en version 3 et conserve l'etat d'armement, le compteur de heartbeats et le dernier tick moteur confirme, avec checkpoint au plus une fois par seconde. La qualification du reset IWDG au boot reste reservee a MT-10.

## Addendum 2026-07-30 - MT-08 handlers de fault bornes

- `HardFault`, `MemManage`, `BusFault` et `UsageFault` utilisent des trampolines nus: bit 2 de `EXC_RETURN` choisit MSP ou PSP, bit 4 localise la frame Cortex-M7 apres une eventuelle frame FP et une pile DTCM diagnostic de 1 KiB accueille le code C de capture.
- La capsule enregistre fault type, registres empiles `R0..R3/R12/LR/PC/xPSR`, SP, `EXC_RETURN`, `CFSR/HFSR/DFSR/AFSR/BFAR/MMFAR/ICSR/SHCSR`, puis publie le slot CRC avant `NVIC_SystemReset()`. Une adresse de frame hors des SRAM internes connues n'est jamais dereferencee.
- Les faults configurables sont actives a l'init diagnostic afin de conserver leur classe au lieu d'une escalade systematique en HardFault. Aucun handler n'appelle FatFs, HAL peripherique, UI, allocation ou watchdog.
- Le reset explicite est inconditionnel, y compris si aucune session/capsule n'est active ou si l'IWDG MT-09 n'existe pas encore. Dans les builds normaux, les quatre trampolines ecrivent directement `SYSRESETREQ` sans pile ni trace diagnostic et n'attendent plus indefiniment.
- En `Debug` avec un probe effectivement attache, un BKPT est emis apres commit et avant le reset; continuer l'execution declenche ensuite le reset explicite. Sans probe, et dans `Test`, aucun break n'est execute.

## Addendum 2026-07-30 - MT-07 Backup SRAM et breadcrumbs

- Les deux linkers actifs declarent les 4 KiB de Backup SRAM a `0x38800000` et une section `.backup_sram` `NOLOAD`. La capsule utilise deux slots alignes de 512 octets; un assert linker interdit tout depassement.
- En build diagnostic, la region MPU 2 couvre les 4 KiB en shareable, non cache, non bufferable et XN avant activation du D-cache. L'init active l'acces backup, l'horloge BKPRAM et le regulateur de retention; un echec interdit le lancement Monkey.
- Le format v2 contient seed, index et tick logique, compteurs, contexte processeur/reset/fault et un ring de 16 actions compactes. CRC32, generation monotone, double slot et marqueur ecrit en dernier preservent l'ancien slot si un reset interrompt un commit.
- Le breadcrumb de l'action due est committe avant son injection. Un arret manuel committe `STOPPED`; aucun acces FatFs, allocation ou maintenance D-cache n'intervient.
- Le depot ne contient aucun schema produit etablissant le cablage de `VBAT`: la retention est garantie a travers un reset sous VDD, mais sa conservation apres retrait complet de l'alimentation reste une validation carte obligatoire.

## Addendum 2026-07-30 - MT-06 supervision bornee

- `monkey_test_monitor` sonde toutes les 150 periodes moteur (10 Hz), sans allocation ni rattrapage de polls, les compteurs CPU, Sampler et Looper ainsi que les bornes track/hall/page UI.
- Chaque famille ne produit au plus qu'un warning ou une erreur par poll. Les depassements CPU et underruns sont recuperables; une borne UI invalide ou la corruption des sentinelles du moniteur arrete proprement la session et declenche la restauration jetable.
- Les compteurs ne sont jamais remis a zero par Monkey: le moniteur capture des baselines de session et ignore une regression de compteur afin de ne pas transformer un reset externe en faux delta.

## Addendum 2026-07-30 - MT-05 session jetable

- Le lancement Monkey capture un `ProjectSaveV1` en SDRAM diagnostic, le focus UI, le hall mode, le transport, les playheads et le gain master, puis applique le snapshot de boot comme etat live jetable.
- L'undo est suspendu pendant la session. L'arret coupe transport et notes, restaure le snapshot RAM et les autorites UI, puis reprend le transport seulement s'il etait actif avant le test.
- Le demarrage est refuse si la SD ou un writer/converter est deja actif. Le gain master est borne a `0.25` pendant la session laissee sans surveillance.
- Le snapshot ajoute `0x53F0C` octets (environ 336 KiB) de SDRAM uniquement dans `Debug`/`Test`; il n'ajoute aucune RAM aux firmwares normaux.

## Addendum 2026-07-30 - AUDIO TEST calibration de volume percu

- La phase moteurs contient 3576 scenarios automatiques: C2/C4/C6, matrice
  TIMBRE/COLOR uniquement pour PRISM et STACK, combinaisons multi-oscillateurs
  reelles et trois frappes/declenchements pour les modeles aleatoires.
- Un scenario continu capture 100 ms `ATTACK`, attend 300 ms depuis le note-on,
  puis capture 1 s `SUSTAIN`. Un scenario percussif conserve la meme attaque et
  accumule `STRIKE` jusqu'a trois fenetres silencieuses de 50 ms, avec timeout
  borne a 3 s.
- Les 26 scenarios FILTER/SUM/MASTER/FX historiques restent a la suite. La
  progression porte donc 3602 scenarios, puis 60 lignes de synthese sont
  serialisees avant restauration du snapshot. Duree estimee: environ 103 min.

## Addendum 2026-07-30 - MT-04 injection bornee

- Les actions Monkey dues sont injectees hors IRQ par une primitive generique `diagnostic_input`, independante de `audio_test_*`.
- Les boutons rejoignent la file `ui_event` avec une origine diagnostic explicite et un etat maintenu compatible avec les combos; les encodeurs alimentent l'accumulateur borne du driver; les touches passent par `keyboard_runtime_process_hall`.
- L'arret libere tous les boutons et toutes les touches synthetiques encore maintenus. Une cible invalide incremente le compteur d'avertissements sans bloquer la superloop.

## Addendum 2026-07-30 - MT-03 generateur deterministe

- `monkey_test_action` porte un PRNG xorshift32 sans allocation, initialise par une seed non nulle enregistree dans la vue. `monkey_test_start_seed()` permet deja de reconstruire exactement le flux depuis une seed explicite; `monkey_test_start()` fabrique seulement la seed d'une nouvelle session.
- Chaque action atomique contient `index`, `type`, `target`, `value`, `delay_ticks` et `logical_tick`. La base logique est `engine_tick_count` a 1500 Hz; `HAL_GetTick()` reste limite au temps d'affichage et ne decide pas l'ordre des actions.
- Les familles ponderees produisent taps, maintiens, combos SHIFT, accords de boutons, encodeurs usuels/extremes et notes avec velocite. Les gestes composes sont developpes dans une file statique de quatre press/release au plus.
- Le service consomme au maximum huit actions dues par passage de superloop. A MT-03, elles alimentent uniquement les compteurs et le dernier type affiche: aucune action synthetique n'est encore injectee dans l'UI ou le clavier.

## Addendum 2026-07-30 - MT-02 socle autonome MONKEY TEST

- `monkey_test` est un module de diagnostic autonome, initialise et servi par la superloop uniquement avec `BRICK_TEST_BUILD=1`; sa source est exclue de `Release` et `Premium`.
- Cette etape porte seulement le lifecycle `IDLE/RUNNING/STOPPED`, le temps d'affichage et les compteurs reserves. Elle n'injecte encore aucune action et ne diagnostique aucun fault.
- Le module ne depend ni de `audio_test_runner`, ni de `audio_test_csv`, ni de leurs donnees. Les appels d'orchestration juxtaposes dans `brick6_app_init` ne creent aucune autorite commune.

## Addendum 2026-07-30 - frontiere firmware Test

- Le preset CMake `Test` utilise `CMAKE_BUILD_TYPE=Release` et la variante low-cost: il conserve donc les optimisations, le LTO et les contraintes temps reel de `Release`.
- L'etape stable `MT-01.5` active aussi `BRICK_TEST_BUILD=1` dans le preset `Debug`, qui conserve ses options `-Og -g3` et embarque les memes pages et sources de diagnostic que `Test` pour le debogage sur cible.
- `BRICK_TEST_BUILD` est une option CMake numerique centralisee, forcee a `1` par `Debug` et `Test`, et a `0` par `Release` et `Premium`. Les sources `audio_test_runner`, `audio_test_csv` et `audio_track_diag` sont exclues de ces deux derniers.
- `brick6_app_init` n'initialise et ne sert les modules de diagnostic que dans `Debug` et `Test`. Le workflow explicite du firmware representatif est `cmake --preset Test`, `cmake --build --preset Test`, puis `flash_test.bat`.
- Verification low-cost du 2026-07-30: `Release` et `Test` compilent et lient avec les memes flags Release. Le surcout diagnostic mesure est de 25 280 octets Flash, 12 768 octets RAM_D1 et 194 944 octets SDRAM; DTCM/RAM_D2/RAM_D3/ITCM sont inchanges.

## Addendum 2026-07-30 - cadence AUDIO TEST automatique

- Le runner est servi par la superloop hors IRQ, y compris après une sortie de page, afin qu'une annulation attende l'écriture éventuellement engagée puis restaure le snapshot sans dépendre du tick UI.
- `audio_test_runner_tick()` est cadencé par la superloop hors IRQ. La machine conserve le chemin simple `PREPARE -> CONFIGURE -> NOTE_ON -> WARMUP -> MEASURE -> CAPTURE -> NOTE_OFF -> WRITE -> NEXT -> RESTORE`; les cas FX insèrent après `NOTE_OFF` deux fenêtres `FX_TAIL_EARLY/FX_TAIL_LATE`, sans écriture SD, avant les deux écritures `FX_ACTIVE` puis `FX_TAIL`.
- Le writer FatFs reste un service superloop séparé; le runner attend son acquittement durable pendant le silence avant de configurer le cas suivant.
- Le catalogue de diagnostic courant borne ses sommes à huit pistes. Les cas et durées exacts sont définis par `audio_test_runner.c`; ce diagnostic n'ajoute aucune identité de piste au produit.

## Addendum 2026-07-29 - boot codec borne et identique cold/warm/GDB

- `board_audio_codec_init()` initialise uniquement le diagnostic de boot; la configuration physique est maintenant faite dans `board_audio_start_stream()` apres zero/clean du buffer TX.
- Pour les deux variantes, le TX DMA zero est lance seul afin d'etablir MCLK/BCLK/LRCK. Le codec est ensuite explicitement reset, configure et verifie; le RX DMA, autorite des callbacks IRQ audio, ne demarre qu'apres validation complete.
- Chaque tentative est bornee (ACK I2C, lectures de retour et flags de power-ready) et un echec declenche une seconde tentative complete apres arret SAI/DMA et nouveau reset codec.
- Cette remise a zero explicite supprime la difference de principe entre cold boot, reset MCU et flash/reset GDB: aucun etat conserve du codec externe n'est accepte comme prerequis.
- `board_audio_get_boot_diag()` expose hors IRQ `last_error`, `init_count`, `failure_count`, `retry_count`, `codec_ready` et `stream_started`.
- Le diagnostic low-cost expose aussi l'etape TLV320AIC3204, le dernier triplet page/register/attendu/masque/relu, l'etat TX/RX et les validations reset, clocks, interface, DAC, routage, volumes, power et unmute. Les echecs ACK, reset, I2C/readback, clocks, routage, volume, power de sortie et mute sont donc distinguables sans log ni acces IRQ.

## Addendum 2026-07-29 - copie ITCM au reset

- Les linker scripts Flash low-cost et premium exposent `.itcm_text` en execution ITCM `0x00000000` avec image de chargement en Flash, symboles `__itcm_text_load__`, `__itcm_text_start__`, `__itcm_text_end__`, et entree `.itcm_text` / `.itcm_text.*`.
- Les startups reellement compilees `Board/LowCost/Generated/Startup/startup_stm32h743xx.s` et `Board/Premium/Generated/Startup/startup_stm32h743xx.s` copient `.itcm_text` depuis Flash vers ITCM juste apres `SystemInit()` et avant la copie `.data`, le zero `.bss`, les constructeurs C++ et `main()`.
- Les caches I/D ne sont actives que plus tard dans `main()`, donc cette copie ne necessite pas de maintenance cache locale. L'MPU courant ne declare pas de region ITCM XN et `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT)` garde l'ITCM executable par defaut.

## Addendum 2026-08-05 - primitives Deluge integrees a Stack

- Les primitives oscillateur Deluge retenues sont rendues par Stack sous les modeles `SINE`, `TRI`, `SQUARE` et `SAW`; aucun moteur Deluge selectable ni initialisation runtime separee ne subsiste.
- Les tables anti-alias et le renderer interne Deluge restent compiles uniquement comme dependances de ces modeles Stack; Prism, Wave et les autres moteurs gardent leurs chemins propres.

## 1. Perimetre

Perimetre operationnel de zone (appartient a Z0):
- `Board/<Variant>/Generated/Src/main.c`
- `Src/Core/brick6_app_init.c`
- `Inc/Core/brick6_app_init.h`

Elargissements necessaires (preuve de cadence et points periodiques):
- `Src/Core/engine_tasklet.c` + `Inc/Core/engine_tasklet.h`: base de cadence non-RTOS derivee des frames audio.
- `Src/UI/ui_tasklet.c` + `Inc/UI/ui_tasklet.h`: init lazy UI et tick UI dans superloop.
- `Src/UI/ui_renderer_oled.c`: service periodique de rendu OLED complet (16 ms) appele en superloop.
- `Src/UI/display_flush_service.c`: service periodique display flush (16 ms) appele en superloop, hors continuation d'un flush DMA deja actif.
- `Src/MIDI/midi.c`: callback IRQ TIM12 et callback TIM5 utilises par la cadence globale.
- `Src/MIDI/midi.c` + `Src/MIDI/midi_host.c`: services MIDI device/host cadences hors IRQ.
- `Board/<Variant>/UsbStack/usb_host.c` + `Board/<Variant>/UsbStack/usbh_conf.c` + `Src/MIDI/usbh_midi.c`: service USB Host CubeMX, IRQ HCD et classe MIDI host.
- `Board/<Variant>/Generated/Src/stm32h7xx_it.c`: branchement IRQ TIM12/TIM5 vers HAL, plus service `PendSV` pour flush TX USB MIDI differe.
- `Board/<Variant>/Generated/Src/tim.c`: configuration frequence TIM12 (1500 Hz) et activation IRQ associee.
- `Src/Core/brick6_app_init.c`: service superloop preview SD (`sd_preview_process()`) hors IRQ.
- `Src/Core/brick6_app_init.c`: init et service cooperatif de l'`audio_recorder` commun Audio Rec/Looper, hors IRQ.
- `Src/Core/brick6_app_init.c`: chargement boot de la photo catalogue samples persistante via `wav_loader_catalog_init_load()`, sans scan automatique de `0:/Samples`.
- `Src/Core/brick6_app_init.c`: les recorders historiques sont retires; le record produit passe par `audio_recorder` + `generic_recorder`.

Sous-roles internes dans `brick6_app_init.c`:
- Orchestrateur boot produit: initialisation ordered des sous-systemes applicatifs.
- Wiring inter-zones: enregistrement callback audio DSP et chaines runtime.
- Service loop applicative: aggregation des services hors IRQ audio.

Dependances de Z0 sans appartenance:
- HAL/CubeMX (`MX_*`, `HAL_*`, clocks, peripherals) selectionne par `BRICK6_VARIANT`.
- Interfaces Board (`Inc/Board/*`) et implementations `Board/Premium/*` / `Board/LowCost/*`.
- Z1 audio (`audio_*`, `brick6_audio_runtime_*`, mixer/synth).
- Z2/Z3/Z4/Z5/Z6 via appels d'init/service delegues.
- USB host/device et MIDI host.

Exclusions explicites:
- Tout pipeline audio IRQ (`Src/Audio/audio.c` callbacks DMA) appartient a Z1, pas a Z0.
- Logiques metier UI/Seq/Param/Storage ne font pas partie de Z0, elles sont seulement orchestrees.
- Drivers CubeMX individuels (`adc.c`, `sai.c`, etc.) exclus du perimetre de gouvernance Z0.

## 2. Autorite(s) de verite

Autorite boot principal:
- `main()` dans `Board/<Variant>/Generated/Src/main.c` est le point d'entree unique observe.
- `BRICK6_VARIANT=premium|lowcost` selectionne un seul arbre Generated, un seul startup et un seul linker.

Autorite init systeme MCU/HAL:
- Dans `main()`:
  - `MPU_Config()` (region DMA non-cacheable),
  - `SCB_EnableICache()`, `SCB_EnableDCache()`,
  - `HAL_Init()`,
  - `SystemClock_Config()`, `PeriphCommonClock_Config()`,
  - puis `MX_*_Init()`.

Autorite init sous-systemes projet:
- `brick6_app_init()` dans `Src/Core/brick6_app_init.c`.
- Les frontieres physiques audio, USB, power, controls, surface, LED et display passent par `Inc/Board/*`; premium et low-cost partagent le codec TLV320AIC3204 et le contrat audio stereo 24-bit/2 slots, avec seulement le routage physique SAI/I2C/DMA propre à la carte; low-cost ajoute FUSB302/role USB dynamique et POWER_HOLD.

Autorite wiring global inter-zones:
- `brick6_app_init()`:
  - `audio_init(&hsai_BlockA2, &hsai_BlockB2)`
  - `audio_set_float_callback(brick6_audio_runtime_dsp)`
  - ordre d'init runtime (drum/sampler/Prism, puis param/seq/storage/undo/control/hall/etc.).
- `brick6_audio_runtime_dsp()`:
  - point d'injection MAIN pour la preview SD via le buffer de lecture pre-resample.

Autorite cadence/service loop hors audio IRQ:
- `while(1)` dans `main()` + `brick6_app_process()`.
- `engine_tasklet_poll()` est l'autorite de consommation de la cadence derivee audio cote non-IRQ.

Tasklets/timers periodiques observes:
- TIM12 IRQ -> `HAL_TIM_PeriodElapsedCallback` (dans `midi.c`) -> `seq_runtime_time_adapter_process_internal_from_irq()`.
- TIM5 OC IRQ -> `HAL_TIM_OC_DelayElapsedCallback` -> `midi_clock_on_timer_tick()`.
- `engine_tasklet_notify_frames()` (alimente depuis IRQ audio Z1) -> `engine_tasklet_poll()` en superloop.
- `PendSV` -> `midi_usb_tx_deferred_service_from_isr()` pour lancer un premier flush TX USB MIDI hors superloop apres enqueue ISR.
- `OTG_HS_IRQn` -> `HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS)` pour USB Host; priorite 7, sous SAI2/DMA audio a 1.

Seconde autorite concurrente:
- Aucune seconde autorite concurrente complete pour la sequence boot/system init/superloop.
- Cadence seq a deux chemins mais avec ownership exclusif par source clock:
  - interne: Z1 audio bloc (`seq_runtime_audio_collect_block_events` -> drive internal steps).
  - externe: superloop recoit les pulses MIDI puis les met en pending; la consommation d'avance step reste en Z1 audio bloc (`seq_runtime_audio_collect_block_events`).
  TIM12 reste un ticker auxiliaire interne, non autorite d'avance step.

## 3. API entrantes

Qui appelle Z0:
- Boot CPU/reset appelle `main()`.
- Superloop appelle `brick6_app_process()` depuis `main()`.
- HAL IRQ appelle:
  - DMA ADC1/ADC2 -> `HAL_ADC_ConvCpltCallback()` -> traitement Hall direct sur Low-Cost.
  - `TIM8_BRK_TIM12_IRQHandler()` -> `HAL_TIM_IRQHandler(&htim12)` -> `HAL_TIM_PeriodElapsedCallback`.
  - `TIM5_IRQHandler()` -> `HAL_TIM_IRQHandler(&htim5)` -> `HAL_TIM_OC_DelayElapsedCallback`.

Contrats implicites d'ordre:
- MPU region DMA doit etre configuree avant activation cache CPU (verifie dans `main()`).
- Horloges/system clocks avant `MX_*_Init()`.
- `audio_set_float_callback()` doit etre pose avant `audio_start()`.
- `engine_tasklet_init()` doit preceder l'usage de `engine_tick_count` pour UI cadence.
- `ui_tasklet_poll()` initialise UI au premier tick engine, pas pendant boot `brick6_app_init()`.
- `engine_tasklet_poll()` consomme au plus `ENGINE_TASKLET_MAX_TICKS_PER_POLL` ticks par appel; le backlog restant est conserve pour les tours suivants.
- Premium: `hall_loop_process()` consomme au plus `HALL_LOOP_MAX_SAMPLES_PER_POLL` samples Hall ADC FIFO par appel; le backlog restant reste dans la FIFO Hall. Low-Cost ne depile plus cette FIFO: son moteur Hall est alimente dans l'IRQ ADC DMA.

## 4. API sortantes

Z0 appelle principalement:
- System/HAL/Cube:
  - `HAL_Init`, clocks config, `MX_*_Init`, timers start (`HAL_TIM_Base_Start`, `HAL_TIM_OC_Start`, `HAL_TIM_Base_Start_IT`).
- Boot produit via `brick6_app_init()`:
  - init audio runtime, synths, sampler, sequencer, storage, undo, controls, hall, MIDI.
- Runtime via `brick6_app_process()`:
  - tasklets/services metier (`engine_tasklet_poll`, `seq_runtime_time_adapter_process`, `pattern_live_service`, `hall_loop_process`, `voice_manager_service`, etc.).
- Boucle `main()` appelle en plus:
  - `MX_USB_HOST_Process`, `midi_host_poll_bounded(8)`,
  - `ui_tasklet_poll` conditionne par `engine_tick_count`,
  - `ui_renderer_oled_service_poll`, `display_flush_service_poll` apres init UI.

## 5. Etats structurants possedes

### `Src/main.c`
- `last_tick` (`uint32_t`, local `main`):
  - Ecriture: dans la boucle quand `engine_tick_count` change.
  - Lecture: compare avec `engine_tick_count`.
  - Role: cadence de `ui_tasklet_poll` (1 appel par tick engine detecte).

### `Src/Core/brick6_app_init.c`
- `g_sample_pool_data[...]` (SDRAM_SAMPLES float array):
  - Ecriture: `sample_pool` lors du chargement WAV ou du restore projet.
  - Role: arena residante des samples projet, dimensionnee pour absorber le reste disponible de la SDRAM apres les reserves fixes; le boot n'injecte plus de sample par defaut.
- `g_audio_recorder_ring[...]` et les deux write buffers dans `audio_recorder.c` (SDRAM_RECORDER):
  - Role: capture SPSC commune Looper/Audio Rec et double-buffer PCM24, draines hors IRQ.
- `g_looper_preroll_pcm[...]` dans `brick6_looper_runtime.c` (SDRAM_RECORDER int32 stereo):
  - Role: tampon de demarrage 0.25 s post-REC Looper avant disponibilite des pages SD engagees.

### `Src/Core/engine_tasklet.c` (cadence non-RTOS rattachee Z0)
- `volatile uint32_t engine_tick_count`:
  - Ecriture: `engine_tick()` dans `engine_tasklet_poll`.
  - Lecture: `main()` (cadence UI), `seq_runtime`, recorder transport, autres modules.
  - Role: horloge logique derivee audio cote superloop.
- `static volatile uint32_t engine_frames_accum`:
  - Ecriture: IRQ audio via `engine_tasklet_notify_frames`.
  - Lecture/ecriture: superloop via `engine_tasklet_poll`.
  - Role: passerelle IRQ->main loop pour ticks.
- `engine_frames_per_tick` (32), `engine_last_poll_ms`:
  - Role: quantification et dt_ms des ticks.
- `ENGINE_TASKLET_MAX_TICKS_PER_POLL = 8U`:
  - Role: borne dure du nombre de ticks engine consommes par passage superloop.

### `Src/App/Hall/hall_loop.c` (service Hall superloop Premium et finalisation commune)
- `HALL_LOOP_MAX_SAMPLES_PER_POLL = 32U`:
  - Role Premium: borne dure du nombre de samples Hall bruts consommes par passage superloop.
  - Role Low-Cost: aucun depilement; `hall_engine_process_sample()` est deja execute par l'IRQ ADC DMA et `hall_engine_process()` reste le hook commun actuellement vide.

### `Src/UI/ui_tasklet.c` (cadence UI rattachee Z0)
- `g_ui_tasklet_init`:
  - Ecriture: premier `ui_tasklet_poll()`.
  - Lecture: `ui_tasklet_poll`, `ui_tasklet_is_initialized`.
  - Role: init lazy display+ui_core et gate des services render/flush.
- Au premier `ui_tasklet_poll()`, `drv_display_init()` initialise le SSD1309 display OFF, vide le framebuffer puis la RAM controleur en full-screen synchrone, et active le display seulement apres ce clear complet. Le premier flush DMA normal vient ensuite du renderer OLED.

## 6. Flux runtime

1. Reset / entry:
- CPU entre dans `main()`.

2. Init platforme MCU/HAL:
- Verification bornes region DMA D2.
- `MPU_Config()` (region non-cacheable DMA), activation I/D cache.
- `HAL_Init()`, clocks system/periph.
- Initialisation peripheriques `MX_*` (GPIO, DMA, USART, FMC, SDMMC, SPI, I2C, ADC, SAI, TIM...).

3. Lancement services bas niveau:
- Start timers dans `main()`:
  - TIM5 base + OC ch1,
  - TIM12 base IT,
  - FATFS init.

4. Init sous-systemes projet:
- `brick6_app_init()`:
  - SDRAM, USB device, codec,
  - mixer/fx policy,
  - audio float tracks,
  - gate SD,
  - chargement de la photo catalogue Samples persistante si le fichier catalogue existe,
  - initialisation de l'`audio_recorder` commun et de sa recovery `.REC`,
  - synths/hall bridge,
  - runtime audio + wiring callback DSP,
  - engine_tasklet/param defaults,
  - seq runtime,
  - pattern/project/boot-context/undo,
  - control events + hall loop,
  - politique UI boot explicite:
    - calibration hall invalide/absente -> page `CALIBRATION`,
    - calibration hall valide -> page `CFG` (`UI_PAGE_TEMPLATE_CFG`),
  - demarrage de `ui_boot_loading`: le restore du dernier projet est differe jusqu'a une premiere frame OLED de loading,
  - start audio,
  - delay 200 ms,
  - reset peak CPU,
  - init MIDI.

5. Runtime continu superloop:
- Dans `main while(1)` ordre observe:
  1) `brick6_app_process()`
  2) `MX_USB_HOST_Process()`
  3) `midi_host_poll_bounded(8)`
  4) si tick engine avance: `ui_tasklet_poll()`
  5) si UI init: `ui_renderer_oled_service_poll()` puis `display_flush_service_poll()`.

6. Runtime continu `brick6_app_process()` ordre observe:
- `engine_tasklet_poll()`
- `seq_runtime_time_adapter_process()`
- `brick6_sampler_runtime_queue_stream_pages()`
- `sample_cache_service(32768U)`
- `audio_recorder_service()`
- `pattern_load_service(4096U)`
- `pattern_live_service()`
- `sd_preview_process()`
- `brick6_master_control_process()`
- `ui_boot_loading_service()` apres la premiere frame loading: restore du boot context puis attente de `project_v1_get_autoload_progress()`.
- pendant le boot loading, le budget `multi_sample_service_load` est reduit pour privilegier des passages UI/OLED plus reguliers; hors loading le budget normal est conserve.
- si boot loading actif: `hall_loop_process()` seulement, sans navigation UI normale; sur Low-Cost l'acquisition et l'automate Hall continuent neanmoins dans l'IRQ.
- sinon: `hall_loop_process()`, `ui_core_service_track_selection_inputs()`, `hall_keyboard_bridge_process()`; sur Low-Cost le premier appel ne depile plus de samples, tandis que le bridge consomme les transitions produites en IRQ.
- `voice_manager_service()`
- `midi_poll()`

7. Points critiques periodiques hors superloop direct:
- IRQ TIM12 appele en parallele de la superloop (adaptateur temps seq depuis IRQ).
- IRQ TIM5 OC pour clock MIDI.
- IRQ audio (Z1) alimente `engine_tasklet_notify_frames`; superloop consomme ensuite.
- IRQ audio (Z1) porte aussi la progression step du sequencer (interne + consommation pulses externes) en domaine sample.
- IRQ DMA ADC Hall Low-Cost porte le traitement position/velocite et publie uniquement des drapeaux pending; le bridge clavier reste en superloop.
- `PendSV` sert de contexte differe basse priorite pour demarrer le TX USB MIDI quand des messages sont enfiles depuis ISR.

## 7. Contraintes RT/CPU/memoire

- Pas de RTOS observe: ordonnanceur cooperatif superloop + IRQ HAL.
- Separation hard-RT audio vs services non-IRQ:
  - audio IRQ traite pipeline hard-RT,
  - Z0 orchestre services bornes/bounded hors IRQ (USB host poll bounded, services SD hors IRQ).
- Dependance forte a l'ordre d'init et au hardware clock/timer configure.
- `engine_tasklet_poll()` utilise section critique IRQ courte pour transfert frames->ticks et applique un cap fixe permanent de ticks par appel.
- Premium: `hall_loop_process()` applique un cap fixe permanent de samples FIFO Hall par appel. Low-Cost: aucun backlog de traitement Hall en superloop, chaque mesure acceptee est traitee dans l'IRQ ADC DMA.
- Buffers globaux statiques Looper/writer (`g_record_rings`, `g_looper_preroll_pcm`), pas de malloc dans Z0 observe.

## 8. Invariants a ne pas casser

- Point d'entree boot unique: `main()`.
- Ordre impose:
  - MPU/cache/HAL/clocks avant peripheriques,
  - peripheriques avant `brick6_app_init`,
  - callback DSP enregistre avant `audio_start`.
- Absence de RTOS: cadence basee sur superloop + IRQ.
- Separation init vs runtime continu:
  - init dans `main` + `brick6_app_init`,
  - services periodiques dans `main while` + `brick6_app_process`.
- Cadence UI alignee sur `engine_tick_count` (pas sur boucle brute).
- Cadence seq split stricte:
  - interne: drive step en domaine audio bloc (Z1),
  - externe: pulses MIDI recueillies hors IRQ audio puis consommees en domaine audio bloc,
  - TIM12: ticker auxiliaire interne uniquement.
- Service MIDI host autoritatif unique: `midi_host_poll_bounded(8)` appele dans `main()`; `midi_host_poll()` reste un wrapper API sans second scheduler.

## 9. Dependances inter-zones

- Vers Z1: wiring callback DSP (`audio_set_float_callback(brick6_audio_runtime_dsp)`), start audio, source de ticks via IRQ audio->engine tasklet.
- Vers Z4: init seq runtime + service superloop transport/bridge; l'avance step (interne/externe) est consommee cote Z1 audio bloc.
- Vers Z6: init pattern/project/undo, service `audio_recorder_service` et appel `pattern_live_service` en runtime.
- Vers Z5: init/tick UI via `ui_tasklet_poll`, service selection inputs dans app process.
- Vers Z3/Z2: init param defaults et effets indirects via init/runtime des autres zones.

## 10. Dette technique observee

- Centralisation elevee dans `brick6_app_init()` (wiring de nombreuses zones dans une seule fonction).
- Dependances d'ordre implicites fortes (pas de garde explicite de prerequis inter-modules).
- Service MIDI host autoritatif unique en superloop `main()`:
  - `midi_host_poll_bounded(8)` appele une fois par boucle,
  - `midi_host_poll()` reste un wrapper API de `midi_host_poll_bounded(8)` (budget effectif inchange).
- `MX_USB_HOST_Process()` appelle directement `USBH_Process(&hUsbHostHS)`: pendant enumeration/connexion, la pile ST contient des `USBH_Delay(200/100/10/2 ms)` via `HAL_Delay`, donc le service peut bloquer ponctuellement la superloop.
- `usb_host_tasklet_poll_bounded(4)` reste disponible comme API mais n'est plus appele par la boucle principale: il etait redondant avec `MX_USB_HOST_Process()` et pouvait enchainer plusieurs `USBH_Process()` bloquants dans un meme tour (ex. 200 ms + 100 ms + transitions), aggravant le freeze UI sans borner le cout interne d'un appel.
- `midi_host_poll_bounded(8)` borne le nombre de paquets USB-MIDI sortis de la queue host par passage; chaque message peut encore appeler le dispatch MIDI interne et le miroir `midi_send_raw(MIDI_DEST_USB, ...)`, donc le cout par message depend du chemin MIDI interne/USB device.
- `midi_poll()` cote device traite au plus `MIDI_USB_MAX_BURST` paquets RX et tente au plus un flush TX batch jusqu'a `MIDI_USB_MAX_BURST` paquets; il est borne par compteur mais son cout par message depend du dispatch MIDI interne et de l'etat USB device.
- Priorites IRQ actuelles observees dans le code:
  - USB Host `OTG_HS_IRQn`: 7.
  - USB Device `OTG_FS_IRQn`: 6.
  - Audio SAI2 et DMA1 Stream3/4: 1.
  - TIM5/TIM12: 5, TIM7 encodeurs: 6, PendSV MIDI USB TX differe: 15.
- Consequence audit historique: un clavier USB Host branche peut generer une activite HCD continue (bulk IN arme puis etats URB/NAK/not-ready selon le peripherique). L'ancien placement `OTG_HS_IRQn` a 0 pouvait preempter l'IRQ audio; le placement courant met SAI2/DMA a 1 et OTG_HS a 7.
- Contrat split seq mis a jour:
  - progression step interne en domaine audio bloc (deterministe sample),
  - progression step externe consommee en domaine audio bloc a partir de pulses MIDI pending,
  - TIM12 conserve un role de ticker auxiliaire.
- API `brick6_app_get_stats()` retiree de `brick6_app_init.h` (reliquat sans call site ni implementation).
- Reliquat supprime et contrat fige: aucune API stats Z0 exposee tant qu'une autorite de metriques n'est pas definie.
- Recorder legacy `live_recorder` / `recorder_transport` retire de l'init et de la superloop: il ne reste pas d'autorite capture/playback historique en Z0.
- Melange plateforme et logique produit dans `main.c`/`brick6_app_init.c` (timers HAL, USB host loop, UI cadence, orchestration metier).

## 11. Impact eventuel sur la cartographie globale

- Z0 est confirme comme zone d'orchestration (boot + wiring + cadence non-RTOS), pas zone de logique metier.
- Frontiere Z0/Z1 reste nette: Z0 demarre et cadence, Z1 execute hard-RT audio IRQ.
- Z0 met en evidence une sous-frontiere "cadence" (engine tasklet + timers TIM12/TIM5 + superloop gating UI) utile pour maintenance future.

## 12. Addendum - ordre de service SD recording produit

- Z0 cadence `audio_recorder_service()`, le streamer, le Looper et les services fichiers; `sd_scheduler` reste l'autorite d'arbitrage READ/WRITE/FILESYSTEM.
- Audio Rec et Looper partagent le meme recorder et produisent directement `.REC` puis `.WAV`; aucun export RAW intermediaire n'existe.
- Les operations project, preview, import et caches opportunistes sont refusees ou differees pendant une capture active ou une fenetre streaming protegee.
- Le contrat complet, les tails, limites memoire et invariants sont documentes dans `docs/architecture/recorder_sd.md`.

## 13. Addendum - Audio Rec / Rec Edit skeleton

- Z0 cadence le meme writer SD global pour `SAMPLE_WAV`: aucun service FatFs supplementaire ni second writer n'est ajoute.
- `sample_capture_model_service()` est appele cote UI/superloop; il orchestre arm/start/stop/finalisation sans toucher au chemin IRQ.
- `pattern_load_service()` reste cadence pendant un record `SAMPLE_WAV`; les operations project globales restent refusees via les guards writer existants.

## Addendum - Waveform cache persistant

- Z0 initialise `waveform_cache` apres `sd_access_gate_init()` et tente seulement de creer/verifier `0:/BRICK` puis `0:/BRICK/.wavecache` au boot.
- Aucun sample n'est scanne au boot et aucun `.brkwave` n'est genere au boot; un echec SD/mkdir reste non bloquant.
- La superloop cadence `waveform_cache_service()` hors IRQ, apres les services sample/writer/refill/pattern prioritaires et avant la preview opportuniste.
- Apres une finalisation Audio Rec, les services SD Rec Edit et `.wavecache` sont explicitement differes de deux passes pour laisser `TAKE_READY -> Rec Edit` initialiser le modele et produire le premier rendu sans FatFs.

## Addendum 2026-07-17 - couche Board premium

- La selection variante CMake compile les sources premium depuis `Board/Premium` et expose les contrats publics `Inc/Board/*`; le driver codec TLV320 commun est partage, tandis que le reste des sources de carte reste propre a la variante.
- La pile USB Cube premium vit maintenant dans `Board/Premium/UsbStack`; le commun passe uniquement par `board_usb_*` pour l'init/process de role expose.
- `brick6_app_init.c` ne reference pas directement le codec, SAI ni USB CubeMX pour l'init produit: le codec, le demarrage audio physique, USB Device et le delai power passent par `board_audio_*`, `board_usb_*` et `board_power_*`.
- Le CubeMX premium et ses handles restent sous `Board/Premium/Generated`; les appels `MX_*_Init()` generes ne sont pas wrappers ni modifies.

## Addendum 2026-07-17 - mapping controles low-cost

- La frontiere Board porte maintenant le mapping physique explicite des shift-registers par variante via `board_controls_button_logical_for_physical()`: premium conserve son mapping 24 positions existant, low-cost expose 32 positions SR.
- Le backend Hall commun separe le nombre de lanes UI Hall (`HALL_UI_LANE_COUNT=16`) du nombre de touches Hall clavier (`HALL_KEY_COUNT=24`) afin de garder le pipeline Z5 commun tout en autorisant le clavier low-cost 24 touches.

## Addendum 2026-07-23 - SDMMC1 low-cost

- La variante low-cost bloque de nouveau dans `Error_Handler()` si `HAL_SD_Init(&hsd1)` echoue pendant `MX_SDMMC1_SD_Init()`; aucun echec SD n'est masque au boot.
- Le pinout SDMMC1 low-cost attendu est `PC8=D0`, `PC9=D1`, `PC10=D2`, `PC11=D3`, `PC12=CK`, `PD2=CMD`, sans Card Detect utilisable; `BSP_SD_IsDetected()` considere donc la carte presente.
- CMD et D0..D3 sont configures en AF12 avec pull-up interne; CK reste AF12 sans pull-up. La vitesse GPIO reste `GPIO_SPEED_FREQ_VERY_HIGH`.
- L'identification HAL demarre en bus 1 bit et a 400 kHz derive de l'horloge SDMMC; apres identification, la configuration low-cost demande le bus 4 bits et `ClockDiv=5` sur SDMMCCLK 240 MHz, soit 24 MHz cible en transfert.
- Les diagnostics GDB low-cost sont portes par `g_sd_init_diag`: stage, derniere commande, dernier retour SDMMC/HAL, `STA`, `RESP1`, `CLKCR`, `POWER`, compteur CMD55 et compteur ACMD41. Premium conserve son chemin SDMMC genere.

## Addendum 2026-07-23 - cadence low-cost et initialisation OLED

- Le SAI1 low-cost active explicitement ses deux slots stereo TX/RX (`SlotActive=0x00000003`). Avec zero slot actif, le DMA RX ne produisait pas les callbacks qui alimentent `engine_tasklet_notify_frames()`.
- L'initialisation lazy de l'OLED reste commune et autoritative dans le premier `ui_tasklet_poll()`. En fonctionnement normal, les callbacks audio font avancer `engine_tick_count`. Si le bootstrap audio termine en `AUDIO_INIT_ERROR`, `engine_tasklet_poll()` produit un tick de secours borne par `HAL_GetTick()` afin que l'OLED puisse afficher l'erreur explicite.
- La chaine nominale reste: callbacks DMA RX SAI1 -> `engine_tasklet_notify_frames()` -> `engine_tasklet_poll()` -> `engine_tick_count` -> `ui_tasklet_poll()` -> `drv_display_init()`, puis renderer et service de flush SPI5/DMA. Le tick de secours n'est actif qu'en erreur audio finale.

## Addendum 2026-07-28 - boot calibration Hall low-cost

- Le bypass temporaire low-cost de calibration Hall est retire de `brick6_app_init.c`.
- Une calibration Hall chargee et valide ouvre `CFG`; une calibration absente ou invalide ouvre automatiquement `CALIBRATION`.
- La calibration low-cost n'est appliquee au moteur et sauvegardee qu'apres validation commune des 24 touches.

## Addendum 2026-07-23 - bootstrap audio low-cost

- Le codec low-cost est le `TLV320AIC3204` sur `I2C1`, adresse 7 bits `0x18`; son reset de boot est logiciel. Le reset codec diagnostique est abandonné: le scénario matériel courant compare un boot BAD avant/après restart SAI1/DMA seul, sans toucher au codec ni au clock tree (`docs/debug/lowcost_audio_boot_good_bad.md`).
- `SAI1_A` fournit le TX maitre a 48 kHz avec MCLK 12,288 MHz, trames stereo de 64 bits et deux slots actifs de 32 bits transportant chacun 24 bits utiles.
- Le DMA TX low-cost reste circulaire en mots 32 bits. Les deux slots doivent rester explicitement actifs dans CubeMX et dans le code genere.
- Tant que le potentiometre low-cost n'est pas monte, `PB1` ne participe ni a la sequence ADC Hall ni au traitement runtime; le controle master conserve son gain fixe temporaire. Le chemin premium reste inchange.

## Addendum 2026-07-23 - entree bootloader systeme low-cost

- Sur la variante low-cost uniquement, `Board/LowCost/Generated/Src/main.c` surveille `SHIFT + STEP16` en superloop apres `brick6_app_process()`.
- Si le combo reste maintenu au moins 2 s, `PB8` (`BOOTLOADER_TRIGGER`) est reconfigure en sortie push-pull, force a 1 pendant environ 10 ms, puis `NVIC_SystemReset()` est appele pour entrer dans le bootloader systeme STM32 via BOOT0.
- Le flag local `LOWCOST_BOOTLOADER_SHIFT_STEP16_ENABLE` permet de desactiver cette fonction sans toucher aux mappings UI premium.

## Addendum 2026-07-23 - presets CMake par variante

- `CMakePresets.json` fixe explicitement `BRICK6_VARIANT` dans chaque preset public.
- Le preset configure/build `Debug` est reserve a la variante `lowcost`, utilise `CMAKE_BUILD_TYPE=Debug`, genere dans `build/Debug` et conserve les symboles GDB avec `-Og -g3`.
- Le preset configure/build `Release` est reserve a la variante `lowcost`, utilise `CMAKE_BUILD_TYPE=Release` et genere dans `build/Release`.
- Le preset configure/build `Premium` est reserve a la variante `premium` et genere dans `build/Premium`.
- Aucun preset public generique ne doit selectionner implicitement une variante; seul le preset cache commun reste factorise et non invocable directement.

## Addendum 2026-07-25 - init runtime Stack

- `brick6_app_init()` initialise maintenant `brick6_stack_runtime_init()` apres le runtime Prism/Braids et avant `brick6_audio_runtime_init()`.
- Stack reste un runtime Synth separe; cet init ne modifie pas l'ordre ni la semantique de `brick6_braids_runtime_init()` pour Prism.

## Addendum 2026-07-25 - temporisation boot Power low-cost

- La vraie temporisation d'allumage low-cost est `POWER_BOOT_PRESS_MS` dans `Board/LowCost/Src/board_power_lowcost.c`.
- `board_power_hold_enable_after_boot_press()` maintient `POWER_HOLD` bas tant que `POWER_BUTTON_SENSE` n'a pas ete vu haut en continu pendant 1000 ms, puis met `POWER_HOLD` haut et ne gere plus aucun chemin d'extinction runtime.
- Aucun watchdog, auto-off firmware ou demande de shutdown n'est active dans cette zone.
