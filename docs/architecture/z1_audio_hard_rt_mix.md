# Z1 - Audio hard-RT, moteurs et mix

AUDIO est l'unique autorite d'admission des notes internes, d'allocation, de stealing, de rendu, de mixer et de page-cache. CONTROL publie des intentions fixes; aucun scheduler CONTROL n'appelle directement un moteur ou le mixer.

Les moteurs rendent dans les lanes externes associees aux bindings. Le pool synth maintient un mapping direct voix logique -> slot physique; les configurations de track sont versionnees et recopies seulement lors d'un changement. Les etats chauds des voix restent en DTCM et aucun chemin audio n'alloue dynamiquement.

Le mixer applique filtre, VCA, niveau, pan, inserts, sends puis traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Send3 ne conserve que Daisy Stereo et Junologue; VIBE et DRIFT sont des inserts par entite. VIBE utilise le kernel Deluge Float avec politique `dry + wet` 1:1. DRIFT expose DELAY et FEEDBACK, sans LFO interne.

La reverb globale utilise le kernel Mutable/Deluge, son buffer float de 32768 elements et ses cinq controles normalises ROOM SIZE, DAMPING, WIDTH, HPF et LPF. WET reste exterieur au moteur.

## GROUP

Le GROUP master 7 possede le bus AUDIO, les deux kernels Audio FX A/B, MOD et les traitements post-somme. Les children 8..15 gardent leur moteur, chemin mono/stereo, filtre, VCA, niveau, pan et mute locaux. Leur dry et leurs niveaux locaux A/B alimentent les bus GROUP; leurs sends globaux sont neutralises. Les sorties A/B sont reinjectees en parallele avant filtre et MIX master. Le mute parent coupe le bus et les contributions children sans reecrire leur mute local.

## Sampler mono et stereo

Le format est immutable pendant la voix. Une page physique de 16 KiB contient 4096 frames mono FLOAT32 ou 2048 frames stereo entrelacees. Mono reste mono jusqu'au pan final; Multi applique filtre et VCA par voix avant spread/pan. Les inserts recoivent le signal stereo apres cette projection. Reverse et ping-pong appartiennent au Sampler RAM, pas au streamer.

## Wave

Wave possede OSC1, OSC2 et COMMON. TABLE est un slot logique projete vers un slot/generation AUDIO. Les deux oscillateurs sont independants; WAVE ne possede aucun routage ou etat de modulation croisee. L'interpolation de frame et de sample est permanente, POS est mis a jour a chaque sample et aucun smoothing POS n'est applique; phase et pitch restent possedes par la voix.

Le snapshot de waveform est une publication seqlock AUDIO->CONTROL fixe et sans pointeur. Il capture au plus 48 points par oscillateur, a 20 Hz maximum, sans second rendu. Desactive, il n'ajoute aucun cout par sample.

## Integration d'un moteur

Un nouveau moteur doit ajouter son type canonique, binding runtime, capacites, catalogue de parametres/backends, rendu borne, admission/fermeture, exposition UI et cles persistantes. Il ne doit contourner ni `track_runtime`, ni le registre de parametres, ni les files CONTROL/AUDIO.
