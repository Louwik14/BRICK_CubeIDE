# Z1 - Audio hard-RT, moteurs et mix

CONTROL est l'unique autorite musicale: il cree les outputs, applique les quotas Multi per-track/global, choisit les victimes et publie atomiquement NOTE OFF puis NOTE ON. Une commande legale est garantie par construction; AUDIO ne fait aucune admission ni stealing musical et traite une ressource indisponible comme une rupture d'invariant. Il mappe `output_id` vers un slot DSP, rend les moteurs et possede FREE/RELEASE physique. Aucun scheduler CONTROL n'appelle directement un moteur ou le mixer.

`STOP(output_id)` retire HELD cote AUDIO mais une tail RELEASE peut continuer. Sa fin ne produit aucun ACK musical. Si le slot doit etre reutilise, AUDIO le reinitialise physiquement avant le nouveau START.

Les moteurs rendent dans les lanes du programme courant de chaque entite. Le
mapping d'execution AUDIO porte `output_id`, note, velocity et gate sans devenir
une autorite d'admission. Le pool synth maintient son mapping vers les slots
physiques. Tout PROGRAM remplace synchroniquement le renderer au sample
commande, conserve les outputs logiques et initialise localement le nouveau DSP
pour les notes rendables; aucun NOTE OFF/ON n'est fabrique. Un moteur incompatible
peut donc rester silencieux sans fermer le ledger; son retour compatible
reprojette les notes encore vivantes. Les etats chauds des voix restent en DTCM
et aucun chemin audio n'alloue dynamiquement.

Le mixer applique filtre, VCA, niveau, pan, inserts, sends puis traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Send3 ne conserve que Daisy Stereo et Junologue; VIBE et DRIFT sont des inserts par entite. VIBE utilise le kernel Deluge Float avec politique `dry + wet` 1:1. DRIFT expose DELAY et FEEDBACK, sans LFO interne.

La reverb globale utilise le kernel Mutable/Deluge, son buffer float de 32768 elements et ses cinq controles normalises ROOM SIZE, DAMPING, WIDTH, HPF et LPF. WET reste exterieur au moteur.

## GROUP

Le GROUP master 7 possede le bus AUDIO, les deux kernels Audio FX A/B, MOD et les traitements post-somme. Les children 8..15 gardent leur moteur, chemin mono/stereo, filtre, VCA, niveau, pan et mute locaux. Leur dry et leurs niveaux locaux A/B alimentent les bus GROUP; leurs sends globaux sont neutralises. Les sorties A/B sont reinjectees en parallele avant filtre et MIX master. Le mute parent coupe le bus et les contributions children sans reecrire leur mute local.

## Sampler mono et stereo

Le format est immutable pendant la voix. Une page physique de 16 KiB contient 4096 frames mono FLOAT32 ou 2048 frames stereo entrelacees. Mono reste mono jusqu'au pan final; Multi applique filtre et VCA par voix avant spread/pan. Les inserts recoivent le signal stereo apres cette projection. Reverse et ping-pong appartiennent au Sampler RAM, pas au streamer.

## Wave

Wave possede OSC1, OSC2 et COMMON. TABLE est un slot logique projete vers un slot/generation AUDIO. Les deux oscillateurs sont independants; WAVE ne possede aucun routage ou etat de modulation croisee. L'interpolation de frame et de sample est permanente, POS reste l'axe des frames et aucun smoothing POS n'est applique. START (0..100 %) et LEN (1..100 %) definissent une fenetre lineaire interne bornee a la fin du cycle : `effective_len = min(LEN, 1 - START)`, puis `read_phase = START + phase_porteuse * effective_len`. La phase porteuse et le pitch restent possedes par la voix, aucun wrap de lecture n'est applique, et START=0/LEN=100 conserve le chemin historique bit-identique.

Le snapshot de waveform est une publication seqlock AUDIO->CONTROL fixe et sans pointeur. Il capture au plus 48 points par oscillateur, a 20 Hz maximum, sans second rendu. Desactive, il n'ajoute aucun cout par sample.

## Integration d'un moteur

Un nouveau moteur doit ajouter son type canonique, installation PROGRAM, capacites, catalogue de parametres/backends, rendu borne, mapping/fermeture physique, exposition UI et cles persistantes. Il ne doit contourner ni `track_runtime`, ni le registre de parametres, ni la FIFO fonctionnelle CONTROL/AUDIO.
