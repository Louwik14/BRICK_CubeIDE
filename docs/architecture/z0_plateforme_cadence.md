# Z0 - Plateforme, memoire, cadence et frontieres

## Execution H743

BRICK6 utilise une image unique sur le Cortex-M7 du STM32H743, sans RTOS.

```text
AUDIO IRQ @ 48 kHz, demi-buffer 64 frames
SEQ IRQ TIM4 @ 750 Hz
CONTROL superloop
STORAGE superloop
USB et DMA par IRQ peripheriques
```

AUDIO priorite 1 preempte SEQ priorite 2, qui preempte la superloop. AUDIO
n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. TIM5 a
1 MHz est l'horloge media canonique; le M7 possede son extension 64 bits et son
IRQ d'overflow.

## Frontieres concurrentes

- CONTROL/SEQ vers AUDIO: FIFO SPSC datee et `AUDIO_STATE_COMMIT`.
- STORAGE vers AUDIO: registries locaux, rings, pages et publications `ready`.
- AUDIO vers CONTROL/STORAGE: seqlocks, leases et compteurs SPSC.
- USB IRQ vers AUDIO et retour: deux rings FLOAT32 SPSC.
- CPU vers DMA et DMA vers CPU: maintenance cache explicite dans le driver
  proprietaire du transfert.

Toutes ces frontieres sont locales au meme M7. `DMB` ordonne les publications;
les seqlocks et compteurs SPSC restent necessaires face aux interruptions.

## Memoire

- DTCM/D1: etats AUDIO chauds et buffers de calcul bornes.
- D2 cacheable: etats locaux M7 partages entre IRQ et services cooperatifs.
- SRAM3 et moitie haute de SRAM4 non-cacheables: objets `IRQ_SHARED_D2/D3`
  dont le placement deterministe est conserve pour Recorder trace, SEQ et USB.
- SDRAM cacheable: payloads Sample, registries locaux, Preview et Recorder ring.
- fenetre SDRAM Recorder non-cacheable: scratchs explicitement places qui
  dependent encore de ce contrat physique.
- D2 DMA non-cacheable: buffers peripheriques hardware-proven.

Les sections linker et regions MPU expriment les proprietes cache/DMA, pas une
topologie de processeurs. Aucun clean/invalidate CPU vers CPU n'est permis.

## Ownership

```text
CONTROL  produit, UI, parametres et configuration
SEQ      scheduling musical
AUDIO    DSP, voix et mixer
STORAGE  SD, loaders, page-cache et writer Recorder
Platform hardware, DMA, IRQ, cache et MPU
```

Le build classe chaque unite dans `DOMAIN_CONTROL`, `DOMAIN_STORAGE`,
`DOMAIN_AUDIO`, `DOMAIN_CONTRACTS` ou `PLATFORM_H743`. Les definitions physiques
resident dans le domaine qui possede leur lifecycle; aucun domaine
`SHARED_BACKING` n'existe.

Le firmware est compile une seule fois, pour le Cortex-M7. Le target
`domain_dependency_check` reutilise les dependances enregistrees par Ninja lors
de ce build normal pour appliquer les firewalls CONTROL, STORAGE, AUDIO et
CONTRACTS. Il ne compile ni ne lie aucune image ou aucun objet secondaire.

## USB

TinyUSB fonctionne sans RTOS. L'IRQ OTG FS publie Audio OUT dans le ring PC
vers BRICK; AUDIO consomme ce ring. AUDIO publie le ring BRICK vers PC, consomme
par le service USB cooperatif. Les rings restent non-cacheables car ils relient
deux IRQ/services a forte cadence; leur SPSC et leurs `DMB` sont conserves.

## Port RT1172

Le port conserve owners, priorites, SPSC, seqlocks, leases, generations de
ressource, completions DMA et horloge media. Il doit fournir des placements RAM
et attributs cache equivalents, sans transporter une architecture de second
CPU inexistante.
