# Architecture globale — contrat actuel

Ce document est la carte canonique du produit courant. Le code réel reste l'autorité ; les documents Z détaillent les zones locales.

## Modèle global

Le produit est organisé en trois couches :

1. l'état canonique et les commandes de contrôle ;
2. la projection runtime track-aware ;
3. l'exécution bornée audio, séquenceur, MIDI et UI.

Une décision ne doit avoir qu'une autorité. Une représentation interne peut être différente du vocabulaire produit, mais elle ne crée pas un second ownership.

## Topologie et runtime

`track_topology` est l'autorité de la topologie produit : rôles, présence, cardinalités, identités `role + ordinal` et capacités structurantes.

- 8 Play Tracks configurables ;
- Low-Cost : 4 Special (`Input1`, `Looper`, `FX`, `Master`) et 12 tracks actives ;
- Premium : 6 Special (`Input1`, `Looper`, `FX`, `Master`, `Input2`, `Input3`) et 14 tracks actives ;
- capacité de stockage commune : 14 slots ;
- aucune ressource Input4 ;
- les Special ne sont pas convertibles en famille/type Play.

`track_runtime` lit la configuration canonique et publie la projection effective : famille, type, moteur, binding, cible mixer, capacités, domaines disponibles et statut des paramètres. Le mapping track logique → cible physique est explicite. L'invalidation et le refresh sont demandés explicitement ; les queries runtime restent pures.

La distinction est donc :

| Contrat produit | Projection interne | Exécution |
|---|---|---|
| rôle, famille, type, capacité | descriptor/runtime context, binding et mix target | moteur, mixer, scheduler et backends |
| ownership logique d'un paramètre | domaine, ressource, cardinalité, statut | apply vers filtre, mixer/VCA, modulation ou moteur |
| identité de track | lane ou instance effectivement réservée | audio et événements bornés |

Les lanes physiques DSP et les tracks logiques ne sont jamais confondues.

## Rôles Special

`Master`, `FX`, `Looper` et `Input` sont quatre rôles conceptuels distincts.

- Master possède l'interface produit des effets globaux reverb, delay et compresseur ;
- FX possède quatre MacroFX et leur état TONE ;
- Looper possède les contrôles et le routage de boucle ;
- Input représente une entrée physique fixe de la variante.

Les MacroFX sont la propriété produit de FX. `fx_master_macro` est un nom DSP légitime pour l'insertion post-mix sur le master-bus ; il ne transforme pas Master en propriétaire MacroFX. `MIX` reste le mix track-aware et ne devient pas un conteneur d'effets Master.

## Paramètres et UI

Les ensembles courants sont `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX`.

- `CFG` porte famille/type, MIDI et `VOICES`/`SPREAD` ;
- `ENV` porte filtre, VCA, ENV3 et retriggers ;
- `TONE` porte les moteurs et les surfaces propres aux rôles ;
- `MOD` porte LFO, Matrix, Multi et Slew ;
- `MIX` porte niveau, pan, sends et mute ;
- `PLAY` porte les contrôles propres au moteur ;
- `MIDI FX` porte les quatre slots par Play Track.

ENV est l'owner logique unique de FLT, VCA et ENV3. Le mixer reste le backend VCA et `mod_env3` reste le backend ENV3. BTN6 Premium cible `ENV/VCA`; le module UI courant est `ui_page_template_env`.

ARP est un raccourci physique et un modèle MIDI FX. Il ne constitue pas une capacité autonome et n'ajoute aucun hall mode musical.

La persistence s'appuie sur la classification explicite des paramètres : domaine, ressource, cardinalité, statut et scope de stockage. Elle ne reconnaît plus l'ancienne plage MIX comme règle d'ownership. `VOICES` et `SPREAD` sont `CFG`, non p-lockables et non modulables. Les IDs granular `0..5` sont réservés et inertes ; aucun granular produit n'est actif.

## Persistence courante

| Format | Version | Scope |
|---|---:|---|
| Pattern | 4 | état Play/Special et globals du pattern |
| Project | 4 | projet et pattern embarqué |
| Patch | 3 | une Play Track uniquement |
| Kit | 3 | snapshot de kit et rôles compatibles |

Les séquences Play et Special ont des modèles, pools et limites distincts. Les snapshots et fichiers valident les identités `role + ordinal` avant mutation. Les API dont le nom interne contient `V1` sont conservées pour ne pas renommer leur interface dans cette passe.

## Découpage des zones

| Zone | Autorité actuelle | Lire pour |
|---|---|---|
| Z0 | plateforme et cadence hors IRQ | boot, superloop, diagnostics |
| Z1 | audio hard-RT et mix | IRQ, DMA, moteurs, bus |
| Z2 | topologie et projection runtime | rôles, capacités, bindings |
| Z3 | registre, apply, p-locks et modulation | domaines et ownership des paramètres |
| Z4 | séquenceur, clock et scheduler | modèles Play/Special et événements |
| Z5 | UI et interaction | navigation, boutons, pages, clipboard |
| Z6 | snapshots et fichiers | versions, classification et restore |

Les frontières sont des dépendances orientées : Z2 projette pour Z1/Z3/Z4/Z5/Z6 ; Z3 classe et applique ; Z6 persiste sans devenir propriétaire des backends ; Z1 ne déduit aucune décision produit depuis une lane physique.

Le futur split dual-core se prépare par des seams et snapshots explicites. Aucune architecture dual-core, bus central ou IPC spéculatif n'est un contrat produit actuel.

## Historique utile

Les anciens ensembles, anciennes familles de Special, anciennes lanes MIX, le granular et les groupes de séquence ne sont pas des variantes concurrentes du produit. Ils peuvent apparaître dans les audits de preuve, mais seulement comme historique explicite ou dette volontaire ; ils ne doivent pas être utilisés pour décrire l'état courant.
