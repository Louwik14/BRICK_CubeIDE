# Z0 - Plateforme, memoire, cadence et frontieres

## Execution

L'audio travaille par demi-buffer de 64 frames a 48 kHz et n'execute ni FatFs, ni scan de cache, ni travail Storage non borne. TIM5 a 1 MHz, demarre avant les domaines, est l'unique media clock BRICK; `brick_media_clock` possede son extension 32 vers 64 bits, l'IRQ d'overflow et l'unique conversion rationnelle en samples. CONTROL, SEQ et AUDIO lisent cette meme timeline. L'origine de la grille AUDIO est capturee au demarrage effectif du RX DMA. Le port H743 reveille l'IRQ SEQ periodique TIM4 a 750 Hz; AUDIO priorite 1 preempte SEQ priorite 2, qui preempte la superloop. SEQ prepare l'intervalle absolu suivant et AUDIO applique uniquement les evenements terminaux dates. Aucun comportement musical ne depend d'un numero de tick ni de la valeur 64, qui reste une constante de port.

USB OTG FS est possede exclusivement par TinyUSB en mode bare-metal
(`OPT_OS_NONE`). Sur H743, l'IRQ OTG FS de priorite 6 fait recevoir le paquet
Audio OUT par TinyUSB, rearme l'endpoint, puis `tud_audio_rx_done_isr()` retire
exactement ce paquet de la FIFO TinyUSB et le publie immediatement dans le ring
PC vers BRICK. Un callback traite un seul paquet, sans boucle de vidage. Les
paquets observes portent 47 a 49 frames stereo; le maximum FS declare est de
392 octets, soit 49 frames de 8 octets. L'IRQ SAI AUDIO, de priorite 1, reste
plus prioritaire et peut interrompre cette publication USB.

L'IRQ USB ne lance jamais le pump general `tud_task_ext()` et n'execute ni
MIDI, ni controle USB, ni calcul complet de feedback. La superloop conserve le
role manager, le pump TinyUSB general borne a quatre evenements, le feedback
UAC2, Audio IN BRICK vers PC, USB MIDI Device et le role Host MIDI. Elle appelle
le service USB normal avant et apres la passe applicative. La cadence de cette
superloop n'est plus dans le chemin d'ingress Audio OUT PC vers BRICK.

Le premier paquet de feedback est arme apres le callback d'activation de
l'interface : il contient donc la valeur nominale 48.0 en 16.16, jamais la
valeur d'initialisation nulle de TinyUSB.

Les flux UAC2 PCM32 sont convertis uniquement a la frontiere TinyUSB et
traversent ensuite deux rings SPSC FLOAT32 interleaved de 288 frames
places dans la moitie D3 non cachee. Pour PC vers BRICK, le writer unique est
le callback IRQ Audio OUT USB et le reader unique est AUDIO. Pour BRICK vers
PC, AUDIO reste writer et le service USB differe reste reader. L'IRQ AUDIO ne
touche jamais TinyUSB.
Le role Host applique une attente VBUS de 200 ms par deadline, et les erreurs
I2C FUSB utilisent un retry cadence. Le latch/level `INT_N` reste le chemin
normal. Les registres read-to-clear distinguent attach, detach, changement CC
et erreur. Un detach arrete le role puis relance le DRP. Les retries I2C et la
ligne `INT_N` persistante sont bornes a 100 ms; une reconciliation watchdog a
5 s couvre uniquement une EXTI perdue ou un reset silencieux du FUSB.

Le Hall Low-Cost execute la machine bornee depuis l'acquisition ADC. TIM5 est le compteur libre commun de capture et de media time. La plateforme possede son extension et sa conversion; aucune sample clock locale AUDIO n'est publiee vers CONTROL.

## Frontiere CONTROL/AUDIO

La frontiere suit `CONTROL/SEQ decide -> commande finale 16 octets -> IRQ AUDIO execute` sur le meme M7. La FIFO SPSC locale de 4096 commandes transporte PROGRAM, PARAM, NOTE, TRANSPORT, RECORD, PANIC et AUDIO_STATE_COMMIT. Les requetes visuelles typees AUDIO waveform et synth waveform empruntent egalement PARAM dans cette FIFO; elles n'ont ni mailbox ni file secondaire. Aucun pointeur, callback, contexte mutable, Pattern ou Project ne la traverse.

Les ingress Hall/MIDI et les sources scheduler restent des buffers locaux CONTROL. CONTROL resout et fusionne leur fenetre, transforme un retrigger en NOTE OFF puis NOTE ON au meme sample, puis publie un lot atomique dans la FIFO unique. AUDIO ne fusionne aucune queue et l'ordre physique FIFO est l'ordre fonctionnel a timestamp egal.

Le contrat maximal d'un horizon est 1024 commandes parametres, 1024 commandes NOTE et 35 commandes generales, soit 2083 commandes. Pattern et Project ne poussent plus leurs milliers de commandes dans la FIFO: CONTROL construit une transaction AUDIO locale bornee, puis publie une seule commande `AUDIO_STATE_COMMIT`. CONTROL attend ensuite que le `tail` FIFO ait franchi le commit; AUDIO ne l'avance qu'apres application, ce qui rend le workspace reutilisable sans ACK. La FIFO de 4096 couvre l'horizon, le pire cumul hors horizon de 953 commandes et une marge explicite de 512: besoin prouve 3548. A l'interieur d'un horizon, la reservation locale bornee porte tout le produit de la fenetre avant un commit FIFO unique.

La frontiere physique de plateforme est regroupee dans `Inc/Platform` et
`Src/Platform`. Les types et layouts fonctionnels appartiennent a `ControlRT`;
le backing de la FIFO et de la transaction appartient a `DOMAIN_CONTROL`. Les
publishers et queues locales appartiennent explicitement a `DOMAIN_CONTROL`,
jamais a `PLATFORM_H743` ou `SHARED_BACKING`. Les writers CONTROL, readers AUDIO,
publishers AUDIO et readers CONTROL sont des unites distinctes dans leur domaine proprietaire. Les
fichiers de metier CONTROL, les runtimes et DSP AUDIO, ainsi que les pools
Storage/Sampler, restent dans leurs domaines; `live_parameter_audio_runtime`
reste dans `Inc/Audio` et `Src/Audio` et n'est pas une projection IPC.

PROGRAM porte directement la structure moteur. PARAM porte les proprietes
finales et PANIC emprunte la meme FIFO; aucune generation musicale, queue
prioritaire ou plan fonctionnel de restore ne traverse la frontiere. L'etat
restore est valide puis republie par CONTROL avec le contrat final.

Sur H743, les objets IPC restants resident dans la moitie haute de SRAM4
`0x38008000..0x3800FFFF`, shareable et non-cacheable. Le Streamer n'en fait
plus partie: leases et index rapide sont locaux au M7 en D2 cacheable,
metadata et payloads restent dans leurs arenas SDRAM. La maintenance cache des
pages Stream appartient uniquement a la frontiere CPU/DMA. Le ring Recorder
reste dans son arena SDRAM cacheable et son etat SPSC local reside en DTCM. La
transaction AUDIO reside dans la SDRAM CONTROL cacheable locale. Elle ne porte
ni generation, ni checksum, ni magic, ni maintenance de cache inter-coeur.
`DMB` ordonne sa publication vers l'IRQ et le franchissement du `tail` protege
sa duree de vie. La zone historique
`.sdram_recorder` de 256 KiB reste shareable non-cacheable pour les autres
registries; les buffers DMA SAI sont en D2 non-cacheable.

Les principaux sens sont:

```text
CONTROL -> AUDIO : FIFO unique PROGRAM, PARAM, NOTE, TRANSPORT, RECORD, PANIC et requetes visuelles typees; data planes volumineux separes
AUDIO -> CONTROL : niveau REC, waveforms audio/synth et diagnostic Audio
Storage <-> AUDIO : rings et leases locaux, etats/generations de page et payloads bornes
```

Preview est un ring PCM SPSC M4->M7: CONTROL possede payload/`write_count`, AUDIO `read_count` et le gain/active local applique par PARAM. Recorder est monocoeur: AUDIO IRQ possede payload/`produced_frames`/fermeture/fault, STORAGE superloop possede `released_frames`, writer et erreurs SD. Le Streamer AUDIO date son DSP avec la media clock TIM5 canonique. Le transport et le REC bus sont des runtimes AUDIO locaux alimentes par TRANSPORT/PARAM; aucun snapshot parallele n'en revient. FILTER POS affiche la valeur CONTROL canonique; aucune valeur DSP n'est une autorite UI.

Au boot, `track_state` est initialise avant la projection finale `track_runtime`; le bridge Hall/keyboard et son focus sont ensuite initialises et synchronises depuis cette autorite canonique. PLAY/PAUSE ou une reconfiguration moteur ne font pas partie du protocole d'activation Hall.

## Memoire et port monocoeur H743 vers RT1172

Les budgets DTCM, D1, D2, SRAM2, SRAM3, SRAM4, ITCM et SDRAM sont controles par les linkers; toute croissance d'une region proche de sa limite exige un budget explicite. Les voix et etats chauds restent en DTCM; les arenas AUDIO volumineuses resident en SDRAM selon leur contrat cache.

La cible H743 puis RT1172 utilise une seule image M7. Le port conserve les
owners logiques, les priorites IRQ, les generations et les protocoles DMA,
mais aucun boot HSEM, image M4, mailbox ou protocole de caches prives. Le port
RT1172 devra fournir les placements RAM, attributs MPU/cache, hooks DMA et la
media clock equivalents; le Streamer ne demande aucune adaptation inter-core.

## Ownership logique du build monocoeur

Le build classe chaque unite dans un seul ensemble logique: `DOMAIN_CONTROL`,
`DOMAIN_STORAGE`, `DOMAIN_AUDIO`, `DOMAIN_CONTRACTS`, `SHARED_BACKING` ou
`PLATFORM_H743`. Il n'existe plus de domaine de transition mixte. UI,
sequenceur et etat Param canonique appartiennent a CONTROL; le refill Stream,
son page-cache, ses leases de recyclage, son I/O, son decode et son scheduler
appartiennent a STORAGE; DSP, projection Param appliquee, ENV3 et caches/plans
de modulation appartiennent a AUDIO. Les catalogues immuables partages
(modeles moteur/FX/MD et formes d'affichage) appartiennent aux contrats.

`SHARED_BACKING` n'est pas un domaine fonctionnel: il ne contient que les
variables placees correspondant aux `extern`, sans fonction, init, reset ou
policy. L'initialisation reste chez le writer proprietaire. `PLATFORM_H743`
porte seulement les seams de composition mono-coeur, le hardware
board et le staging/remap LED physique. Boutons, encodeurs et logique produit LED
appartiennent a CONTROL. Les backings diagnostic/waveform, Recorder,
Preview et projections Sampler appartiennent a `SHARED_BACKING`. Le backing du
page-cache Stream est local au domaine Sampler monocoeur.
`sample_page_cache.c` possede les
metadonnees, index, reservations et publications READY dans `DOMAIN_STORAGE`;
`sample_page_cache_audio.c` possede les credits et acces AUDIO. Ces unites sont
exclues du compile-check Cortex-M4 historique. Les appels CONTROL vers AUDIO
passent par `Inc/ControlRT`; les data planes physiques restants utilisent
`Inc/IPC`. Le compile-check CONTROL historique interdit toute dependance vers
`Inc/Audio`, `Src/Audio` et les DSP tiers. Le firewall CONTRACTS refuse en plus
les headers prives CONTROL/AUDIO et les anciennes APIs owner-specific sorties de
la liste des contrats. Les checks compilent de vrais objets CM4/CM7, incluent
les DSP tiers declares, produisent symboles/relocations/sections et ferment les
indefinis par provider ou allowlist nominative avant un link relocatable.

## Robustesse

Boot, faults, watchdog et diagnostics doivent rester bornes et sans allocation dynamique dans les chemins critiques. Les informations de crash persistantes sont diagnostiques, jamais une seconde autorite runtime.
