# Z0 - Plateforme, memoire, cadence et IPC

## Execution

L'audio travaille par demi-buffer de 64 frames a 48 kHz. L'IRQ SAI possede la timeline audio et n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. CONTROL se cadence seul: TIM12 porte le tick musical interne, TIM5 porte le temps physique et l'unique ancre boot SAI permet sa conversion en samples. La superloop publie l'horizon musical glissant; aucun reveil AUDIO, compteur de frames periodique ou PendSV sequenceur ne traverse la frontiere. Scheduler, lifecycle et Note FX contribuent d'abord a une fenetre CONTROL fixe; ses 64 buckets sample/kind finalisent ensuite la FIFO en ordre chronologique, avec STOP avant START a timestamp egal.

Hall Low-Cost et Premium executent la meme machine bornee depuis l'acquisition ADC. TIM5 est le compteur libre commun de capture. Le producteur ne lit jamais la timeline audio; AUDIO publie un ancrage coherent `{tim5_tick, first_renderable_sample}`.

## Frontiere CONTROL/AUDIO

La frontiere suit `M4 CONTROL decide -> commande finale 16 octets -> M7 AUDIO execute`. La FIFO SPSC unique de 2048 commandes transporte exclusivement PROGRAM, PARAM, NOTE, TRANSPORT, RECORD et PANIC. Aucun pointeur, callback, contexte mutable, Pattern ou Project ne la traverse.

Les ingress Hall/MIDI et les sources scheduler restent des buffers locaux CONTROL. CONTROL resout et fusionne leur fenetre, transforme un retrigger en NOTE OFF puis NOTE ON au meme sample, puis publie un lot atomique dans la FIFO unique. AUDIO ne fusionne aucune queue et l'ordre physique FIFO est l'ordre fonctionnel a timestamp egal.

PROGRAM porte directement la structure moteur. PARAM porte les proprietes
finales et PANIC emprunte la meme FIFO; aucune generation musicale, queue
prioritaire ou plan fonctionnel de restore ne traverse la frontiere. L'etat
restore est valide puis republie par CONTROL avec le contrat final.

Sur H743, les objets IPC resident dans la moitie haute de SRAM4 `0x38008000..0x3800FFFF`, shareable et non-cacheable; les registres Stream fixes resident dans la fenetre IPC partagee SRAM3/D2, et la projection complete du Recorder dans la zone SDRAM partagee non-cacheable. `DMB` ordonne la publication mais ne remplace pas le protocole d'ownership. Les payloads SDRAM cacheables exigent clean producteur puis invalidate consommateur. La zone Recorder de 256 KiB est shareable non-cacheable; les buffers DMA SAI sont en D2 non-cacheable.

Les principaux sens sont:

```text
CONTROL -> AUDIO : PROGRAM, PARAM, NOTE, TRANSPORT, RECORD, PANIC; data planes volumineux separes
AUDIO -> CONTROL : tail FIFO, credits STREAM, PCM/framing Recorder, ancre boot, diagnostic
Storage <-> AUDIO : registration, token, completion de page et payloads bornes
```

Preview est un ring PCM SPSC M4->M7. Recorder publie un ring append-only M7->M4. Le Looper AUDIO date son DSP avec sa timeline locale. FILTER POS affiche le shadow CONTROL; aucune valeur DSP n'est une autorite UI. Le boot ne publie qu'un diagnostic physique exceptionnel, sans READY musical ni dependance nominale.

Au boot, `track_state` est initialise avant la projection finale `track_runtime`; le bridge Hall/keyboard et son focus sont ensuite initialises et synchronises depuis cette autorite canonique. PLAY/PAUSE ou une reconfiguration moteur ne font pas partie du protocole d'activation Hall.

## Memoire et migration H747

Les budgets DTCM, D1, D2, SRAM2, SRAM3, SRAM4, ITCM et SDRAM sont controles par les linkers; toute croissance d'une region proche de sa limite exige un budget explicite. Les voix et etats chauds restent en DTCM; les arenas AUDIO volumineuses resident en SDRAM selon leur contrat cache.

La migration H747 conserve les payloads et protocoles. Restent physiques: deux images CM7/CM4, boot/HSEM, clocks, linkers, MPU des deux coeurs, repartition IRQ/DMA et initialisation FMC/SDRAM unique. M7 recoit SAI/audio; M4 recoit UI/MIDI/SD/display.

## Robustesse

Boot, faults, watchdog et diagnostics doivent rester bornes et sans allocation dynamique dans les chemins critiques. Les informations de crash persistantes sont diagnostiques, jamais une seconde autorite runtime.
