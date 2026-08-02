# Z1 — Audio hard-RT et mix

L'IRQ audio exécute des chemins bornés et préalloués. Une piste `0..7` configurée avec un moteur audio obtient une voie physique; `Off` et `MIDI` n'en consomment pas. L'identité logique ne dépend jamais du numéro de voie.

Les moteurs locaux sont Synth, Drum, Sampler RAM/Stream/Multi/Looper et External. Looper possède son runtime par slot. External lit uniquement l'entrée physique dont le slot est propriétaire; les entrées sans propriétaire sont désactivées et aucun monitoring parallèle n'est créé.

Le mixer applique niveau, pan, inserts valides, sends et traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Il n'existe plus de piste FX ni de chaîne MacroFX.

Les quotas Low-Cost/Premium limitent les ressources physiques, jamais la topologie logique de huit pistes.
