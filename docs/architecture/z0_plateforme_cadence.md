# Z0 - Plateforme, memoire, cadence et IPC

## Execution

L'audio travaille par demi-buffer de 64 frames a 48 kHz. L'IRQ SAI possede la timeline audio, publie les reveils monotones et n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. La superloop orchestre les tasklets moteur, Storage et UI avec rattrapage borne.

Hall Low-Cost et Premium executent la meme machine bornee depuis l'acquisition ADC. TIM5 est le compteur libre commun de capture. Le producteur ne lit jamais la timeline audio; AUDIO publie un ancrage coherent `{tim5_tick, first_renderable_sample}`.

## Frontiere CONTROL/AUDIO

Les commandes et occurrences utilisent des rings SPSC; les projections utilisent des snapshots versionnes ou mailboxes latest-wins; la telemetrie utilise des compteurs monotones ou snapshots. Aucun pointeur, callback ou contexte mutable ne traverse la frontiere.

Sur H743, les objets IPC resident dans la moitie haute de SRAM4 `0x38008000..0x3800FFFF`, shareable et non-cacheable. `DMB` ordonne la publication mais ne remplace pas le protocole d'ownership. Les payloads SDRAM cacheables exigent clean producteur puis invalidate consommateur. La zone Recorder de 256 KiB est shareable non-cacheable; les buffers DMA SAI sont en D2 non-cacheable.

Les principaux sens sont:

```text
CONTROL -> AUDIO : notes, transitions, parametres, routing, binding, MOD, restore
AUDIO -> CONTROL : clock, cadence, ACK, telemetrie, capture Recorder
Storage <-> AUDIO : registration, token, completion et payloads bornes
```

Preview est un ring PCM SPSC M4->M7. Recorder publie un ring append-only M7->M4. Looper et Audio FX exposent seulement des statuts etroits. Aucun consommateur ne relit la structure interne de l'autre domaine.

## Memoire et migration H747

Les budgets DTCM, D1, D2, SRAM2, SRAM3, SRAM4, ITCM et SDRAM sont controles par les linkers; toute croissance d'une region proche de sa limite exige un budget explicite. Les voix et etats chauds restent en DTCM; les arenas AUDIO volumineuses resident en SDRAM selon leur contrat cache.

La migration H747 conserve les payloads et protocoles. Restent physiques: deux images CM7/CM4, boot/HSEM, clocks, linkers, MPU des deux coeurs, repartition IRQ/DMA et initialisation FMC/SDRAM unique. M7 recoit SAI/audio; M4 recoit UI/MIDI/SD/display.

## Robustesse

Boot, faults, watchdog et diagnostics doivent rester bornes et sans allocation dynamique dans les chemins critiques. Les informations de crash persistantes sont diagnostiques, jamais une seconde autorite runtime.
