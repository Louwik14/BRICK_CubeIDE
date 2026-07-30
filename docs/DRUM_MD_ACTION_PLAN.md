# Plan d'action — moteur `DRUM / MD`

Passe de conception uniquement. Les deux mémoires locales ont été lus intégralement. Cette version ne couvre que `TRX-BD`, `TRX-SD`, `TRX-CH`, `EFM-BD`, `EFM-SD` et `EFM-CB`; tous les autres modèles sont explicitement reportés.

## 1. Verdict

- Faisabilité élevée avec un moteur unique `DRUM / MD`, une sélection `MODEL`, huit slots dynamiques au maximum et des rendus spécialisés par modèle.
- La fidélité réaliste est élevée pour les structures DSP, les rapports et les enveloppes décrits dans les mémoires; le mapping exact 0–127 reste une approximation à écouter.
- Risques : transitoires TRX non fournis, courbes commerciales incomplètes, échelle du feedback EFM, signification exacte de `GAP`/`MTAL`, et migration du registre dense de paramètres.
- Recommandation : Option B hybride (petites fonctions spécialisées + primitives communes), coefficients préparés hors boucle sample, aucun Plaits, aucune allocation ni lecture SD dans l'audio.
- Les enveloppes pitch, amplitude, bruit et index FM appartiennent aux moteurs MD. Elles sont indépendantes de `ENV FLT`, `ENV VCA` et `ENV3`. Les filtres MD sont internes; le filtre global de track reste appliqué après le moteur.

## 2. Sources exploitées

| source | contenu utile | confiance | licence/risque |
|---|---|---:|---|
| `docs/trx_machinedrum.pdf` — David Möllerstedt, Chalmers EX064/2004 | Architecture TRX, mono/deux tons/métallique, BD/SD/CH, LUT 4096, enveloppes, filtres, retrigger, assembleur DSP56303 | Très élevée pour le prototype décrit; moyenne pour le produit final | Mémoire universitaire; réimplémentation indépendante |
| `docs/efm_machinedrum.pdf` — Erik Larsson, Chalmers EX048/2000 | Équations FM/PM, phases, feedback, index/pitch/amplitude envelopes, BD/SD/CB, HPF, PRNG, rapports et contraintes DSP | Très élevée pour le prototype; moyenne à élevée pour les courbes OS | Mémoire universitaire; aucun code tiers repris |
| [Elektron Machinedrum User's Manual OS 1.63](https://www.elektron.se/wp-content/uploads/2024/09/machinedrum_manual_OS1.63.pdf) | Noms, ordre et intention des paramètres commerciaux TRX/EFM | Très élevée | Copyright Elektron; noms/faits seulement |
| [Elektronauts — MD Voices diagram](https://www.elektronauts.com/t/md-voices-diagram/173460) et [page 2](https://www.elektronauts.com/t/md-voices-diagram/173460?page=2) | Provenance des scans Chalmers et incertitudes de traduction | Élevée pour la provenance, moyenne pour les interprétations | Forum; aucune spécification normative |
| [MAME `elektronmono.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/elektron/elektronmono.cpp), R. Belmont | Coldfire + 2 DSP56303, synthèse/FX, mémoire; driver sans son | Élevée matériel, nulle algorithme | BSD-3-Clause; ne rien copier |
| [Machinedrum Data Tool](https://github.com/waftlord/mddt), waftlord | SysEx et métadonnées utiles à une future vérification | Moyenne à élevée | Apache-2.0; pas de synthèse |
| [TBD-16 FM Kick](https://docs.dadamachines.com/tbd-16/machines/fm-kick) | Comparaison FM moderne, utile pour exclure Plaits/CTAG du chemin MD | Faible pour MD | GPL/MIT de dépendances; aucune reprise |
| Code BRICK inspecté | Autorités runtime, audio 64 frames/48 kHz, paramètres, UI, p-locks et persistance | Très élevée | Code local; arbre de travail préexistant conservé |

## 3. Architecture actuelle du dépôt

- `Inc/UI/ui_core.h`, `Src/UI/ui_track_catalog.c` : la famille `Drum` expose actuellement `TRX BD` et `BD Analog`.
- `Inc/Core/track_runtime.h`, `Src/Core/track_runtime.c` : binding UI→runtime, instance stable par track, slots TONE et cible mixer explicite.
- `Src/Core/brick6_audio_runtime.c` : `brick6_map_runtime_type_to_drum_model()` puis `brick6_render_synth_tracks()` rendent un buffer mono par track logique.
- `Inc/Audio/drum_synth.h`, `Src/Audio/drum_synth.cpp` : API init/model/note/process/set-param; seul `BD_ANALOG` rend aujourd'hui via Plaits, le placeholder `TRX_BD` est silencieux. Le futur MD doit remplacer ce chemin sans créer d'autorité parallèle.
- `Src/Seq/seq_play_scheduler.c`, `Src/Keyboard/keyboard_engine.c`, `Src/Seq/seq_output_guard.c` : note-on, vélocité, one-shot et panic/all-notes-off.
- `Inc/Audio/audio_float.h`, `Inc/Board/board_audio_format.h`, `Src/Audio/audio.c` : 48 kHz, `AUDIO_BLOCK_SIZE == 64`, appels éventuellement segmentés aux événements; l'état MD doit survivre aux segments.
- `Inc/Param/param_store.h`, `Src/Param/param_registry.c`, `Src/Param/param_registry_backends.c`, `Src/Core/track_tone_sound_state.*` : huit IDs TRX existants réutilisés par `BD_ANALOG`, collision à résoudre avec des slots MD génériques.
- `Src/UI/pages/ui_page_template_tone.c` : précédent de pages dynamiques par famille/type; `TRX_BD` tombe actuellement sur une page vide.
- `Src/Mod/mod_destination_catalog.c` : destinations contextuelles; MD devra exclure `MODEL` de la modulation et exposer seulement les slots du modèle courant.
- `Src/Seq/seq_boundary_engine.c` : les p-locks sont `(set_id,param_slot,value16)` et appliqués dans l'ordre collecté. Le plan impose une pré-passe `MODEL`, puis les slots.
- `Inc/Storage/pattern_live_ram.h`, `Src/Storage/pattern_live_ram.c`, `pattern_sd_bank.c`, `project_v1.*`, `patch_v1.c`, `kit_v1.c`, `undo_v2.c`, `track_snapshot.c` : matrice dense indexée par `PARAM_COUNT`; le format est explicitement prototype.
- `Src/Core/cpu_load.c` et `Src/Core/brick6_wave_runtime.c` fournissent une instrumentation existante; son emploi reste facultatif et manuel.

## 4. Ce que révèlent réellement les mémoires

### TRX

**Certain.** Le DSP original traite par blocs de 32 samples. Les familles sont mono sinus + attaque/pitch envelope, deux tons à intervalle maintenu, et six carrés métalliques filtrés. `TRX-BD` suit `PTCH DEC RAMP RDEC STRT NOIS HARM CLIP`; sinus principal, transitoire/bruit, pitch sweep, enveloppe d'amplitude, harmoniques et clipping. `TRX-SD` suit `PTCH DEC BUMP BENV SNAP TONE TUNE CLIP`; deux oscillateurs conservent leur ratio sous transposition, avec bruit et saturation. `TRX-CH` utilise six carrés, LPF 24 dB/octave, deux HPF 12 dB/octave dont un fixe, et `GAP DEC HPF LPF MTAL`. La LUT sinus TRX du mémoire est 4096 mots 24 bits. Le retrigger redémarre la phase et applique un fade très court à l'ancienne queue.

**Déduit.** Les périodes 114, 102, 86, 80, 58, 52 à 44,1 kHz donnent environ 387, 432, 513, 551, 760 et 848 Hz; conserver les périodes comme constantes et considérer les commentaires OCR divergents comme incertains. L'aliasing des carrés peut être constitutif; aucune band-limitation par défaut.

**Inconnu.** Tables exactes d'attaque, courbes 0–127 et coefficients commerciaux; rôle précis de `GAP` et `MTAL`. Ces points seront exposés comme approximations et non comme fidélité revendiquée.

### EFM

**Certain.** `x(t)=A sin(αt + I sin(βt))`; en vraie FM, `ωinst=α+Iβ cos(βt)`. La phase modifie les interférences des bandes latérales. Un opérateur contient porteuse, modulateur, feedback, enveloppes exponentielles d'amplitude/index/pitch; le ratio reste lié au pitch. `EFM-BD` est le noyau à deux opérateurs; `EFM-SD` ajoute bruit et HPF; `EFM-CB` utilise deux paires et le ratio de branche `1,48`. Le feedback doit être borné avant la zone chaotique non déterministe. Le HPF léger suit `y[n]=x[n]-x[n-1]+a y[n-1]`. Le PRNG mémoire est Fibonacci additif à deux mots, graine non nulle.

**Déduit.** La LUT EFM décrite (32768 mots 24 bits, sans interpolation) peut être remplacée sur M7 par une LUT native 1024/2048 avec interpolation si l'écoute ne révèle pas de différence; le choix sera manuel. La surface OS prime sur les variables internes supplémentaires du mémoire.

**Reporté.** Tous les modèles hors périmètre de cette première version (`TRX-XT`, `TRX-CP`, `TRX-RS`, `TRX-CB`, `TRX-OH`, `TRX-CY`, `TRX-MA`, `TRX-CL`, `TRX-XC`, `TRX-B2`, `EFM-XT`, `EFM-CP`, `EFM-RS`, `EFM-HH`, `EFM-CY`, et autres familles) ne sont pas planifiés ici.

## 5. Catalogue des modèles

| MODEL | famille | primitives | paramètres commerciaux dans l'ordre | confiance | coût prévu | priorité |
|---|---|---|---|---|---|---:|
| TRX-BD | TRX mono | sinus, transient, pitch/amp env, bruit, harmoniques, clip | PTCH DEC RAMP RDEC STRT NOIS HARM CLIP | A structure / B courbes | faible | 1 |
| TRX-SD | TRX deux tons | 2 sinus, ratio, bruit, env, clip | PTCH DEC BUMP BENV SNAP TONE TUNE CLIP | A structure / B courbes | faible-moyen | 2 |
| TRX-CH | TRX métallique | 6 carrés, LPF, 2 HPF, env | GAP DEC HPF LPF MTAL | A structure / B GAP | moyen | 3 |
| EFM-BD | EFM simple | 2 opérateurs, index, pitch/amp env, feedback mod | PTCH DEC RAMP RDEC MOD MFRQ MDEC MFB | A structure / B feedback | moyen | 4 |
| EFM-SD | EFM + bruit | EFM-BD, bruit, HPF bruit/final | PTCH DEC NOISE NDEC MOD MFRQ MDEC HPF | A structure / B courbes | moyen | 5 |
| EFM-CB | EFM deux branches | 2 paires FM, ratio 1,48, double enveloppe, feedback | PTCH DEC SNAP FB MOD MFRQ MDEC | A structure / B courbes | moyen-haut | 6 |

## 6. Architecture DSP proposée

- **État commun :** modèle validé, note/vélocité, active, compteur de retrigger, RNG propre à la track, dirty mask et niveau.
- **État persistant :** `MODEL` + `P1..P8` normalisés; un seul jeu de slots, sans banque cachée par modèle. Les profils fournissent labels, défauts, plages et masque de modulation.
- **État runtime :** union compacte `trx_mono`, `trx_dual`, `trx_metal`, `efm_single`, `efm_dual`; coefficients préparés au changement de paramètre ou au début de bloc dirty. Les enveloppes internes ne touchent jamais `ENV FLT`, `ENV VCA` ou `ENV3`.
- **Rendu :** fonction spécialisée choisie hors boucle sample. Les primitives partagées sont phase accumulator, sinus léger, enveloppes internes, bruit, HPF/LPF léger, clipping, mixage et retrigger fade.
- **Filtres :** HPF/LPF propres au modèle; le buffer MD est ensuite soumis au filtre global de track existant.
- **Numérique :** Q32 pour la phase, LUT native int16 1024/2048/4096 et interpolation seulement si nécessaire; float pour coefficients/gains. Carrés métalliques par bit de phase afin de préserver l'aliasing constitutif.
- **MODEL :** valider, couper/dé-clicker brièvement, zéroter l'état incompatible, resemer le RNG, charger les défauts et préparer les coefficients; premier trigger déterministe. `MODEL` est p-lockable mais non modulable.

Option A (noyau générique profilé) réduit d'abord le code mais introduit des branches et états inutiles par sample. Option B (fonctions spécialisées avec profils hors boucle) garde un worst-case lisible, une union RAM compacte et des tests isolés. **Option B hybride est retenue.**

## 7. Mapping des paramètres

Convention initiale : `u=value/127`, `expmap(a,b,u)=a·(b/a)^u`, `decay=exp(-1/(t·48000))`; toutes les exponentielles/divisions sont préparées hors boucle.

### TRX-BD

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| MODEL | sélection | enum validé | six modèles | oui, premier | de-click |
| PTCH | fondamental | `expmap(25,300,u)` transposé par note | 20–1200 Hz | oui | courbe exacte |
| DEC | amplitude | `expmap(.03,4,u)` | 30 ms–4 s | oui | durée finale |
| RAMP | chute pitch | `0..72u` st, fréquence initiale puis retour | 0–72 st | oui | signe exact |
| RDEC | durée chute | `expmap(.002,.8,u)` | 2–800 ms | oui | courbe |
| STRT | attaque | impulsion/transient façonné `u` | 0–1 | oui | table absente |
| NOIS | bruit d'attaque | gain `u²`, enveloppe courte | 0–1 | oui | durée |
| HARM | harmoniques | mix 2e harmonique/waveshape borné | 0–1 | oui | fonction exacte |
| CLIP | saturation | pregain `2^(4u)` puis clip borné | 1–16× | oui | courbe |

### TRX-SD

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| PTCH | base deux tons | `expmap(70,420,u)` | 40–1600 Hz | oui | courbe |
| DEC | amplitude | `expmap(.025,2.5,u)` | 25 ms–2,5 s | oui | env partagées |
| BUMP | pitch initial | `0..48u` st | 0–48 st | oui | signe |
| BENV | durée bump | `expmap(.002,.25,u)` | 2–250 ms | oui | — |
| SNAP | transient/bruit | gain `u²` court | 0–1 | oui | rôle exact |
| TONE | balance tonale | crossfade deux tons/bruit | 0–1 | oui | rôle commercial |
| TUNE | intervalle | `2^((3+21u)/12)` | 3–24 st | oui | plage |
| CLIP | saturation | clip somme borné | 1–16× | oui | — |

### TRX-CH

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| GAP | écart six carrés | morph des périodes originales, ±25 % | rapports bornés | oui | fonction exacte |
| DEC | amplitude | `expmap(.008,1.5,u)` | 8 ms–1,5 s | oui | CH/OH |
| HPF | HPF variable | `expmap(200,14000,u)` | 200 Hz–14 kHz | oui | courbe |
| LPF | LPF interne | `expmap(1200,20000,u)` | 1,2–20 kHz | oui | résonance |
| MTAL | couleur métal | mix carré/bruit/densité | 0–1 | oui | intention exacte |

### EFM-BD

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| PTCH | porteuse | `expmap(20,300,u)` transposé | 15–1200 Hz | oui | courbe |
| DEC | amplitude | `expmap(.025,4,u)` | 25 ms–4 s | oui | — |
| RAMP | pitch env | `0..72u` st | 0–72 st | oui | — |
| RDEC | pitch decay | `expmap(.002,.8,u)` | 2–800 ms | oui | — |
| MOD | index | `12u²` borné | 0–12 | oui | unité FM/PM |
| MFRQ | ratio | `0.25·2^(4u)` | 0,25–4 | oui | continu/quantifié |
| MDEC | index decay | `expmap(.002,2,u)` | 2 ms–2 s | oui | — |
| MFB | feedback mod | `gmax·u²` borné | limite stable | oui | échelle |

### EFM-SD

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| PTCH | porteuse | `expmap(70,500,u)` | 40–2000 Hz | oui | — |
| DEC | amplitude | `expmap(.02,2.5,u)` | 20 ms–2,5 s | oui | — |
| NOISE | bruit | gain `u²` | 0–1 | oui | PRNG |
| NDEC | decay bruit | `expmap(.003,1.5,u)` | 3 ms–1,5 s | oui | — |
| MOD | index | `12u²` | 0–12 | oui | — |
| MFRQ | ratio | `0.25·2^(5u)` | 0,25–8 | oui | — |
| MDEC | index decay | `expmap(.002,1.5,u)` | 2 ms–1,5 s | oui | — |
| HPF | filtre modèle | `expmap(80,14000,u)` | 80 Hz–14 kHz | oui | HPF bruit fixe |

### EFM-CB

| paramètre MD | rôle | mapping initial | plage interne | p-lock | incertitude |
|---|---|---|---|---|---|
| PTCH | branche A | `expmap(100,1500,u)`, B=`1,48A` | 80–6000 Hz | oui | plage |
| DEC | enveloppe lente | `expmap(.02,3,u)` | 20 ms–3 s | oui | — |
| SNAP | poids rapide | crossfade rapide/lente | 0–1 | oui | courbe |
| FB | feedback porteuses | `gmax·u²` borné | limite stable | oui | chaos |
| MOD | index commun | `10u²` | 0–10 | oui | — |
| MFRQ | ratio mod | `0.5·2^(3u)` | 0,5–4 | oui | — |
| MDEC | index decay | `expmap(.002,1,u)` | 2 ms–1 s | oui | — |

## 8. Budget CPU et mémoire

Aucun budget CPU chiffré n'est imposé dans ce plan : les mesures et arbitrages seront faits manuellement par l'utilisateur. Le code doit néanmoins rester déterministe, sans allocation, sans SD et sans calcul lourd inutile dans la boucle sample.

- RAM cible de conception : état commun + union + coefficients ≤ 512 octets par instance, LUT partagée hors instance. À confirmer par `sizeof` après intégration.
- Vérification manuelle après chaque modèle : écouter le silence, l'attaque, le decay, les extrêmes de feedback/filtre et plusieurs tracks simultanées; relever le comportement CPU avec les outils déjà présents si souhaité.
- Si le coût ou le caractère ne convient pas : déplacer un calcul au changement de paramètre/bloc, réduire la LUT/interpolation, simplifier le filtre ou spécialiser davantage. Ne pas introduire de moteur générique ou de Plaits.

## 9. Plan d'action numéroté

Chaque étape est isolée, compile autant que possible, et ne demande à l'utilisateur que l'écoute et/ou la mesure manuelle indiquée.

### Étape 1 — Intégration `DRUM / MD`

- **objectif :** remplacer le placeholder `TRX BD` par le type unique MD, sans implémenter de son.
- **fichiers probables :** `Inc/UI/ui_core.h`, `Src/UI/ui_track_catalog.c`, `Inc/Core/track_runtime.h`, `Src/Core/track_runtime.c`, `Inc/Audio/drum_model_ids.h`, `Inc/Audio/drum_synth.h`, `Src/Audio/drum_synth.cpp`, `Src/Core/brick6_audio_runtime.c`.
- **modifications :** type `DRUM/MD`, `DRUM_MODEL_ID_MD`, binding stable, API compatible, aucun chemin Plaits pour MD; conserver `BD_ANALOG`.
- **tests :** compilation; sélection UI/runtime; silence MD; vérification que les autres tracks/moteurs restent inchangés.
- **à écouter/mesurer manuellement :** rien de sonore; vérifier seulement que `MD` se sélectionne et reste silencieux.
- **critère de réussite :** MD routé et compilable, sans seconde autorité runtime.
- **critère d'arrêt :** collision avec `BD_ANALOG` ou binding non déterministe.
- **dépendances :** aucune.

### Étape 2 — UI dynamique, p-locks et changement de modèle

- **objectif :** installer `MODEL + P1..P8`, labels dynamiques et ordre modèle→slots.
- **fichiers probables :** `param_store.h`, `param_registry*.c`, `track_tone_sound_state.*`, `ui_page_template_tone.c`, `mod_destination_catalog.c`, `seq_boundary_engine.c`, fichiers Storage parcourant `PARAM_COUNT`.
- **modifications :** slots génériques persistants; profils de paramètres par modèle; `MODEL` p-lockable mais non modulable; restauration et application déterministes; changement de modèle réinitialisant l'état incompatible et chargeant les défauts.
- **tests :** compilation; changement de modèle sans son; p-locks dans un ordre aléatoire puis vérification du modèle appliqué avant les slots; save/load/undo des slots.
- **à écouter/mesurer manuellement :** aucun rendu requis; contrôler visuellement les labels et l'absence de pic/état résiduel si un trigger de test est disponible.
- **critère de réussite :** surface dynamique sûre, aucun ancien filtre/feedback/NaN au premier trigger.
- **critère d'arrêt :** ordre p-lock ou format persistant ambigu.
- **dépendances :** étape 1.

### Étape 3 — Primitives MD légères

- **objectif :** ajouter uniquement les primitives partagées, sans modèle publié.
- **fichiers probables :** nouveaux `Inc/Audio/md_dsp.h`, `Src/Audio/md_dsp.c`, intégration build minimale.
- **modifications :** phase Q32, LUT native, sinus, enveloppes internes pitch/amplitude/bruit/index, PRNG par voix, HPF/LPF léger, clipping, mixage et retrigger fade; coefficients hors boucle.
- **tests :** compilation et tests unitaires légers des bornes, phase, silence et fin d'enveloppe.
- **à écouter/mesurer manuellement :** choisir une LUT 1024/2048/4096 et interpolation; comparer le bruit xorshift/Fibonacci à l'écoute et relever le coût si désiré.
- **critère de réussite :** API sans allocation, états bornés, aucune dépendance Plaits/Deluge.
- **critère d'arrêt :** conversion répétée float/int, calcul transcendant par sample ou état partagé entre tracks.
- **dépendances :** étape 2.

### Étape 4 — `TRX-BD`

- **objectif :** sinus, attaque, pitch envelope, amplitude envelope, bruit, harmoniques et clipping.
- **fichiers probables :** `drum_synth.cpp`, nouveaux rendus/états MD, profil et labels.
- **modifications :** rendu spécialisé et huit mappings du tableau 7; phase initiale et retrigger fade déterministes.
- **tests :** compilation; triggers, retrigger, valeurs min/max et changement de modèle.
- **à écouter/mesurer manuellement :** impact d'attaque, chute de pitch, longueur de decay, bruit, harmoniques et clipping; CPU si souhaité.
- **critère de réussite :** kick musical, borné, silencieux après decay et p-lockable.
- **critère d'arrêt :** attaque non déterministe, clic incontrôlé ou état non fini.
- **dépendances :** étape 3.

### Étape 5 — `TRX-SD`

- **objectif :** deux tons à ratio invariant, bump, bruit et saturation.
- **fichiers probables :** rendu/état TRX dual, profils et page dynamique.
- **modifications :** `BUMP/BENV`, `SNAP`, balance `TONE`, intervalle `TUNE`, enveloppes internes indépendantes des enveloppes de track.
- **tests :** compilation; note basse/centrale/haute, p-locks et retrigger.
- **à écouter/mesurer manuellement :** invariance du rapport, balance tonale, snap, bump, durée et saturation.
- **critère de réussite :** snare cohérente sur toute la tessiture et sans queue étrangère.
- **critère d'arrêt :** second ton ou bruit corrélé/instable.
- **dépendances :** étape 4.

### Étape 6 — `TRX-CH`

- **objectif :** six carrés, aliasing volontaire, HPF/LPF internes et decay métallique.
- **fichiers probables :** rendu/état `trx_metal`, profils et paramètres.
- **modifications :** périodes originales comme constantes, `GAP`, `MTAL`, HPF fixe + variable, LPF interne; filtre global de track reste en aval.
- **tests :** compilation; triggers rapprochés, decay court/long, paramètres aux bornes.
- **à écouter/mesurer manuellement :** vérifier que l'aliasing donne le caractère attendu, que `GAP/MTAL` sont musicaux et que les filtres internes ne remplacent pas le filtre global.
- **critère de réussite :** hi-hat métallique stable, sans DC ni queue infinie.
- **critère d'arrêt :** coût ou filtrage incompatible avec le caractère; revenir à la version carrée la plus simple.
- **dépendances :** étapes 3–4.

### Étape 7 — `EFM-BD`

- **objectif :** opérateur EFM, pitch/index/amplitude envelopes, phase et feedback mod.
- **fichiers probables :** état/rendu `efm_single`, profils et paramètres.
- **modifications :** implémenter la variante retenue FM/PM, phases initiales `π/2`, ratio `MFRQ`, index `MOD`, feedback `MFB`, coefficients hors boucle.
- **tests :** compilation; retrigger, bornes index/feedback, changement de modèle.
- **à écouter/mesurer manuellement :** attaque, pitch sweep, bandes latérales, évolution de brillance et feedback jusqu'à la limite stable.
- **critère de réussite :** kick EFM musical, stable et reproductible.
- **critère d'arrêt :** DC, pitch drift ou explosion au feedback maximal.
- **dépendances :** étape 3.

### Étape 8 — `EFM-SD`

- **objectif :** ajouter bruit par voix et les HPF internes au noyau EFM.
- **fichiers probables :** extension `efm_single`, profil SD et labels.
- **modifications :** `NOISE/NDEC`, HPF fixe du bruit, HPF final `HPF`; ne pas utiliser `ENV FLT/VCA/ENV3`.
- **tests :** compilation; retrigger rapide, plusieurs tracks et paramètres extrêmes.
- **à écouter/mesurer manuellement :** décorrélation du bruit, attaque snare, durée bruit/tonalité et interaction du HPF interne avec le filtre global aval.
- **critère de réussite :** snare EFM distincte, queue propre, aucune corrélation audible entre tracks.
- **critère d'arrêt :** bruit partagé ou filtre interne qui modifie le routage global.
- **dépendances :** étape 7.

### Étape 9 — `EFM-CB`

- **objectif :** deux paires FM, ratio de branche `1,48`, double enveloppe et feedback.
- **fichiers probables :** état/rendu `efm_dual`, profil CB et UI dynamique.
- **modifications :** branche rapide/lente contrôlée par `SNAP`, ratio fixe de branche, `FB`, `MOD`, `MFRQ`, `MDEC`.
- **tests :** compilation; triggers simultanés, p-locks, extrêmes et reset `MODEL`.
- **à écouter/mesurer manuellement :** attaque cowbell, rapport tonal, équilibre rapide/lent, feedback et disparition après decay.
- **critère de réussite :** cowbell EFM stable et cohérente avec les primitives validées.
- **critère d'arrêt :** branche supplémentaire inutile ou feedback hors bornes.
- **dépendances :** étape 8.

### Étape 10 — Stabilisation et documentation

- **objectif :** fermer la version limitée aux six modèles et documenter le réel.
- **fichiers probables :** `docs/architecture/z1_audio_hard_rt_mix.md`, `z2_track_runtime_authority.md`, `z3_param_modulation_control.md`, `z5_ui_navigation_interaction.md`, `z6_state_persistence_patterns_projects.md`, `ARCHITECTURE_GLOBAL.md`, `readme.md` si surface produit visible.
- **modifications :** préciser moteur unique MD, slots, enveloppes internes, filtres internes/filtre global aval, ordre p-lock, limites et modèles reportés.
- **tests :** compilation complète; non-régression manuelle des autres moteurs, save/load, UI et séquence.
- **à écouter/mesurer manuellement :** les six modèles, extrêmes, retriggers, p-locks, changements de modèle et plusieurs tracks; relever CPU/qualité selon vos propres outils.
- **critère de réussite :** dépôt compilable, six modèles sélectionnables et documentation sans promesse sur les modèles reportés.
- **critère d'arrêt :** divergence code/doc ou modèle partiellement exposé.
- **dépendances :** étapes 1–9.

## 10. Ordre recommandé des premiers modèles

1. `TRX-BD` : valide sinus, attaque, pitch/amplitude envelopes, bruit, harmoniques et clip.
2. `TRX-SD` : ajoute deux tons, ratio invariant, balance et bump.
3. `TRX-CH` : ajoute six carrés, aliasing et filtres internes.
4. `EFM-BD` : ajoute opérateur, index, phase, feedback et pitch envelope FM.
5. `EFM-SD` : ajoute bruit indépendant et HPF modèle.
6. `EFM-CB` : ajoute deux branches, ratio `1,48` et double enveloppe.

Cet ordre réutilise chaque primitive après validation manuelle et s'arrête exactement au périmètre demandé. Tous les autres modèles restent reportés.

## 11. Questions ouvertes et décisions proposées

| incertitude | décision par défaut | vérification manuelle ultérieure |
|---|---|---|
| FM vraie ou PM pour EFM-BD/SD/CB | choisir la forme la moins coûteuse qui conserve l'attaque et les bandes latérales du mémoire | écouter les deux variantes sur notes et feedback extrêmes |
| LUT | 1024 int16 interpolée comme premier choix | comparer à 2048/4096 à l'écoute |
| PRNG | Fibonacci additif par voix si simple; xorshift32 si l'implémentation existante est moins coûteuse | écouter corrélation et attaques sur plusieurs tracks |
| mémoire des slots lors de MODEL | un seul jeu de slots, réinitialisé aux défauts du nouveau modèle | changer de modèle, revenir, vérifier le premier trigger |
| `GAP`/`MTAL` | morph borné des périodes et de la densité métal; documenter comme approximation | écouter sweeps lents et p-locks |
| feedback EFM | limite fixe sûre, sans rechercher le chaos instable | pousser `MFB/FB` au maximum et écouter DC/accrochage |
| transitoire TRX | impulsion/bruit façonné très court, aucune table SD | écouter `STRT/SNAP` et vérifier absence de clic |
| format persistant | conserver les slots génériques; bump/reset du format prototype si `PARAM_COUNT` change | save/load et undo sur branche de test |
| modèle modulable | `MODEL` p-lockable, jamais destination LFO | vérifier que la liste MOD l'exclut |
| accent | vélocité pilote amplitude et légère attaque/index; aucun paramètre accent supplémentaire | comparer vélocités faibles/fortes |

## 12. Fichiers et dépendances hors zone

- Runtime : `ui_core.h`, `ui_track_catalog.c`, `track_runtime.h/.c`, `brick6_audio_runtime.c`.
- Paramètres/état : `param_store.h`, `param_registry*.c`, `track_tone_sound_state.*`, `mod_destination_catalog.c`.
- Séquence/UI : `seq_boundary_engine.c`, interfaces p-lock, `ui_page_template_tone.c`, labels et clipboard si nécessaire.
- Persistance : `pattern_live_ram.*`, `pattern_sd_bank.c`, `project_v1.*`, `patch_v1.c`, `kit_v1.c`, `undo_v2.c`, `track_snapshot.c`.
- Audio/build : `drum_synth.*`, `drum_model_ids.h`, fichiers de primitives MD et CMake; le mixer reste consommateur, pas autorité MD.
- Documentation : `z1`, `z2`, `z3`, `z5`, `z6`, `ARCHITECTURE_GLOBAL.md`, `readme.md` selon l'étape 10.
- Dépendances interdites : Plaits, copie Deluge pour la LUT MD, accès SD/allocations dans l'audio, moteur générique surdimensionné.
