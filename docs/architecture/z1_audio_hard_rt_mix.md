# Z1 — Audio hard-RT et mix

L'IRQ audio exécute des chemins bornés et préalloués. Une piste `0..7` configurée avec un moteur audio obtient une voie physique; `Off` et `MIDI` n'en consomment pas. L'identité logique ne dépend jamais du numéro de voie.

Les moteurs locaux sont Synth, Drum, Sampler RAM/Stream/Multi/Looper et External. Looper possède son runtime par slot. External lit uniquement l'entrée physique dont le slot est propriétaire; les entrées sans propriétaire sont désactivées et aucun monitoring parallèle n'est créé.

Les moteurs internes publient exclusivement dans les lanes externes du mixer associées aux pistes logiques. La track physique `tracks[3]`, sans entrée matérielle, reste désactivée : elle n'est ni une autorité d'activation des moteurs ni une source silencieuse de substitution.

Le pool synth maintient une table directe voix logique → slot physique; le rendu et les événements de note ne rescannent pas les owners globaux. Les configurations Prism, Stack et Wave sont versionnées : une voix ne recopie la configuration de sa track que lorsque cette version change. Stack inclut les primitives anti-alias Deluge retenues sans moteur Deluge distinct.

Tous les états de voix des moteurs synthétiques, y compris les slots poly additionnels de Stack et Wave, résident en DTCM. Le coût d'accès d'une voix ne dépend donc plus de son index physique dans le pool global.

Le panorama de spread est précalculé lors des changements de configuration; la boucle audio lit directement le gain spatial de chaque voix sans division flottante par bloc.

Le runtime conserve un décompte par moteur synthétique. Les balayages Prism, Stack et Wave ne sont exécutés que si au moins une track liée utilise le moteur correspondant; la consommation fixe d'une configuration qui n'emploie pas ce moteur est ainsi évitée.

Le mixer applique niveau, pan, inserts valides, sends et traitements globaux. Reverb, delay, compresseur et gain Master sont globaux. Il n'existe plus de piste FX ni de chaîne MacroFX.

Les quotas Low-Cost/Premium limitent les ressources physiques, jamais la topologie logique de huit pistes.

## Addendum 2026-08-05 - NoteFx EUCLID borne

EUCLID reste un runtime MIDI FX, pas un moteur audio ni une sortie physique.
Chaque piste conserve trois instances ; chaque instance utilise un ledger fixe
de 16 sources et 16 owned. Le maximum logique est donc 8 x 3 x 16 = 384
sources et 384 owned avant les admissions aval. Le fan-out est fixe, sans
allocation, et les refus On/Off ainsi que les high-water par slot sont exposes
par les diagnostics NoteFx.

Le budget de demi-buffer reste 64 frames, avec 8 On par piste, 32 Off reserves
et 32 commandes. Ces valeurs sont des bornes de politique instrumentees, pas
une mesure de marge CPU H743 ; les mesures DWT et underrun restent differees.
