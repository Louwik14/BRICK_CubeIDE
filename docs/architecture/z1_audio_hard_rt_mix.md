# Z1 — Audio hard-RT et mix

L'IRQ audio exécute des chemins bornés et préalloués. Une piste `0..7` configurée avec un moteur audio obtient une voie physique; `Off` et `MIDI` n'en consomment pas. L'identité logique ne dépend jamais du numéro de voie.

Les moteurs locaux sont Synth, Drum, Sampler RAM/Stream/Multi/Looper et External. Looper possède son runtime par slot. External lit uniquement l'entrée physique dont le slot est propriétaire; les entrées sans propriétaire sont désactivées et aucun monitoring parallèle n'est créé.

Les moteurs internes publient exclusivement dans les lanes externes du mixer associées aux pistes logiques. La track physique `tracks[3]`, sans entrée matérielle, reste désactivée : elle n'est ni une autorité d'activation des moteurs ni une source silencieuse de substitution.

Le pool synth maintient une table directe voix logique → slot physique; le rendu et les événements de note ne rescannent pas les owners globaux. Les configurations Prism, Stack, Wave et DELUGE sont versionnées : une voix ne recopie la configuration de sa track que lorsque cette version change.

Tous les états de voix des moteurs synthétiques, y compris les slots poly additionnels de Stack, Wave et DELUGE, résident en DTCM. Le coût d'accès d'une voix ne dépend donc plus de son index physique dans le pool global.

Le panorama de spread est précalculé lors des changements de configuration; la boucle audio lit directement le gain spatial de chaque voix sans division flottante par bloc.

Le runtime conserve un décompte par moteur synthétique. Les balayages Prism, Stack, Wave et DELUGE ne sont exécutés que si au moins une track liée utilise le moteur correspondant; la consommation fixe d'une configuration qui n'emploie pas ce moteur est ainsi évitée.

Le mixer applique niveau, pan, inserts valides, sends et traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Il n'existe plus de piste FX ni de chaîne MacroFX.

Les quotas Low-Cost/Premium limitent les ressources physiques, jamais la topologie logique de huit pistes.
