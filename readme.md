# BRICK6 — produit actuel

BRICK6 est une machine audio embarquée track-aware pour le jeu live, séquencée et déterministe. Le code courant est l'autorité de ce document.

## Topologie

La topologie logique est publiée par `track_topology` et projetée par `track_runtime`.

- 8 Play Tracks configurables sur Low-Cost et Premium ;
- Low-Cost : `8 Play + Input1 + Looper + FX + Master` (12 tracks actives) ;
- Premium : `8 Play + Input1 + Looper + FX + Master + Input2 + Input3` (14 tracks actives) ;
- le stockage commun conserve 14 slots ; les tracks inutilisées Low-Cost ne sont pas sélectionnables ;
- les Special sont fixes et ne sont jamais des familles/types de Play configurables ;
- les tracks logiques et les lanes DSP physiques sont des espaces distincts.

`track_topology` est l'autorité des rôles, présences et cardinalités. `track_runtime` en est la projection autoritaire pour familles, types, capacités, bindings et cibles effectives. Les refresh sont explicites ; les getters runtime ne rafraîchissent pas implicitement.

### Play

Familles configurables : `Off`, `Synth`, `Sampler`, `Drum`, `MIDI`, `External`.

Types exposés par famille :

- `Synth` : `Prism`, `Wave`, `Stack`, `DELUGE` ;
- `Sampler` : `RAM`, `Stream`, `Multi` ; `Looper` est la Special Looper, pas un type Play sélectionnable ;
- `Drum` : `TRX BD`, `BD Analog` ;
- `MIDI` : `MIDI` ;
- `External` : `External`.

`Off` désactive réellement la track. `MIDI` produit des notes sans chemin audio local. `External` associe MIDI et une entrée physique exacte ; `track_input_ownership` est l'autorité unique de cette réservation. Une entrée réservée reste visible sur sa Special avec `USED Pn`, sans monitoring en double ni remplacement automatique.

Chaque Play Track est indépendante pour clavier, MIDI, MIDI FX, séquence, scheduler, live record, mute, paramètres, p-locks et snapshots. La polyphonie et le spread sont les paramètres `CFG` `VOICES`/`SPREAD`, appliqués par `synth_polyphony` ; ils ne sont ni p-lockables ni modulables.

### Special

Les rôles sont fixes : `Master`, `Looper`, `Input` et `FX`.

- `Master` porte les effets globaux reverb, delay et compresseur ;
- `FX` porte exclusivement quatre slots MacroFX ;
- Master et FX exposent `CFG`, `TONE` et leur séquence/action Special ; `ENV`, `MOD` et `MIX` y restent indisponibles ;
- `Looper` porte son routage et ses contrôles de boucle ;
- `Input` représente chaque ressource d'entrée physique de la variante ;
- Master et FX sont deux rôles distincts, dérivés de `track_topology`.

Les MacroFX appartiennent à FX même si le backend d'insertion s'appelle `fx_master_macro` : ce nom décrit l'insertion master-bus DSP, pas le propriétaire produit.

## Ensembles UI

Les ensembles produits courants sont : `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX`.

- `CFG` : famille/type, MIDI, configuration et `VOICES`/`SPREAD` ;
- `ENV` : filtre, VCA, ENV3 et leurs retriggers ;
- `TONE` : moteur, oscillateurs, sampler, drum, effets Master ou MacroFX selon le rôle ;
- `MOD` : LFO, Matrix, Multi et Slew ;
- `MIX` : niveau, pan, sends et mute track-aware ;
- `PLAY` : commandes propres au moteur ;
- `MIDI FX` : modèle MIDI FX et ses quatre slots par Play Track.

ENV est l'unique propriétaire logique du filtre, du VCA et d'ENV3, sur les deux variantes. Les backends physiques restent légitimes : VCA dans le mixer et ENV3 dans la modulation. Il n'existe aucun ensemble VCA autonome. Sur Premium, BTN6 ouvre `ENV` directement sur la sous-page VCA. Le module UI courant est `ui_page_template_env`.

ARP est un raccourci physique et le nom d'un modèle MIDI FX (`note_fx_arp`), pas une capacité topologique autonome. L'ouverture de MIDI FX ne change pas le mode musical `SEQ`/`KEYBOARD`.

## Audio et moteurs

Le chemin audio est borné, sans RTOS ni allocation dynamique dans l'IRQ : moteur, filtre, VCA/volume, inserts track, sends et bus, puis traitements globaux. Les moteurs produits sont Prism, Wave, Stack, DELUGE, Sampler RAM/Stream/Multi, Drum et MIDI/External selon la capacité de la track. La Special FX applique ses quatre MacroFX sur l'insertion master-bus dédiée.

Les entrées physiques sont limitées à Input1 sur Low-Cost et Input1..3 sur Premium. Il n'existe pas d'Input4 produit.

## Persistence

Les versions courantes sont strictes :

- Pattern v4 ;
- Project v4 ;
- Patch v3, Play-only ;
- Kit v3.

La persistence et les snapshots classent chaque paramètre explicitement selon son domaine et son ownership. Elle ne déduit plus l'ownership d'une ancienne plage numérique MIX. Les identités de tracks persistées utilisent `role + ordinal`. Les séquences Play et Special ont des modèles distincts ; les états audio transitoires ne sont pas sérialisés.

Le registre conserve les IDs `0..5` réservés pour l'ancien produit granular. Le granular n'est plus un moteur, une capacité ou une surface produit. Les ordinals réservés restants sont conservés pour stabilité ; ils ne constituent pas des contrôles actifs.

## Autorités à retenir

La logique canonique décide ; `track_runtime` projette ; les backends exécutent. Toute nouvelle règle track-aware doit utiliser les tables explicites de `track_topology`, `track_runtime`, des domaines de paramètres et des capacités runtime correspondantes. Les documents d'architecture Z2 à Z6 détaillent ces contrats.

Historique utile : les noms `V1` des API persistence restent des noms techniques ; ils ne désignent pas la version courante des fichiers Pattern/Project.
