# Z0 - Plateforme, memoire, cadence et IPC

## Execution

L'audio travaille par demi-buffer de 64 frames a 48 kHz. L'IRQ SAI possede sa timeline audio locale et n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. CONTROL se cadence seul: TIM12 porte le tick musical interne, TIM5, demarre avant les domaines, porte le temps physique commun et sa conversion nominale en samples. La superloop publie l'horizon musical glissant; aucun reveil AUDIO, compteur de frames periodique ou PendSV sequenceur ne traverse la frontiere. Scheduler, lifecycle et Note FX contribuent d'abord a une fenetre CONTROL fixe; ses 64 buckets sample/kind finalisent ensuite la FIFO en ordre chronologique, avec STOP avant START a timestamp egal.

Hall Low-Cost et Premium executent la meme machine bornee depuis l'acquisition ADC. TIM5 est le compteur libre commun de capture. CONTROL en possede l'extension et la conversion; AUDIO initialise sa sample clock locale depuis TIM5 au premier callback valide et ne publie aucune ancre.

## Frontiere CONTROL/AUDIO

La frontiere suit `M4 CONTROL decide -> commande finale 16 octets -> M7 AUDIO execute`. La FIFO SPSC unique de 2048 commandes transporte PROGRAM, PARAM, NOTE, TRANSPORT, RECORD et PANIC. Les requetes visuelles typees AUDIO waveform et synth waveform empruntent egalement PARAM dans cette FIFO; elles n'ont ni mailbox ni file secondaire. Aucun pointeur, callback, contexte mutable, Pattern ou Project ne la traverse.

Les ingress Hall/MIDI et les sources scheduler restent des buffers locaux CONTROL. CONTROL resout et fusionne leur fenetre, transforme un retrigger en NOTE OFF puis NOTE ON au meme sample, puis publie un lot atomique dans la FIFO unique. AUDIO ne fusionne aucune queue et l'ordre physique FIFO est l'ordre fonctionnel a timestamp egal.

La frontiere physique de plateforme est regroupee dans `Inc/Platform` et
`Src/Platform`. Les types, layouts et `extern` purs appartiennent a
`DOMAIN_CONTRACTS`; leurs definitions physiques appartiennent au groupe
`SHARED_BACKING`. Les publishers et queues locales appartiennent explicitement
a `DOMAIN_CONTROL`, jamais a `PLATFORM_H743`. Les writers CONTROL, readers AUDIO,
publishers AUDIO et readers CONTROL sont des unites distinctes dans leur domaine proprietaire. Les
fichiers de metier CONTROL, les runtimes et DSP AUDIO, ainsi que les pools
Storage/Sampler, restent dans leurs domaines; `live_parameter_audio_runtime`
reste dans `Inc/Audio` et `Src/Audio` et n'est pas une projection IPC.

PROGRAM porte directement la structure moteur. PARAM porte les proprietes
finales et PANIC emprunte la meme FIFO; aucune generation musicale, queue
prioritaire ou plan fonctionnel de restore ne traverse la frontiere. L'etat
restore est valide puis republie par CONTROL avec le contrat final.

Sur H743, les objets IPC resident dans la moitie haute de SRAM4 `0x38008000..0x3800FFFF`, shareable et non-cacheable; les registres Stream fixes resident dans la fenetre IPC partagee SRAM3/D2, et la projection complete du Recorder dans la zone SDRAM partagee non-cacheable. `DMB` ordonne la publication mais ne remplace pas le protocole d'ownership. Les payloads SDRAM cacheables exigent clean producteur puis invalidate consommateur. La zone Recorder de 256 KiB est shareable non-cacheable; les buffers DMA SAI sont en D2 non-cacheable.

Les principaux sens sont:

```text
CONTROL -> AUDIO : FIFO unique PROGRAM, PARAM, NOTE, TRANSPORT, RECORD, PANIC et requetes visuelles typees; data planes volumineux separes
AUDIO -> CONTROL : niveau REC, waveforms audio/synth et diagnostic Audio; plus les retours physiques STREAM/Recorder hors IPC fonctionnel
Storage <-> AUDIO : registration, token, completion de page et payloads bornes
```

Preview est un ring PCM SPSC M4->M7: CONTROL possede payload/`write_count`, AUDIO `read_count` et le gain/active local applique par PARAM. Recorder est le ring inverse: AUDIO possede payload/`head_cursor`/fermeture/fault, CONTROL uniquement `tail_cursor`, writer et erreurs SD. Le Looper AUDIO date son DSP avec sa timeline locale. Le transport et le REC bus sont des runtimes AUDIO locaux alimentes par TRANSPORT/PARAM; aucun snapshot parallele n'en revient. FILTER POS affiche la valeur CONTROL canonique; aucune valeur DSP n'est une autorite UI.

Au boot, `track_state` est initialise avant la projection finale `track_runtime`; le bridge Hall/keyboard et son focus sont ensuite initialises et synchronises depuis cette autorite canonique. PLAY/PAUSE ou une reconfiguration moteur ne font pas partie du protocole d'activation Hall.

## Memoire et migration H747

Les budgets DTCM, D1, D2, SRAM2, SRAM3, SRAM4, ITCM et SDRAM sont controles par les linkers; toute croissance d'une region proche de sa limite exige un budget explicite. Les voix et etats chauds restent en DTCM; les arenas AUDIO volumineuses resident en SDRAM selon leur contrat cache.

La migration H747 conserve les payloads et protocoles. Restent physiques: deux images CM7/CM4, boot/HSEM, clocks, linkers, MPU des deux coeurs, repartition IRQ/DMA et initialisation FMC/SDRAM unique. M7 recoit SAI/audio; M4 recoit UI/MIDI/SD/display.

## Ownership de build prepare pour H747

Le build H743 classe chaque unite dans un seul ensemble: `DOMAIN_CONTROL`,
`DOMAIN_AUDIO`, `DOMAIN_CONTRACTS`, `SHARED_BACKING` ou `PLATFORM_H743`. Il n'existe plus de
domaine de transition mixte. UI, sequenceur, Storage et etat Param canonique
appartiennent a CONTROL; DSP, projection Param appliquee, ENV3 et caches/plans
de modulation appartiennent a AUDIO. Les catalogues immuables partages
(modeles moteur/FX/MD et formes d'affichage) appartiennent aux contrats.

`SHARED_BACKING` n'est pas un domaine fonctionnel: il ne contient que les
variables placees correspondant aux `extern`, sans fonction, init, reset ou
policy. L'initialisation reste chez le writer proprietaire. `PLATFORM_H743`
porte seulement les seams de composition mono-coeur, le hardware
board et le staging/remap LED physique. Boutons, encodeurs et logique produit LED
appartiennent a CONTROL. Les backings diagnostic/waveform, FIFO, Recorder,
Preview, page-cache et projections Sampler appartiennent a `SHARED_BACKING`.
Le page-cache n'y est pas masque: `sample_page_cache.c` possede les
metadonnees, index, reservations et publications CONTROL;
`sample_page_cache_audio.c` possede les credits et acces AUDIO. Le port H747
ne change que leur placement physique. Les appels CONTROL vers AUDIO ne passent
que par `Inc/IPC`; le compile-check Cortex-M4 interdit toute dependance vers
`Inc/Audio`, `Src/Audio` et les DSP tiers. Le firewall CONTRACTS refuse en plus
les headers prives CONTROL/AUDIO et les anciennes APIs owner-specific sorties de
la liste des contrats. Les checks compilent de vrais objets CM4/CM7, incluent
les DSP tiers declares, produisent symboles/relocations/sections et ferment les
indefinis par provider ou allowlist nominative avant un link relocatable.

## Robustesse

Boot, faults, watchdog et diagnostics doivent rester bornes et sans allocation dynamique dans les chemins critiques. Les informations de crash persistantes sont diagnostiques, jamais une seconde autorite runtime.
