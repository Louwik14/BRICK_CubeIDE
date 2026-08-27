# Z0 - Plateforme, memoire, cadence et IPC

## Execution

L'audio travaille par demi-buffer de 64 frames a 48 kHz. L'IRQ SAI possede la timeline audio, publie les reveils monotones et n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. Apres chaque demi-buffer, elle publie le prochain `first_renderable_sample` et arme PendSV; cette continuation CONTROL est servie avant le retour en superloop et publie l'horizon musical suivant. Scheduler, lifecycle et Note FX contribuent d'abord a une fenetre CONTROL fixe; ses 64 buckets sample/kind finalisent ensuite la FIFO en ordre chronologique, avec STOP avant START a timestamp egal. Cette fenetre transitoire n'est ni une seconde FIFO, ni un chemin de recovery. La superloop orchestre les autres tasklets moteur, Storage et UI.

Hall Low-Cost et Premium executent la meme machine bornee depuis l'acquisition ADC. TIM5 est le compteur libre commun de capture. Le producteur ne lit jamais la timeline audio; AUDIO publie un ancrage coherent `{tim5_tick, first_renderable_sample}`.

## Frontiere CONTROL/AUDIO

La frontiere suit `M4 CONTROL decide -> payload fixe IPC -> M7 AUDIO execute`. Aucun pointeur, callback ou contexte mutable ne la traverse.

- musical interne: ring SPSC de 257 slots physiques, 256 utilisables, reserve aux actions finales `START/STOP/RETRIGGER`; la borne GROUP reste `7 * (2 * 8 + 7) + 8 * (2 * 1 + 7) = 233`, expirations naturelles et fermetures CONTROL comprises, soit une marge de 23 actions. Une fermeture consomme le ledger de l'output et rejoint les memes buckets; elle n'ajoute donc pas de fan-out au pire cas;
- musical externe Hall/MIDI: ingress SPSC distinct de 129 slots physiques afin qu'une saturation externe ne consomme jamais la garantie SEQ;
- parametres: ring general `PARAM_SET` et transport date compact dedie de 1024 ecritures finales;
- panic: publication generationnelle etroite, idempotente, independante des rings musicaux; AUDIO purge uniquement les actions anterieures a la generation puis ferme physiquement;
- binding/routing/configuration: commandes et snapshots versionnes, jamais la FIFO musicale;
- restore: plan immutable et completion dedies.

Les deux ingress musicaux sont fusionnes par AUDIO selon `(timestamp, STOP avant START)` sans melanger leurs capacites. Ils ne stockent que des actions resolues dans l'horizon publiable et ne portent ni decision, ni recovery, ni retry.

Sur H743, les objets IPC resident dans la moitie haute de SRAM4 `0x38008000..0x3800FFFF`, shareable et non-cacheable; les registres Stream fixes resident dans la fenetre IPC partagee SRAM3/D2, et la projection complete du Recorder dans la zone SDRAM partagee non-cacheable. `DMB` ordonne la publication mais ne remplace pas le protocole d'ownership. Les payloads SDRAM cacheables exigent clean producteur puis invalidate consommateur. La zone Recorder de 256 KiB est shareable non-cacheable; les buffers DMA SAI sont en D2 non-cacheable.

Les principaux sens sont:

```text
CONTROL -> AUDIO : actions musicales finales, parametres dates, panic, routing, binding, MOD, restore
AUDIO -> CONTROL : clock, cadence, ACK via ring partage, telemetrie, capture Recorder
Storage <-> AUDIO : registration, token, completion de page et payloads bornes
```

Preview est un ring PCM SPSC M4->M7. Recorder publie un ring append-only M7->M4. Le Looper AUDIO date son DSP avec le sample clock publie par AUDIO, jamais avec la timeline mutable du sequenceur CONTROL. Looper et Audio FX exposent seulement des statuts etroits. Aucun consommateur ne relit la structure interne de l'autre domaine.

Au boot, `track_state` est initialise avant la projection finale `track_runtime`; le bridge Hall/keyboard et son focus sont ensuite initialises et synchronises depuis cette autorite canonique. PLAY/PAUSE ou une reconfiguration moteur ne font pas partie du protocole d'activation Hall.

## Memoire et migration H747

Les budgets DTCM, D1, D2, SRAM2, SRAM3, SRAM4, ITCM et SDRAM sont controles par les linkers; toute croissance d'une region proche de sa limite exige un budget explicite. Les voix et etats chauds restent en DTCM; les arenas AUDIO volumineuses resident en SDRAM selon leur contrat cache.

La migration H747 conserve les payloads et protocoles. Restent physiques: deux images CM7/CM4, boot/HSEM, clocks, linkers, MPU des deux coeurs, repartition IRQ/DMA et initialisation FMC/SDRAM unique. M7 recoit SAI/audio; M4 recoit UI/MIDI/SD/display.

## Robustesse

Boot, faults, watchdog et diagnostics doivent rester bornes et sans allocation dynamique dans les chemins critiques. Les informations de crash persistantes sont diagnostiques, jamais une seconde autorite runtime.
