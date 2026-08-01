# Z2 — Track runtime authority

Z2 définit le contrat unique entre la topologie produit et les capacités runtime. Le code de `track_topology`, `track_state` et `track_runtime` est l'autorité.

## Capacité VCA Sampler

`track_runtime_supports_vca_gate()` autorise `Sampler/Stream` lorsqu'il est bindé et audio-routable. `Sampler/Looper` reste explicitement exclu. Cette capacité pilote les paramètres VCA effectifs, la sous-page ENV/VCA et les appels communs du clavier et du scheduler vers le mixer.

## Topologie produit

La topologie est compile-time et dépend de la variante :

| Variante | Play | Special | Tracks actives |
|---|---:|---|---:|
| Low-Cost | 8 | Input1, Looper, FX, Master | 12 |
| Premium | 8 | Input1, Looper, FX, Master, Input2, Input3 | 14 |

Le stockage partagé conserve 14 slots. `track_topology` publie les rôles, les ordinals, la catégorie Play/Special/unused, les entrées physiques et les capacités. Il n'existe pas d'Input4. Une Special est fixe : elle n'est jamais une famille ou un type configurable de Play.

Les indices courants sont Input1=8, Looper=9, FX=10 et Master=11; Premium ajoute Input2=12 et Input3=13.

Les identités persistables et copiables sont `role + ordinal`. Une lane DSP ou un index mixer n'est jamais déduit de l'ordinal logique sans passer par la projection runtime.

## Autorités

- `track_topology` : présence, rôle, cardinalité, identité et capacités structurelles ;
- `track_state` : configuration canonique des Play (`family`, `type`, MIDI et valeurs CFG) ;
- `track_runtime` : projection autoritaire de famille, type, moteur, binding, capacité effective, cible mixer, cible filtre et ensembles UI ;
- `track_input_ownership` : réservation exclusive des entrées par les tracks `External` ;
- `synth_polyphony` : quota, voix, spread et ownership des slots synth ;
- `track_mute` : comportement de mute.

Les mutations invalident explicitement le runtime puis un appel autorisé demande le refresh. `track_runtime_get_*` et `track_topology_get_*` sont des queries ; ils ne rafraîchissent pas implicitement l'état.

## Play : familles, types et capacités

Les familles Play configurables sont `Off`, `Synth`, `Drum`, `MIDI`, `External` et `Sampler`. Les types courants sont :

L'ordre de sélection CFG des Play est explicitement `Off → Synth → Drum → MIDI → External → Sampler`. Il est indépendant des valeurs numériques persistées de `ui_track_family_t`; les labels sont résolus par la famille enum canonique et les familles Special `Input1..3` restent hors de ce catalogue.

- Synth : `Prism`, `Wave`, `Stack`, `DELUGE` ;
- Sampler : `RAM`, `Stream`, `Multi` ;
- Drum : `TRX BD`, `BD Analog` ;
- MIDI : `MIDI` ;
- External : `External`.

`Off` est une désactivation runtime. `MIDI` est une track de notes sans audio local. `External` associe MIDI et audio et réserve exactement une entrée physique. La réservation est refusée atomiquement si l'entrée exacte est déjà utilisée ; aucune autre entrée n'est choisie automatiquement.

Les capacités runtime principales sont notes, audio, MIDI, clavier, MIDI FX, automation, mute et réservation d'entrée. Elles sont publiées par la topologie puis restreintes par le binding effectif. Le scheduler, le clavier et le MIDI FX n'allouent jamais de comportement Play à une Special.

## Special et ownership

Les rôles sont résolus directement avec `track_topology_find_special()` :

- Master : effets globaux reverb, delay, compresseur ; pas de MacroFX ;
- FX : quatre slots MacroFX, avec `CFG` et `TONE` ; `ENV`, `MOD` et `MIX` sont indisponibles ;
- Looper : backend Looper et contrôles de boucle ;
- Input : monitoring de l'entrée physique correspondante.

Master et FX sont donc deux rôles différents. `fx_master_macro` est une projection DSP post-mix légitime ; son nom ne désigne pas un rôle Master.

La matrice d'ensembles des deux rôles est fixe : Master et FX exposent `CFG`, `TONE` et leur séquence/action Special ; `ENV`, `MOD` et `MIX` restent absents. Cette matrice est projetée par `track_runtime` et ne dépend pas de la configuration UI brute `Off/Audio` des adaptateurs Special.

Les entrées physiques sont publiées par la variante : `Input1` en Low-Cost, `Input1..3` en Premium. Une entrée réservée par `External` reste visible sur son rôle Input avec `USED Pn`, sans second monitoring.

## Paramètres et ensembles

`track_runtime_get_param_rule()` est le resolver commun des domaines et ressources :

- `CFG` : famille/type, MIDI, `VOICES` et `SPREAD` ;
- `ENV` : filtre, VCA, ENV3 et retriggers ;
- `TONE` : moteur, effets Master ou MacroFX selon le rôle ;
- `MOD` : LFO, Matrix, Multi et Slew ;
- `MIX` : niveau, pan, sends et mute ;
- `PLAY` : paramètres de jeu/moteur ;
- `MIDI_FX` : quatre slots MIDI FX d'une Play Track.

`VOICES` et `SPREAD` sont CFG, appliqués par `synth_polyphony`, non p-lockables et non modulables. ENV est le propriétaire logique unique du filtre, VCA et ENV3 ; les backends physiques restent en Z1/Z3.

## Polyphonie

La polyphonie synth est bornée par un budget global explicite et par les slots réservés à chaque Play Track. Le nombre de voix demandé ne crée ni migration de track, ni second owner ; une réduction coupe et libère les voix conformément à `synth_polyphony`. Les moteurs non synth ont leurs propres capacités runtime.

## Contrat de projection

Pour toute consommation, le contexte résolu doit conserver explicitement :

`track logique → rôle/capacités → famille/type → moteur/instance → mix target/filter target`.

Un consumer ne déduit pas un binding par formule historique. Les transitions, paste, restore et changement de type valident la capacité et le quota avant mutation ; les conflits retournent une erreur bornée.

## Historique utile

Les anciennes familles `Input`, `Master` ou `Looper` configurables, le shim `runtime_target` et l'ancien type `Input/Hybrid` ne sont pas des contrats courants. Les noms internes `Braids`, `Daisy` et `note_fx_arp` peuvent rester dans le code lorsqu'ils désignent un backend ou un modèle technique précis.
