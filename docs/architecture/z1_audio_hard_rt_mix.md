# Z1 - Audio hard-RT, moteurs et mix

CONTROL est l'unique autorite musicale: il cree les outputs, applique les quotas Multi per-track/global, choisit les victimes et publie atomiquement NOTE OFF puis NOTE ON. Une commande legale est garantie par construction; AUDIO ne fait aucune admission ni stealing musical et traite une ressource indisponible comme une rupture d'invariant. Il mappe `output_id` vers un slot DSP, rend les moteurs et possede FREE/RELEASE physique. Aucun scheduler CONTROL n'appelle directement un moteur ou le mixer.

`STOP(output_id)` retire HELD cote AUDIO mais une tail RELEASE peut continuer. Sa fin ne produit aucun ACK musical. Si le slot doit etre reutilise, AUDIO le reinitialise physiquement avant le nouveau START.

Les moteurs rendent dans les lanes du programme courant de chaque entite. Le
mapping d'execution AUDIO porte `output_id`, note, velocity et gate sans devenir
une autorite d'admission. Le pool synth maintient son mapping vers les slots
physiques. Tout PROGRAM remplace synchroniquement le renderer au sample
commande apres prevalidation de toute sa capacite, conserve les outputs logiques
et initialise localement le nouveau DSP
pour les notes rendables; aucun NOTE OFF/ON n'est fabrique. Un moteur incompatible
peut donc rester silencieux sans fermer le ledger; son retour compatible
reprojette les notes encore vivantes. Un NOTE OFF recu pendant cette phase
silencieuse retire normalement l'output et interdit toute resurrection au retour
d'un moteur compatible. Les etats chauds des voix restent en DTCM
et aucun chemin audio n'alloue dynamiquement.

La croissance polyphonique planifie tous ses slots avant la premiere mutation.
Le rebind des outputs tenus ne masque aucun echec: l'absence volontaire de
renderer est un succes silencieux, tandis qu'un renderer promis mais impossible
declenche le fatal source du consumer.

Prism, Stack, Wave et FM utilisent le meme filtre/VCA physique par voix pour
`VOICES=1` et `VOICES=N`: le passage mono/poly ne change donc plus de
representation d'enveloppe. Un resize conserve les slots portant les outputs
HELD, puis autant de tails RELEASE que la nouvelle capacite le permet; seuls les
slots retires sont reinitialises. Le mapping logique est compacte autour de ces
slots sans recopier ni retrigger leur etat DSP. L'instance devenue voice 0 est
publiee comme nouvelle source de projection moteur et les plans AUDIO qui
adressent cette instance sont reconstruits.

La configuration moteur reste canonique sur l'instance primaire de la track.
L'adapter AUDIO porte le geste commun de projection vers tous les slots physiques:
une application PARAM live, une croissance de polyphonie et l'initialisation
d'une voix passent par cette meme projection propre a Prism, Stack, Wave, FM ou
TB303. Phase, gate, enveloppes, position et historique DSP restent possedes par
chaque voix; Drum et TB303 conservent leur polyphonie effective de un.

Drum reste monophonique mais son backend couvre les 16 slots physiques du pool
synth; toute admission valide possede ainsi une instance representable, y
compris lorsqu'elle recoit un slot 8..15.

Le mixer applique filtre, VCA, niveau, pan, inserts, sends puis traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Send3 ne conserve que Daisy Stereo et Junologue; VIBE, DRIFT, XFADE et DJ EQ sont des inserts par entite. VIBE utilise le kernel Deluge Float avec politique `dry + wet` 1:1. DRIFT expose DELAY et FEEDBACK, sans LFO interne.

La track EXT possede son entree physique via l'ownership CONTROL, puis AUDIO la publie dans la lane du programme. Son parametre TONE `GATE` est CONTROL-owned et publie vers AUDIO: `ON` desactive le VCA de gate et laisse passer l'entree en continu, tandis que `TRIG` active le VCA et reconstruit son compteur depuis le mapping AUDIO des `output_id` vivants. Les NOTE OFF inconnus et les NOTE ON deja presents ne modifient pas ce compteur; le dernier output ferme seul le gate. Le mute reste applique plus loin dans le mixer et conserve donc son autorite dans les deux modes.

La reverb globale utilise le kernel Mutable/Deluge, son buffer float de 32768 elements et ses cinq controles normalises ROOM SIZE, DAMPING, WIDTH, HPF et LPF. WET reste exterieur au moteur.

## GROUP

Le GROUP master 7 possede le bus AUDIO, les deux kernels Audio FX A/B, MOD et les traitements post-somme. Les children 8..15 gardent leur moteur, chemin mono/stereo, filtre, VCA, niveau, pan et mute locaux. Leur dry et leurs niveaux locaux A/B alimentent les bus GROUP; leurs sends globaux sont neutralises. Les sorties A/B sont reinjectees en parallele avant filtre et MIX master. Le mute parent coupe le bus et les contributions children sans reecrire leur mute local.

## Sampler mono et stereo

Le format est immutable pendant la voix. Une page physique de 16 KiB contient 4096 frames mono FLOAT32 ou 2048 frames stereo entrelacees. Mono reste mono jusqu'au pan final; Multi applique filtre et VCA par voix avant spread/pan. Les inserts recoivent le signal stereo apres cette projection. Reverse et ping-pong appartiennent au Sampler RAM, pas au streamer.

XFADE est un Insert FX terminal et exclusif dans sa chaine de track. Sa porteuse A est la sortie stereo de la chaine de track, apres filtre et traitements locaux; FILTER POS, FX ORDER et les modes MONO/MID/SIDE sont donc non applicables. Le seul mode spatial admis est STEREO.

TRACK 1..8 lit uniquement la source moteur/externe de la track cible, capturee au debut du bloc avant filtre, VCA, niveau, pan, inserts et sends. Seules les lanes effectivement ciblees sont copiees et le snapshot respecte le format/source canonique de chaque lane; il ne somme aucun bus global ni source d'une autre track. Une track ne peut pas se cibler elle-meme. LINE et USB lisent directement les lanes physiques decodees du bloc. REC n'est pas une cible: `REC_SOURCE` n'est lisible qu'a travers un Streamer. MASTER est construit avec les autres tracks, leurs sends/returns et la preview, sans la contribution main ni les sends de la track porteuse; XFADE remplace ensuite le bus juste avant la dynamique master. Une seule route MASTER est admise par CONTROL. Les taps TRACK etant immutables pour tout le bloc, les references reciproques ou indirectes ne reinjectent jamais une sortie XFADE dans une cible du meme bloc.

Les gains de courbe XFADE sont calcules lors de la preparation du parametre puis lisses lineairement sur un bloc. La boucle sample ne fait aucun lookup, routage, trigonometrie ou recherche de bus: seulement les multiplications/additions du mix stereo et les increments de gains.

DJ EQ reutilise l'unique DSP trois bandes CMSIS dans chaque slot Insert FX et suit toutes les capabilities generiques. FILTER POS place le filtre avant les deux FX (`PRE`), entre eux (`MID`) ou apres eux (`POST`); FX ORDER choisit A puis B ou B puis A. MONO traite la somme mono, MID seulement la composante centrale, SIDE seulement la composante laterale et STEREO les deux canaux. Les coefficients sont prepares par la LUT existante; le hot-path ne fait ni allocation, ni lock, ni recherche de routage.

## Wave

Wave possede OSC1, OSC2 et COMMON. TABLE est un slot logique projete vers un slot/generation AUDIO. CONTROL adresse `{entite logique, oscillateur}`; AUDIO resout cette destination vers les slots physiques courants du mapping `synth_polyphony` et applique la selection a chaque voix allouee. Les deux oscillateurs sont independants; WAVE ne possede aucun routage ou etat de modulation croisee. L'interpolation de frame et de sample est permanente, POS reste l'axe des frames et aucun smoothing POS n'est applique. START (0..100 %) et LEN (1..100 %) definissent une fenetre lineaire interne bornee a la fin du cycle : `effective_len = min(LEN, 1 - START)`, puis `read_phase = START + phase_porteuse * effective_len`. La phase porteuse et le pitch restent possedes par la voix, aucun wrap de lecture n'est applique, et START=0/LEN=100 conserve le chemin historique bit-identique.

Le snapshot de waveform est une publication seqlock AUDIO->CONTROL fixe et sans pointeur. Il capture au plus 48 points par oscillateur, a 20 Hz maximum, sans second rendu. Desactive, il n'ajoute aucun cout par sample.

## Integration d'un moteur

Un nouveau moteur doit ajouter son type canonique, installation PROGRAM, capacites, catalogue de parametres/backends, rendu borne, mapping/fermeture physique, exposition UI et cles persistantes. Il ne doit contourner ni `track_runtime`, ni le registre de parametres, ni la FIFO fonctionnelle CONTROL/AUDIO.
