# Bug audio Low-Cost dépendant du boot

## Périmètre et classement des hypothèses

Les captures CPU étant propres jusqu'au `tx_buffer` final et le cache TX ayant été
réfuté, l'investigation repart à la frontière DMA/SAI. Le reset diagnostique du
TLV320 est abandonné et n'est appelé par aucune étape du test décrit ici.

1. **Synchronisation initiale SAI1/TLV320 ou état de FIFO fixé au boot.** Le TX
   maître commence à émettre avant le reset logiciel et toute la configuration
   du codec. Le reset, la mise sous tension des blocs et l'unmute arrivent donc à
   une phase arbitraire de BCLK/LRCLK. L'expérience `SAI_DMA_RESTART_V1_TX_THEN_RX`
   la réfute si plusieurs boots BAD restent strictement BAD après restart.
2. **MCLK/BCLK/LRCLK marginal ou mal activé.** L'IOC demande MCLK, mais
   `MX_SAI1_Init()` ne renseigne pas `hsai_BlockA1.Init.MckOutput`. Sur les
   révisions STM32 où `MCKEN` est distinct de `NODIV/NOMCK`, le champ global vaut
   donc zéro. La mesure PE2 et le bit MCKEN rapporté dans `tx_cr1_before` doivent
   trancher; un MCLK absent ou différent entre GOOD/BAD réfute l'hypothèse d'un
   simple état analogique.
3. **État numérique interne du TLV320 après son reset de boot.** Les diviseurs
   sont relus, mais aucun statut de verrouillage de la source série n'est attendu.
   Les flags DAC/ADC prouvent la mise sous tension des blocs, pas l'intégrité des
   trames reçues. Un restart SAI BAD -> GOOD affaiblit cette hypothèse; BAD -> BAD
   la conserve.
4. **Séquence analogique/alimentations du TLV320.** La référence reçoit 40 ms et
   les flags DAC/output/ADC sont attendus, mais les rails et la sortie analogique
   ne sont pas observés. Des clocks DATA identiques entre GOOD/BAD avec une sortie
   analogique différente l'isolent.
5. **Donnée réellement émise par DMA différente du buffer mémoire.** Les compteurs
   et erreurs DMA sont propres, ce qui la rend moins crédible. La comparaison DATA
   physique et le restart avec buffers entièrement remis à zéro peuvent la
   réfuter.

## Chaîne d'initialisation réelle Low-Cost

| Ordre | Fichier / fonction | Action, délai et statut | Point GOOD/BAD possible |
|---:|---|---|---|
| 1 | `Board/LowCost/Generated/Src/main.c` — `SystemClock_Config()` | HSE 12 MHz; PLL1 et bus. Les retours RCC sont testés, sinon `Error_Handler()`. | Peu probable si le MCU fonctionne normalement. |
| 2 | même fichier — `PeriphCommonClock_Config()` | PLL3: M=12, N=491, FRACN=4260, P=40; kernel SAI1 calculé à 12,288000488 MHz. Retour RCC testé. | Jitter/lock physique non observé après le retour HAL. |
| 3 | même fichier — `MX_DMA_Init()`, puis `MX_SAI1_Init()` dans `Board/LowCost/Generated/Src/sai.c` | DMA1 stream 3 TX et stream 4 RX, circulaires, mots 32 bits, FIFO DMA pleine. SAI A maître TX, SAI B synchrone esclave RX, 48 kHz, 24 bits dans 2 slots de 32 bits, trame 64 BCLK. Les retours HAL DMA/SAI sont testés. Les blocs restent désactivés. | `MckOutput` n'est pas assigné malgré l'IOC; aucune purge/lecture de flags explicite après l'init. |
| 4 | `Src/Core/brick6_app_init.c` — `brick6_app_init()` | `board_audio_codec_init()` ne touche pas le codec: il efface seulement le diagnostic. Les moteurs/UI sont initialisés. | Aucun. |
| 5 | `Src/Audio/audio.c` — `audio_init()` | Les deux moitiés RX/TX sont mises à zéro; le TX est nettoyé D-cache avant DMA. | Première charge DMA déterministe (silence). |
| 6 | `Src/Audio/audio.c` — `audio_start()` puis `Board/LowCost/Src/board_audio_lowcost.c` — `board_audio_start_stream()` | Au début de chaque tentative: `HAL_SAI_DMAStop(RX)`, puis `HAL_SAI_DMAStop(TX)`, retours ignorés. HAL désactive le bloc, avorte le DMA et demande un flush FIFO. | Un échec de stop ou un flag persistant est ignoré. |
| 7 | `board_audio_start_stream()` | `HAL_SAI_Transmit_DMA(TX)` est testé. Le DMA lit le buffer de silence, remplit la FIFO, puis HAL active SAI A: MCLK/BCLK/LRCLK et les premières trames commencent ici. Délai fixe 1 ms. | Le codec n'est pas encore configuré; sa phase par rapport aux premières trames dépend du boot. |
| 8 | `Board/LowCost/Drivers/tlv320aic3204.c` — `tlv_init()` / `TLV320AIC3204_SoftwareReset()` | ACK I2C attendu jusqu'à 100 ms. Reset logiciel page 0/reg 1, délai réel 5 ms, puis bit reset relu. Tous les statuts I2C sont propagés. | Le reset est effectué pendant que les clocks et DATA silence tournent. `reset_wait_ms` annonce 2 ms alors que le délai logiciel réel vaut 5 ms: anomalie de diagnostic, pas de séquence. |
| 9 | `tlv_init()` | Référence/quick charge, puis 40 ms. Diviseurs DAC/ADC et interface 24 bits sont écrits et relus. Les écritures analogiques dites « extended » ne sont pas relues dans l'init normale, mais leurs erreurs I2C sont propagées. | Aucun statut de lock/synchronisation série n'est attendu. |
| 10 | `tlv_init()` | Sorties casque mutées; routage analogique; DAC datapath/volumes; attente flags DAC `0x88` (100 ms max); power outputs; attente `0xAA` (100 ms); attente ADC `0x44` (100 ms). | Ces flags valident power DAC/ADC/output, pas la qualité du framing DATA. |
| 11 | `tlv_init()` | HPL/HPR passent de `0x40` à `0x00`; chaque écriture est relue. Aucun délai supplémentaire. Le statut complet remonte à `board_audio_start_stream()`. | Unmute à une phase de trame arbitraire, sans nombre garanti de trames silence après le dernier état ready. |
| 12 | `board_audio_start_stream()` | Seulement après codec ready: `HAL_SAI_Receive_DMA(RX)`, retour testé. RX devient l'autorité des IRQ half/full. En cas d'échec, TX est stoppé et une seconde tentative complète est faite. | Ordre imposé TX -> codec -> RX; aucun essai RX -> TX. |
| 13 | `brick6_app_init()` | Attente 200 ms, puis `prism_debug_boot_start_test()`: 8 Prism, une note par piste, OSC1 seul, LP_BI cutoff UI 0, master 1, exactement 288000 frames (6 s). | Le seuil GOOD/BAD est déjà fixé avant les notes. |

## Anomalies précises

- Le codec est reset/configuré/unmuté après le démarrage du TX maître, sans
  synchronisation explicite sur une frontière LRCLK.
- Le champ HAL `MckOutput` n'est pas initialisé par le fichier généré alors que
  `BRICK6_LOWCOST.ioc` contient `SAI_MASTERCLOCK_ENABLE`. La conséquence dépend de
  la révision STM32; elle doit être mesurée, pas corrigée sur suspicion.
- Les retours des deux `HAL_SAI_DMAStop()` de début de tentative sont ignorés.
- Le « clocks_ok » TLV signifie que les diviseurs ont été relus; ce n'est pas un
  flag de lock ou de synchronisation avec BCLK/LRCLK.
- La valeur diagnostique `reset_wait_ms` (2 ms) ne reflète pas le délai réel de
  5 ms après reset logiciel.
- Le chemin nominal dépend d'un délai fixe de 1 ms entre la première activation
  SAI et le début de l'init codec; il n'attend ni un nombre de trames ni un flag
  de phase.

## Expérience implémentée

Identifiant: `SAI_DMA_RESTART_V1_TX_THEN_RX`.

Après un verdict initial BAD, en main loop uniquement:

1. le moteur DSP est sérialisé avec l'IRQ, sans log en IRQ;
2. les registres codec sont photographiés;
3. RX DMA/SAI est stoppé, puis TX DMA/SAI;
4. les deux FIFO SAI sont explicitement flushées et tous les flags effaçables
   sont nettoyés;
5. les deux moitiés RX/TX sont mises à zéro; RX est invalidé et TX nettoyé côté
   D-cache avant redémarrage;
6. TX redémarre, puis RX, avec les mêmes handles, registres SAI, DMA, buffers et
   clock tree; aucune fonction TLV320 et aucun `HAL_SAI_Init()` n'est appelé;
7. le scénario Prism identique repart pour 288000 frames, puis reçoit son second
   verdict manuel.

Le rapport unique contient les snapshots codec avant/après, les statuts HAL des
quatre opérations, les SR/CR1 SAI avant/purge/après, les états et erreurs SAI/DMA,
la confirmation flush/flags/buffers, puis les deux fenêtres complètes de 6 s.
Les suffixes sont `GOOD`, `BAD_THEN_SAI_RESTART_GOOD`,
`BAD_THEN_SAI_RESTART_BAD` ou `BAD_SAI_RESTART_FAILED`.

Interprétation:

- BAD -> GOOD: SAI/FIFO/framing/synchronisation initiale devient prioritaire;
- BAD -> BAD: codec interne, clocks physiques, alimentations ou analogique;
- BAD -> son différent: le trajet SAI influence le phénomène même sans guérison;
- restart HAL en échec: exploiter le rapport `BAD_SAI_RESTART_FAILED` avant tout
  nouveau test.

## Variantes suivantes, uniquement si V1 ne tranche pas

- `START_V2_CODEC_READY_BEFORE_STREAM`: rendre le codec entièrement prêt avant
  DATA DMA. Cette variante exige d'abord de démontrer comment MCLK reste présent;
  elle est réfutée si BAD persiste sur plusieurs boots.
- `START_V3_SILENCE_256F_BEFORE_UNMUTE`: garder les sorties mutées et transmettre
  au moins 256 trames silence complètes avant power/unmute. Elle cible uniquement
  l'accrochage série/soft-step.
- `START_V4_RX_ARMED_THEN_TX`: après purge et préremplissage silence, armer RX
  synchrone avant d'activer TX maître; comparer à V1 TX -> RX. Le reste est
  strictement identique.

## Protocole physique GOOD contre BAD

Utiliser le même firmware et le même scénario; capturer au moins 10 ms au boot
autour de l'activation SAI, puis 10 ms pendant les 8 notes. Utiliser une masse
courte et des sondes x10; pour DATA/BCLK, préférer une sonde active ou un analyseur
à faible capacité.

| Signal | Point STM32 | Attendu | Comparaison |
|---|---|---|---|
| BCLK/SCK | PE5 | 3,072 MHz, soit 64 clocks par frame | fréquence, duty-cycle, fronts doubles/manquants, ringing et stabilité au tout premier frame |
| LRCLK/FS | PE4 | 48 kHz, période 20,833 µs, trame 64 bits, actif bas 32 bits | phase fixe avec BCLK, largeur 32 BCLK, runt pulse au start/restart |
| DATA TX | PE6 | 2 slots de 32 bits; 24 bits utiles MSB-first par canal, silence zéro avant test | position du MSB par rapport au front FS, décalage d'un bit/slot, DATA GOOD/BAD sur une même note |
| MCLK | PE2 | 12,288 MHz (256 x Fs) si réellement activé | présence dès le TX start, amplitude, fréquence, arrêt/reprise lors de V1, différence GOOD/BAD |

Faire une acquisition numérique quatre voies BCLK+FS+DATA+MCLK avec le même seuil
sur GOOD et BAD. Décoder en 24 bits, deux slots 32 bits, MSB-first; exporter quelques
milliers de frames et vérifier que les mots DATA physiques suivent le PCM attendu.

Mesurer ensuite au codec, pas seulement au MCU, les rails AVDD, DVDD et IOVDD (ou
les noms équivalents du schéma), la référence/common-mode et la sortie casque.
Comparer minimum, maximum, ripple et temps de montée au boot avec déclenchement sur
MCLK/FS. Si les quatre signaux numériques sont identiques mais que la sortie codec
diffère, observer simultanément une sortie analogique avant charge et après charge;
cela sépare codec/rail de connectique/amplification aval.
