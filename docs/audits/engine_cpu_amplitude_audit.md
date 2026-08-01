# Audit CPU des moteurs et contrat d’amplitude Prism/Drum

Audit statique du chemin audio sur le snapshot `87a2db5f3` (2026-08-02).
Cette passe ne modifie aucun firmware et ne prétend pas reproduire les pourcentages
observés sur le matériel.

## 1. Verdict

### Prism

Le code ne permet pas de conclure à une régression Braids faisant coûter trois
fois plus le premier oscillateur. Le motif « premier +3 %, suivant +1 % » est
plus compatible avec un coût composé : activation d’une lane externe, puis coût
incrémental d’une voix ou d’un oscillateur supplémentaire.

Deux cas doivent être séparés pendant la mesure :

| Ce que signifie « premier / second » | Première activation | Activation suivante |
|---|---|---|
| Deux voix polyphoniques | allocation/source externe, clear stéréo, moteur, filtre et VCA par voix | moteur + filtre/VCA de la voix, sommation dans les buffers poly |
| OSC0 puis OSC1 dans une instance Prism mono | lane/mixer partagé + un `MacroOscillator::Render` | second `MacroOscillator::Render` et seconde contribution au mix interne |

Le premier cas explique naturellement une marche plus grande que les suivantes.
Le second explique également un surcoût partagé par la première instance, mais ne
permet pas d’attribuer le +3 % au DSP Braids seul. En particulier, OSC1 est à
niveau zéro par défaut et le wrapper ne l’envoie pas à `Render` tant que son
niveau courant et sa cible sont inactifs.

La cause la plus plausible du profil +3 % / +1 % est donc la combinaison de la
branche poly introduite récemment et du coût partagé de la lane. La confiance est
moyenne : aucun relevé CPU versionné ne relie précisément la valeur historique
« environ +1 % » à un commit, et la valeur actuelle doit être séparée entre voix
poly et oscillateurs internes sur le matériel.

### Anomalies démontrées

Les constats statiques suivants sont démontrés par le code :

* le chemin poly applique un filtre et un VCA `env_adsr` à chaque voix dans
  `mixer_process_external_poly_voice()` ; le chemin mono applique le VCA dans la
  boucle finale du mixer ;
* `mixer_poly_filter_sync_config()` recopie la configuration de la track et force
  le VCA de chaque slot poly à actif ; c’est nécessaire pour libérer une voix
  indépendamment, mais c’est un coût par voix ;
* le wrapper Prism contient deux oscillateurs Braids, deux FIFO de 24 samples et
  une rampe de niveau interne ; les contrôles sont recalculés par bloc de rendu,
  pas par sample ;
* le runtime Drum MD accepte plusieurs profils mais le rendu audible actuel est
  limité à `MD_MODEL_TRX_BD` ; les autres profils MD aboutissent à un buffer nul,
  malgré leur présence dans le catalogue ;
* les tracks sampler liées exécutent un rendu/clear et soumettent un buffer même
  sans voix RAM active, ce qui est un candidat CPU à mesurer ; ce constat reste
  hors de toute modification Stream ou Sampler Multi ;
* la modulation LFO est appelée à chaque bloc, tandis que les inserts, sends et
  la plupart des chemins de lane ne deviennent réellement coûteux que lorsqu’une
  lane est active.

### À préserver

* le saut des voix poly réellement libres par `synth_polyphony_voice_is_renderable()` ;
* les sorties silencieuses anticipées de Wave, Stack et DELUGE lorsque la source
  n’est plus requise ;
* le maintien explicite de source nécessaire au release du VCA commun ;
* le lissage de niveau et le tail Prism tant qu’un test anti-clic et un test de
  release ne prouvent pas qu’ils sont redondants ;
* l’ordre fonctionnel `engine -> filtre -> VCA/volume/pan -> inserts -> sends/bus`.

## 2. Modèle de coût

Pour un bloc de `N` frames, la décomposition utile est :

```text
C_IRQ
= C_global_bloc
  + C_track_scan
  + C_track_lane_active
  + C_engine_fixed
  + Σ C_voice
  + Σ C_oscillateur
  + C_filtre
  + C_VCA_mix
  + C_modulation
  + C_inserts_sends_bus
  + C_conversion_IO
```

Ce modèle distingue la première transition de lane d’une contribution par voix.
Il ne faut pas soustraire deux affichages arrondis comme s’ils étaient des
cycles : les pourcentages de l’interface sont une moyenne lissée.

### Coût fixe global

Le callback DMA appelle `cpu_load_irq_begin()`, traite chaque segment audio, puis
`cpu_load_irq_end()`. À 48 kHz, le half-buffer standard est de 64 frames, soit
environ 1,33 ms de période. Le bloc passe par `audio_io_unpack()`, le DSP runtime,
`mixer_process()`, les effets master éventuels et `audio_io_pack_ramped()`.

Dans `brick6_audio_runtime_dsp()` sont exécutés à chaque bloc :

* refresh/cache de l’autorité track ;
* `mod_lfo_v1_process_block()` ;
* les points d’entrée sampler, looper, Prism, Stack, Wave et DELUGE ;
* le traitement du voice manager si la track hardware est active ;
* le mixer global, puis la macro-FX master et le preview.

Le Drum est le seul de ces moteurs conditionné par `synth_usage.drum_tracks` :
la boucle Drum complète est évitée lorsqu’il n’existe aucune track Drum. Les
autres points d’entrée parcourent leurs tracks et sortent rapidement si aucune
track correspondante n’est liée.

### Coût fixe de la première track sonore

La première lane externe publiée active dans le mixer :

* le format externe et ses buffers ;
* le plan de lane ;
* la branche mono-native ou stéréo/poly ;
* le filtre de track lorsqu’il n’est pas OFF ou lorsqu’un bypass est en transition ;
* le gain, VCA, volume, pan, mute, inserts, sends et bus de cette lane.

Pour une lane poly, `mixer_begin_external_poly()` efface les deux buffers stéréo
une fois par bloc. Le premier slot publiable paie ensuite la préparation du
filtre/VCA poly et le premier passage de sommation. Les slots suivants ne paient
pas ce clear de track, mais paient leur moteur et leur filtre/VCA par voix.

Une track active sans note ne signifie pas toujours « coût nul » : les scans de
résolution restent présents ; une track poly peut même entrer dans la branche
`begin_external_poly` avant qu’aucune voix ne soit publiable. À l’inverse Prism
mono, Wave mono et DELUGE mono ne soumettent pas de lane si leur renderer retourne
zéro. Le statut exact doit donc être mesuré séparément pour mono et poly.

### Coût moteur, voix et oscillateur

* `C_engine_fixed` : synchronisation des paramètres, préparation de contexte,
  allocation logique et éventuel clear de buffer.
* `C_voice` : une instance moteur et, en poly, une instance de filtre/VCA.
* `C_oscillateur` : uniquement les oscillateurs effectivement actifs, sauf les
  petites boucles de contrôle qui inspectent les slots silencieux.
* `C_filtre` : rendu biquad/EQ par lane mono/stéréo, ou par voix dans le chemin
  poly ; OFF possède une sortie anticipée hors transition.
* `C_VCA_mix` : `env_adsr_process_step()` par sample dans le mixer mono ou dans
  `mixer_process_external_poly_voice()` par voix.

Les divisions, interpolations et mises à jour de niveau sont surtout dans les
branches de transition. Les fonctions de pitch coûteuses des moteurs sont
préparées sur changement de note/paramètre, pas à chaque sample, à l’exception
des interpolations nécessaires à une rampe active.

## 3. Chaîne d’exécution et matrice des moteurs

### Chaîne commune

```text
scheduler / keyboard gate
  -> allocation synth_polyphony
  -> note_on/off moteur
  -> renderer par track ou par slot
  -> conversion / table / mise en forme
  -> filtre mono ou filtre poly par voix
  -> VCA ADSR mono ou poly
  -> volume / pan / mute
  -> sommation externe
  -> inserts / sends / bus / master
```

Le scheduler appelle `mixer_track_poly_note_on/off()` pour Prism, Stack, Wave et
DELUGE lorsqu’une track a plus d’une voix. Il appelle sinon
`mixer_track_vca_note_on/off()` pour les moteurs qui déclarent
`supports_vca_gate()`. La note moteur est ensuite envoyée à l’instance track ou
à l’instance allouée au slot.

### Matrice statique

| Moteur | Coût fixe probable | Coût par voix | Traitement silencieux | Filtre/VCA | Anomalies ou points à mesurer |
|---|---|---|---|---|---|
| Prism | scan track ; réservation mono ou clear poly ; sync des paramètres | `sync_voice`, rendu Braids, filtre/VCA poly si poly | slots libres ignorés ; rendu sans note retourne 0 | mono : mixer ; poly : filtre + VCA par voix | OSC0/OSC1 doivent être séparés ; deux Braids mais OSC1 OFF par défaut |
| Wave | préparation de deux contextes, clear de sortie lorsqu’un osc est actif | table/interpolation par oscillateur actif + filtre/VCA poly | phase et position peuvent avancer en silence ; `wave_advance_pos_silent_block()` peut parcourir N frames | VCA commun mono/poly | coût stable/dynamique et 1/2 osc déjà distingué par DWT local |
| Stack | buffer d’accumulation de 24 samples par instance | rendu des slots actifs, conversion/soft clip, puis filtre/VCA | free-running de phase en silence, sans rendu de modèle | VCA commun mono/poly | trois slots internes ne sont pas trois voix poly ; mode waveform change fortement le coût |
| DELUGE | préparation pitch/phase et buffer Q31 | rendu par chunks de 8, conversion Q31 vers float, niveau/velocity | phase avance sans rendu si gate/source inutiles | VCA commun mono/poly | pas d’ADSR interne de note ; rampe de niveau et pitch à distinguer |
| Drum MD / TRX-BD | boucle Drum par track, clear si shot inactif | mono ; deux phases LUT, quatre enveloppes logiques, RNG et clipping par sample | buffer nul par `memset` si inactif | VCA mixer déclenché par la track | `note_off` est sans effet ; profils MD hors TRX-BD actuellement silencieux |
| Drum BD analog | boucle Drum par track, calcul de fréquence par bloc | rendu Plaits/AnalogBassDrum par chunks de 8 | clear si shot inactif | VCA mixer déclenché par la track | enveloppe interne de percussion indispensable ; comparer shot actif et tail |
| Sampler RAM | clear stéréo et scan des voix | lecture/interpolation/cache de la voix active | track liée sans voix : clear, rendu et submit de zéro | VCA sur le chemin sampler prévu | mesurer le coût silencieux ; ne pas confondre avec Stream/Multi |

Les pourcentages par ligne ne sont pas déductibles du C : ils restent à mesurer.
Les moteurs n’ont pas vocation à avoir un coût égal : un modèle Stack triple,
un modèle Braids complexe, un Wave interpolé et un Drum analogique peuvent avoir
des coûts légitimement différents.

### Prism : fonctions et boucles significatives

`brick6_render_prism_tracks()` choisit le chemin mono si le nombre de voix de
rendu vaut un, sinon :

1. `mixer_begin_external_poly()` efface L/R ;
2. chaque slot non libre est synchronisé par `brick6_braids_runtime_sync_voice()` ;
3. `brick6_braids_runtime_render_instance()` génère des paquets de 24 samples ;
4. `mixer_process_external_poly_voice()` filtre, applique l’ADSR VCA et somme ;
5. le slot est libéré lorsque le VCA et le renderer sont terminés.

Dans `brick6_braids_runtime_render_instance()` :

* la boucle de contrôle inspecte toujours les deux OSC ;
* un OSC silencieux est flushé et ne passe pas dans `MacroOscillator::Render` ;
* un OSC actif génère `Render`, avec `Strike`, shape, pitch et paramètres par
  paquet de 24, puis `memcpy` vers sa FIFO ;
* chaque sample additionne les OSC actifs, calcule la normalisation de niveau et
  applique `instance->level` puis `BRAIDS_OUTPUT_TRIM`.

Le `memset(sync_block)` par paquet, le `memcpy` FIFO et les deux boucles OSC sont
des coûts réels, mais le code ne montre ni double rendu d’un même OSC ni rendu
du second OSC lorsque son niveau est nul.

### Wave, Stack et DELUGE

Wave prépare ses contextes avant la boucle sample. Le chemin stable fait des
lectures de table simples ; le chemin dynamique ajoute position lissée,
interpolation et sélection de frame. Le pitch est recalculé quand il est dirty.
Le DWT local existant sépare déjà les mesures « un oscillateur » et « plusieurs
oscillateurs » ; il doit être utilisé sans ajouter une nouvelle infrastructure.

Stack possède trois slots internes par instance. Les slots à niveau nul sont
ignorés, mais les slots actifs peuvent exécuter des rendus très différents :
sinus, formes, wavetable, SUB, FM, ring, triple, swarm ou bruit. Une rampe de
niveau/pitch fait entrer une division par sample dans la branche de transition.
Chaque paquet est accumulé, ramené par un gain d’énergie et passé par le soft
clip, avec un chemin diagnostic supplémentaire si la track est sélectionnée.

DELUGE n’a qu’un oscillateur par instance. Le renderer natif travaille par
chunks de 8 et la sortie Q31 est convertie en float par sample. Le pitch est
calculé par table/arithmétique bornée lorsque `pitch_dirty` est posé. Le niveau
interne est une cible de niveau, pas une ADSR de note.

## 4. Historique Prism

La recherche Git locale donne les changements pertinents suivants :

| Commit | Changement | Impact CPU plausible | Confiance |
|---|---|---|---|
| `ccdffad31` — 2026-07-28 | passage du wrapper Braids à deux `MacroOscillator`, deux états/FIFO, niveaux OSC et mix interne ; ajout du chemin mono no-copy | augmente le coût d’une instance lorsque OSC1 est actif ; ajoute contrôles, FIFO, clear/copies ; ne rend pas OSC1 si niveau nul | élevée pour le changement, faible pour son amplitude matérielle |
| `a44b58dac` — 2026-07-30 | refonte filtre : enveloppe préparée, paramètres lissés, mono/stéréo et nouvelles tables | peut augmenter le coût lorsque le filtre est ON ou en bypass transition ; OFF garde une sortie anticipée | moyenne |
| `bcfa7f0b7` — 2026-07-31 | introduction du chemin poly externe, buffers L/R, filtre poly et VCA par voix | explique directement une marche de première voix puis un coût incrémental par voix | élevée pour la structure, non mesurée en % |
| `23d42678b` — 2026-07-31 | remapping du stockage des filtres poly et mute gain | pas de double rendu Prism identifié ; impact CPU à confirmer seulement si le mapping est dans le bloc actif | faible |
| `55cb142cd` — 2026-05-29 | suppression d’un `Strike` redondant dans l’ancien wrapper mono | plutôt une réduction locale ; ne relie pas le symptôme actuel au +3 % | faible |

Le commit `ccdffad31` est la première différence nette concernant le nombre
d’oscillateurs Prism. Le commit `bcfa7f0b7` est la différence nette concernant
le premier slot poly et son VCA/filtre propre. Aucun commit de l’historique
inspecté ne contient une mesure matérielle prouvant le passage exact de +1 % à
+3 %. Il faut donc comparer le parent de `bcfa7f0b7`, la branche mono, puis la
branche poly avec les mêmes paramètres ; il ne faut pas attribuer ce delta à
Braids sans cette comparaison.

Les changements output gain, clear et copie observables dans l’historique ne
montrent pas de double rendu ou de double sommation du même buffer. Le no-copy
mono évite au contraire une copie lorsque la réservation réussit. Le clear poly
est unique par track et bloc, non répété pour chaque voix.

## 5. Contrat d’amplitude Prism

### Trace Note On / Note Off

```text
Note On
  -> scheduler alloue un slot si poly
  -> mixer_track_poly_note_on() ou mixer_track_vca_note_on()
  -> brick6_braids_runtime_note_on()
  -> gate/trigger + velocity + niveaux OSC
  -> MacroOscillator::Render()
  -> instance->level et BRAIDS_OUTPUT_TRIM
  -> filtre et VCA mixer
  -> volume/pan puis sommation

Note Off
  -> gate Prism à 0
  -> tail_samples_remaining calculé depuis vca_release_s
  -> niveau interne décroît pendant que la source reste rendue
  -> VCA mixer passe en release
  -> fin du slot lorsque renderer et VCA sont idle
```

### Rôles distincts

Le niveau interne Prism n’est pas la copie de l’ADSR `env_adsr` du mixer :

* l’attaque utilise une rampe fixe vers `velocity_gain` ;
* le release utilise le coefficient fixe `0.995` ;
* `velocity_gain` encode la vélocité et le niveau de source ;
* le tail est une durée de maintien du renderer, dérivée du release pour éviter
  de couper la source avant que le VCA commun ait terminé ;
* le VCA mixer porte l’ADSR musicale track/poly et décide le gain de note externe.

Il existe donc deux multiplications d’amplitude, mais pas la preuve de deux ADSR
musicaux identiques. Le contrat actuel est :

1. Prism protège le démarrage/arrêt de sa source et son niveau de sortie ;
2. le mixer porte l’enveloppe musicale, la durée audible de release, puis le
   volume/pan de la lane.

Ce contrat est légitime si le niveau interne reste un smoothing/velocity/source
lifecycle. Il est ambigu parce que `vca_release_s` est aussi utilisé pour
calculer la durée du tail interne et parce que l’audit VCA existant le classe
comme `DOUBLE_VCA`. Il faut mesurer l’enveloppe temporelle et le gain résultant
avant d’envisager une simplification. Le lissage anti-clic ne doit pas être
supprimé sur la seule présence du VCA commun.

### Coût associé

En mono, le niveau interne ajoute une petite boucle par sample au renderer puis
le mixer ajoute la boucle VCA/volume/pan. En poly, cette boucle interne existe
par voix et `mixer_process_external_poly_voice()` ajoute le filtre et l’ADSR par
voix. Le premier slot poly ne doit donc pas être comparé au seul coût de
`MacroOscillator::Render`.

## 6. Contrat d’amplitude Drum

### MD / TRX-BD

Le modèle MD actif `TRX-BD` possède une synthèse de percussion autonome :

* `amplitude_env` définit le decay du corps ;
* `pitch_env` fait descendre la fréquence ;
* `transient_env` forme l’attaque ;
* `noise_env` forme le bruit ;
* deux phases LUT, RNG, drive/clipping et fade de retrigger produisent le sample.

Ces enveloppes font partie du son du kick et ne sont pas des duplications
accidentelles du VCA de track. `md_trx_bd_render()` les traite par sample et ne
fait pas de `powf` dans la boucle ; les conversions exponentielles sont dans la
préparation ou le Note On.

Le scheduler déclenche néanmoins le VCA commun pour une track Drum. Le Drum
ignore actuellement Note Off (`drum_synth_note_off_for_instance()` est un
no-op) et le shot interne se termine seul. Le VCA commun peut donc agir comme
gate/atténuateur externe pendant le shot, mais sa release doit être compatible
avec la durée de l’enveloppe interne. Il ne faut pas conclure « double VCA
erroné » uniquement parce que deux gains existent : l’enveloppe interne définit
le timbre et la décroissance du percussion engine ; le VCA externe définit la
politique de lane.

### BD analog

BD analog utilise son enveloppe interne Plaits/AnalogBassDrum et rend par chunks
de 8. Le VCA track reste une enveloppe de lane post-source. Le même risque de
troncature doit être mesuré, sans supprimer l’enveloppe interne.

### Décision de contrat

Le comportement à conserver avant décision produit est :

* enveloppes internes Drum : forme indispensable du son ;
* VCA commun : contrôle de lane/gain et cohérence mixer ;
* `note_off` Drum : ne doit pas être traité comme un release vocal classique
  tant que le moteur est un shot percussif.

Une éventuelle correction devra choisir explicitement entre VCA externe
optionnel, VCA externe toujours actif comme attenuation, ou bypass VCA Drum.
Cette décision est fonctionnelle et indépendante de l’optimisation CPU.

## 7. Protocole de mesure matériel

### Conditions constantes

* même build et même projet ; 48 kHz et bloc DMA de 64 frames ;
* effects master, inserts, sends, Hall et preview désactivés ;
* modulation et p-locks désactivés ;
* même note, vélocité, pitch, volume et filtre pour toutes les comparaisons ;
* aucun diagnostic track sélectionné pendant les mesures CPU, car les taps
  `audio_track_diag` ajoutent des boucles par sample ;
* stabilisation d’au moins 3 s ; trois acquisitions par configuration ;
* relever minimum, moyenne affichée, dernier échantillon et maximum récent si
  l’interface ou le runner les expose ; refaire après redémarrage puis après
  warm-up pour séparer cache froid et coût stable.

La valeur affichée est `cpu_load_get_avg_permille()` convertie en pourcentage.
Elle est lissée avec un filtre de 1/16 et arrondie par l’UI ; le maximum récent
porte sur une fenêtre de 16 blocs. Le compteur DWT existant mesure l’occupation
réelle entre entrée et sortie IRQ. Le runner audio existant réinitialise la
mesure, attend une phase d’attaque/stabilisation, puis capture les métriques ;
il peut être utilisé sans ajouter de profiler.

### Matrice de mesures

Pour chaque ligne, noter `avg`, `last`, `peak_recent`, `peak`, nombre de blocs et
le RMS/peak audio pour s’assurer que le son est comparable.

| ID | Configuration | But |
|---|---|---|
| B0 | toutes les tracks sonores Off | coût IRQ global pur |
| B1 | track liée, moteur sélectionné, sans note | scans, préparation et coût silencieux |
| B2 | même track, note mono, filtre OFF, VCA OFF/bypass si le setup le permet | coût moteur + lane minimal |
| B3 | note mono, VCA actif, filtre OFF | coût VCA mono isolé |
| B4 | note mono, filtre ON, VCA actif | coût filtre + VCA mono |
| B5 | Prism mono OSC0=1, OSC1=0 | premier Braids interne |
| B6 | Prism mono OSC0=1, OSC1=1 | surcoût OSC1 dans la même instance |
| B7 | Prism poly configuré à 2, une seule note | première voix poly + lane poly |
| B8 | Prism poly configuré à 2, deux notes tenues | surcoût seconde voix |
| B9 | Prism poly au maximum pertinent, notes tenues | pente par voix et slots alloués |
| B10 | Wave, Stack, DELUGE : mono puis poly 1/2/max | comparaison cross-engine |
| B11 | Stack : un slot interne actif, puis deux, puis trois, avec une voix | distinguer slot interne et voix poly |
| B12 | Wave : un puis deux oscillateurs, interpolation stable puis dynamique | coût table/interpolation |
| B13 | Drum MD TRX-BD : silence, attaque, shot actif, fin de shot | coût des enveloppes internes + VCA |
| B14 | Drum MD profils non TRX-BD | confirmer le silence des profils non rendus |
| B15 | Drum BD analog : silence et shot actif | comparer AnalogBassDrum |
| B16 | track sampler RAM liée sans voix puis avec une voix | mesurer le clear/rendu silencieux hors Stream/Multi |

À chaque moteur, refaire B1–B4 avec filtre OFF/ON, VCA inactif/actif et sends
OFF/ON uniquement après la série de référence. Pour les séries poly, noter
séparément `voice_count`, `render_voice_count`, slots HELD et slots RELEASE.
Comparer :

```text
Δtrack = B1 - B0
Δmono  = B2 - B1
ΔVCA   = B3 - B2
Δfilter = B4 - B3
Δvoice = B8 - B7
Δosc   = B6 - B5
```

Les deltas ne sont interprétables que si le niveau sonore, le statut de lane et
le nombre de blocs capturés sont identiques. Si `Δvoice` est proche de +1 % mais
`B7-B0` proche de +3 %, le symptôme est une marche de lane/poly, pas un Braids
trois fois plus cher. Si `Δosc` seul vaut +1 % et que B7 n’ajoute pas la marche,
la cause est interne Prism. Si les deux apparaissent uniquement filtre ON ou
VCA actif, la mesure précédente mélangeait ces étages.

### Instrumentation temporaire éventuelle

Aucune nouvelle instrumentation n’est nécessaire pour cette passe. Si le global
IRQ ne suffit pas, proposer uniquement une mesure temporaire et désactivable
autour de `brick6_braids_runtime_render_instance()`,
`mixer_process_external_poly_voice()` et `mixer_process()`, avec trois compteurs
de blocs : total, voix, oscillateurs actifs. Ne pas l’implémenter avant d’avoir
échoué à séparer le cas mono/poly avec la matrice ci-dessus.

## 8. Plan d’implémentation

Les actions sont volontairement séparées entre comportement, CPU et mesure.

### KEEP

* Garder le contrat de filtre/VCA post-source et le VCA par voix poly.
* Garder les retours silencieux anticipés des renderers et le skip des slots
  libres.
* Garder les deux niveaux Prism jusqu’à validation anti-clic, release et gain.
* Garder les enveloppes internes Drum ; elles sont constitutives du son.
* Garder le DWT Wave existant comme instrument ciblé et désactivable.

### MEASURE

* Reproduire B0–B16 avec le même état de filtre/VCA et séparer OSC0/OSC1 de
  voix 1/voix 2.
* Comparer le parent et `bcfa7f0b7` sur un scénario Prism poly identique.
* Mesurer l’effet de `a44b58dac` filtre OFF/ON/transition.
* Mesurer les slots RELEASE, les paramètres Prism en changement, et les branches
  Wave dynamique/Stack rampe/DELUGE niveau.
* Mesurer les tracks sampler silencieuses et les Drum silencieux avant toute
  optimisation de leurs clears.

### FIX — fonctionnel, après décision et tests

* Réparer séparément le routage des paramètres VCA communs signalé par
  `vca_engine_contract_audit.md` (`PARAM_NOT_APPLIED`) ; fichiers concernés :
  dispatcher paramètre et backend MIX. Ce n’est pas une optimisation CPU et ne
  doit pas être mélangé au diagnostic Prism.
* Formaliser le contrat Drum : release interne de shot, rôle du VCA track et
  comportement attendu d’un Note Off. Tester amplitude, durée et absence de
  troncature avant toute modification.
* Décider si les profils MD hors TRX-BD doivent être rendus ou explicitement
  désactivés ; tester chaque profil avant de toucher au routage.

Pour ces FIX, le gain CPU attendu n’est pas chiffrable et ne doit pas être le
critère principal ; le risque est une modification de durée, d’attaque ou de
niveau sonore. Tests requis : enveloppe mesurée, release, retrigger, mono/poly,
filtre OFF/ON et validation des paramètres UI/p-lock.

| FIX | Cause / fichiers | Gain attendu | Risque | Tests et ordre |
|---|---|---|---|---|
| Routage VCA | dispatcher paramètre et backend MIX, comme documenté par l’audit VCA | CPU : aucun objectif ; correction fonctionnelle | VCA UI/p-lock différent du comportement actuel | d’abord query/encodeur, puis p-lock, snapshot, modulation et audio mono/poly |
| Contrat Drum | `Src/Audio/drum_synth.cpp`, scheduler/mixer ; shot interne et VCA lane non alignés par une règle explicite | à mesurer ; objectif fonctionnel | release tronqué ou niveau modifié | mesurer enveloppe interne, VCA, Note Off, retrigger ; décision produit avant code |
| Profils MD | `drum_synth_note_on()` / `drum_synth_process_block_for_instance()` ne rendent actuellement que TRX-BD | aucun objectif CPU ; rendre ou déclarer les profils | changement sonore par modèle | test modèle par modèle, silence attendu ou rendu de référence, puis validation catalogue |

### OPTIMIZE — seulement après preuve de coût

* Si la mesure confirme un coût significatif, réduire les clears/copies de
  buffers silencieux des lanes qui ne publient aucun son, sans changer le
  contrat d’effacement du mixer.
* Si la mesure le confirme, éviter les recopiages de configuration poly lorsque
  la configuration n’a pas changé ; vérifier la synchronisation du filtre/VCA et
  le release de chaque slot.
* Si le test confirme une pente anormale, optimiser les branches de transition
  Wave/Stack/DELUGE ou les paramètres par paquet Prism, jamais le rendu actif
  sans comparaison audio.

Le gain est « à mesurer » pour chaque candidat ; aucun pourcentage n’est
estimé statiquement. Tests requis : CPU avg/peak/recent, comparaison audio,
note on/off, retrigger, max voices et cache chaud/froid.

| OPTIMIZE | Cause / fichiers | Gain attendu | Risque | Tests et ordre |
|---|---|---|---|---|
| Clears silencieux | `brick6_audio_runtime.c` et buffers externes sampler/lanes | à mesurer uniquement si B1 montre un coût | buffer résiduel ou lane fantôme | d’abord B0/B1, puis silence bit-à-bit et note suivante |
| Recopie poly | `mixer_poly_filter_sync_config()` dans `Src/Audio/mixer.c` | à mesurer sur B7–B9 | config/VCA poly désynchronisé | comparer config inchangée/modifiée, release et max voices |
| Transitions moteur | branches rampes Wave/Stack/DELUGE et paquets Prism | à mesurer par mode | clic, phase ou niveau différents | DWT Wave existant, CPU global, AB audio, puis seulement patch ciblé |

### DEFER

* toute modification du scheduler, de l’allocation globale, du pan, du Hall,
  du master FX ou de l’infrastructure dual-core ;
* Stream et Sampler Multi ;
* remplacement du filtre ou suppression de l’ADSR commun ;
* suppression du smoothing/tail Prism ou d’une enveloppe Drum sans preuve
  fonctionnelle ;
* conclusion sur une régression historique tant que la matrice matérielle ne
  sépare pas la première lane, la première voix et le second oscillateur.

## Conclusion opérationnelle

Le +3 % initial doit être traité comme un coût agrégé de première activation
jusqu’à preuve contraire. Le +1 % suivant est compatible avec une voix poly ou
un oscillateur supplémentaire, mais le code seul ne permet pas de choisir entre
ces deux lectures de « second ». Prism comporte un niveau interne de lifecycle
et un VCA musical de mixer ; Drum comporte des enveloppes de synthèse internes
et un VCA de lane. Dans les deux cas, la coexistence est actuellement défendable
mais le contrat doit être mesuré avant toute suppression.
