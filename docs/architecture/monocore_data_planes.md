# Data planes monocoeur BRICK6

BRICK6 cible le STM32H743 avec un unique Cortex-M7. Il n'existe ni transport
inter-coeur, ni image secondaire, ni protocole de coherence entre caches CPU.

## Frontieres reelles

| Data plane | Writer | Reader | Contrat |
|---|---|---|---|
| FIFO fonctionnelle | CONTROL/SEQ | AUDIO IRQ | SPSC, timestamps, `DMB`, commit de transaction |
| Sample RAM | STORAGE superloop | AUDIO IRQ | registry local, `ready`, generation, STOP et grace de 192 frames |
| Wavetable | STORAGE superloop | AUDIO IRQ | registry local, pointeurs M7, generation, STOP et grace |
| Multi | STORAGE superloop | AUDIO IRQ | registry local, `ready`, `registration_epoch`, STOP et leases |
| Classic / REC_SOURCE | STORAGE superloop | AUDIO IRQ | publication locale, cles et leases avant recyclage |
| STREAM pages | STORAGE superloop | AUDIO IRQ | etats de page, tokens I/O, leases et completions DMA |
| Preview PCM | STORAGE superloop | AUDIO IRQ | ring SPSC cacheable, `write_count/read_count` et `DMB` |
| Recorder FLOAT32 | AUDIO IRQ | STORAGE superloop | ring SPSC, `produced/released`, session et completion SD |
| Waveforms / niveau REC / diagnostic | AUDIO IRQ | CONTROL/UI | seqlocks, snapshots et `DMB` |
| USB Audio | USB IRQ / AUDIO IRQ | AUDIO IRQ / service USB | rings SPSC non-cacheables |

Les pointeurs locaux sont autorises dans les data planes M7 lorsque leur duree
de vie est protegee. Aucun pointeur ne traverse la FIFO fonctionnelle.

## Cache et DMA

Les publications CPU vers CPU utilisent la coherence du meme M7 et des
barrieres d'ordre; elles ne font aucun clean/invalidate. La maintenance cache
reste obligatoire aux seules frontieres CPU vers DMA ou DMA vers CPU. Les
rings hardware-proven et diagnostics explicitement places en memoire
non-cacheable conservent ce placement.

## Retrait

Une ressource n'est recyclee qu'apres fermeture de l'ingress, STOP AUDIO et
preuve d'extinction des lecteurs: grace bornee pour RAM/Wavetable, leases pour
Multi/Classic/REC_SOURCE, ou consumer tail pour les rings. Les generations,
epochs et tokens restants identifient des ressources ou operations I/O; ils ne
sont pas des handshakes de processeurs.
