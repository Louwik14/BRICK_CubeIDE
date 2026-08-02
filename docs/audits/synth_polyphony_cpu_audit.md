# Audit CPU et mémoire de la polyphonie synthétique

Audit en lecture seule du HEAD `8847c2e43`, centré sur Prism et limité aux moteurs synthétiques Prism, Stack, Wave et DELUGE. Les pourcentages cités sont les mesures matérielles fournies ; aucune mesure DWT nouvelle n'a été ajoutée.

## 1. Verdict

La régression a deux composantes distinctes.

1. Le `+2 %` d'une track Prism silencieuse correspond d'abord à l'activation globale du lane matériel moteurs `tracks[3]`, pas à Braids : toute présence d'une famille Synth/Sampler/Drum appelle `track_enable(3, 1)` (`Src/UI/ui_core_runtime_bridge.c:1053-1073`). `mixer_build_lane_plan()` considère alors le lane actif même sans publication externe (`Src/Audio/mixer.c:1126-1194`) et `mixer_process()` exécute son chemin stéréo, sa boucle gain/pan/mute et son routage sur `frames` samples (`Src/Audio/mixer.c:3230-3430`). C'est un coût fixe de lane, commun aux moteurs, présent aussi avec `VOICES=1` et sans note.
2. En mode réellement polyphonique (`render_voice_count > 1`), les quatre moteurs effacent L/R avant de savoir si une voix est rendable, puis inspectent le nombre de voix configuré. Prism ajoute ensuite, par voix active ou en release, une synchronisation complète de l'instance, un rendu Braids, une synchronisation filtre/VCA et une boucle VCA/sommation par sample (`Src/Core/brick6_audio_runtime.c:248-274`, `Src/Audio/mixer.c:2938-2973`). Les slots FREE sont bien exclus avant ces traitements coûteux, mais trop tard pour éviter le clear et le scan du lane.

Le maintien à `~6 %` après Track Off n'est **pas expliqué par un slot poly restant actif dans le chemin normal du HEAD**. Le cleanup courant est explicite et complet : le passage hors Synth/Drum appelle `synth_polyphony_set_track_active(track, 0, 0)` (`Src/Core/track_runtime.c:833-847`), qui coupe les notes, met `render_voice_count=0`, réinitialise chaque moteur et filtre de slot, libère chaque owner, efface l'état de voix et remet `active=0/base_slot=NO_VOICE` (`Src/Core/synth_polyphony.c:131-154`). La transition UI normale force ce refresh avant la fin de la mutation (`Src/Param/param_registry_transition.c:502-531`) et le rebind réinitialise le lane et ses drapeaux externes (`Src/Audio/mixer.c:748-795`).

La cause la plus probable du plateau résiduel, à confirmer sur la cible, est donc `tracks[3].enabled == 1` après la manipulation (transition ayant contourné `ui_core_runtime_bridge_sync_audio_runtime_enables()`, autre track Synth/Sampler/Drum encore comptée, ou mesure observée avant mise à jour/relâchement du compteur). Ce bit produit précisément le même coût de lane silencieux que l'activation initiale. Si `track_is_enabled(3)==0` est confirmé pendant le plateau, le code courant impose de chercher ensuite un lane `g_external_track_enabled[]` publié ou un diagnostic de build test ; ni un owner poly ni Prism seul ne peuvent alors expliquer un coût stable, puisque le renderer Prism n'est plus appelé pour un contexte Off.

## 2. Décomposition du coût

| Classe | Travail exact | Dépendance | Conclusion |
|---|---|---|---|
| Socle IRQ | scans des contextes par moteur, clears des bus MAIN/CUE, planification des lanes et master | constant | correspond au `~4 %` toutes tracks Off ; hors régression poly |
| Lane moteur silencieux | `tracks[3].enabled`, lane plan actif, boucle stéréo gain/pan/mute et routage | par présence globale d'au moins une famille moteur, pas par voix | meilleure explication du `4 -> 6 %` |
| Lane poly silencieux | clear de `g_external_track_l/r[mix_track]`, boucle `voice < render_voice_count`, recherches de slots | nombre de voix configurées si `>1` | inutile démontré ; aucun filtre/VCA/Braids pour les FREE |
| Voix Prism rendable | sync Prism, rendu source, sync filtre, filtre mono, ADSR VCA et somme L/R | voix HELD/RELEASE | coût par voix réel |
| Oscillateur Prism | paramètres, `MacroOscillator::Render()`, FIFO, conversion et mix | oscillateur dont niveau courant/cible est non nul | coût intrinsèque Braids dominant ; OSC1 OFF ne rend pas Braids |
| Fin de release source | buffer source mis à zéro si Prism ne rend plus, mais VCA encore avancé jusqu'à IDLE | voix RELEASE | silence utile au contrat de release, pas un leak |

Le saut mesuré `6 -> 9 %` avec une note et OSC0 seul mélange donc : rendu intrinsèque d'un `MacroOscillator`, syncs par voix, VCA/somme par sample et second passage gain/pan/routage du lane. Il ne mesure pas seulement Braids.

## 3. Chemins exacts des quatre états

### 3.1 Toutes les tracks Off

`brick6_audio_runtime_dsp()` appelle toujours `track_runtime_refresh_if_dirty()`, `mod_lfo_v1_process_block()`, puis les fonctions de rendu sampler, looper, Prism, Stack, Wave et DELUGE (`Src/Core/brick6_audio_runtime.c:546-610`). Chacune parcourt `SEQ_TRACK_COUNT`; les contextes Off échouent sur `bind_state`/`engine` avant tout rendu. Stack traite aussi sa queue de commandes une fois par bloc (`:593-597`).

`mixer_process()` :

- efface les bus MAIN L/R et CUE L/R ; les sends ne sont effacés que si un send/retour est actif (`Src/Audio/mixer.c:3131-3141`) ;
- efface le petit tableau `looper_output_active` et inspecte jusqu'à `MIXER_MAX_TRACKS` contextes looper (`:3142`, `:3178-3200`) ;
- construit un plan pour chaque lane ; tous les lanes sans source matérielle ni externe quittent à `mixer_build_lane_plan()` (`:3230-3243`) ;
- aucun slot synth, filtre poly, VCA poly ou instance Braids n'est consulté.

Données chaudes dominantes : contextes `g_track_runtime_ctx`, états mixer globaux, `g_tracks`, `g_track_filters`, buffers de bus DTCM.

### 3.2 Prism configuré sans note

Le contexte est BOUND/PRISM et `track_runtime_is_audio_routable()` est vrai (`Src/Core/brick6_audio_runtime.c:237-246`). Deux cas doivent être séparés.

**VOICES=1.** `mixer_begin_external_mono_native()` fournit le buffer direct, puis `brick6_braids_runtime_render_instance()` retourne immédiatement sur `has_note=0, gate=0, trigger=0, level=0` (`Src/Core/brick6_braids_runtime.cpp:502-519`). Il n'y a ni clear explicite du buffer, ni commit externe, ni filtre/VCA Prism. En revanche, la présence de la famille Synth a activé `tracks[3]` : `mixer_process()` exécute un lane stéréo silencieux complet.

**VOICES>1.** Avant toute inspection de voix, `mixer_begin_external_poly()` efface `2 * frames * sizeof(float)` (`Src/Audio/mixer.c:2938-2945`). La boucle parcourt exactement `render_voice_count`, qui vaut normalement le nombre configuré ; chaque appel à `synth_polyphony_voice_is_renderable()` résout le slot et rejette FREE (`Src/Core/synth_polyphony.c:510-516`). Aucun `sync_voice`, renderer, filtre ni VCA ne s'exécute. Sans voix publiée, il n'y a pas de commit, mais le lane matériel 3 reste traité.

Structures consultées : contexte track, `g_synth_poly`/`g_synth_slot_owner`/`g_synth_voice` en D2, buffers externes L/R en DTCM, lane 3 et filtre de track en D1.

### 3.3 Une voix Prism active, OSC0 seul

En poly :

1. clear L/R par `mixer_begin_external_poly()` ;
2. scan des `render_voice_count` voix ; les FREE sortent avant le moteur ;
3. pour la voix HELD, résolution du slot physique puis `brick6_braids_runtime_sync_voice()` ; pour une voix distincte de l'instance track, la fonction recopie à chaque bloc le release, huit paramètres par oscillateur, niveau/phase-reset et rappelle `set_shape()` deux fois (`Src/Core/brick6_braids_runtime.cpp:256-280`) ;
4. `brick6_braids_runtime_render_instance()` calcule les rampes des deux oscillateurs, mais OSC1 niveau zéro est exclu avant `MacroOscillator::Render()` (`:521-565`). Par tranche Braids, OSC0 efface `sync_block`, recalcule shape/pitch/paramètres, appelle `Render`, copie vers son FIFO, puis la boucle par sample lisse le niveau, parcourt encore les deux oscillateurs et mélange (`:568-680`) ;
5. `mixer_process_external_poly_voice()` recopie la configuration du filtre/VCA, bypass le filtre rapidement si OFF, puis avance l'ADSR VCA et somme mono vers L/R pour chaque sample (`Src/Audio/mixer.c:2948-2973`) ;
6. commit externe, puis `mixer_process()` traite le lane poly : pas de second filtre/VCA, mais gain/pan/mute, inserts/sends et sommation bus restent par sample (`Src/Audio/mixer.c:3307-3310`, `:3313-3430`).

Avec `VOICES=1`, le chemin poly par voix n'est pas utilisé : Braids rend directement dans le buffer mono du mixer et le filtre/VCA de track partagé est appliqué dans le lane. Le coût observé doit donc toujours être annoté avec la valeur de VOICES.

Structures dominantes : instance Prism de la voix (DTCM pour slots 0..7, D2 pour 8..15), table/code Braids en Flash, FIFO dans l'instance, `prism_tmp` en D1, filtre poly en DTCM et buffers externes en DTCM.

### 3.4 Retour de Prism à Track Off

Dans la transition normale :

- `track_runtime_refresh_track()` construit un contexte OFF puis `track_runtime_bind_ctx()` désactive la polyphonie (`Src/Core/track_runtime.c:1338-1415`, `:833-847`) ;
- pour chaque slot possédé, `synth_polyphony_reset_slot()` appelle all-notes-off/reset sur Prism, Stack, Wave, DELUGE et `mixer_synth_voice_slot_reset()` (`Src/Core/synth_polyphony.c:86-98`) ; owner et état de voix sont libérés ;
- `mixer_rebind_track_state()` réinitialise l'ancien lane, son filtre et ses flags externes (`Src/Audio/mixer.c:748-795`) ;
- le sync UI recalcule `has_engine_track` et doit appeler `track_enable(3, 0)` si toutes les familles Synth/Sampler/Drum sont absentes (`Src/UI/ui_core_runtime_bridge.c:1053-1073`).

Au bloc suivant, le renderer Prism ne voit plus de contexte BOUND/PRISM. Aucun HELD/RELEASE, filtre poly ou instance Prism n'est traité. Un plateau durable à 6 % démontre donc un état extérieur à ce chemin attendu, avec `tracks[3].enabled` comme premier suspect observable.

## 4. Anomalies et travail inutile

### Démontrés

- **Lane matériel moteur silencieux.** Le flag global active un traitement mixer complet même sans source externe publiée. Ce coût est commun à Prism/Stack/Wave/DELUGE et préexiste conceptuellement à la polyphonie, mais la nouvelle publication externe rend son doublon plus visible.
- **Clear poly trop tôt.** Les quatre boucles appellent `mixer_begin_external_poly()` avant le premier test rendable : Prism `Src/Core/brick6_audio_runtime.c:248-268`, Wave `:317-339`, DELUGE `:395-417`, Stack `:458-500`.
- **Coût de scan configuré.** La borne est `render_voice_count`, pas le nombre HELD+RELEASE. `synth_polyphony_find_slot()` peut scanner les 16 owners pour chaque index logique non nul (`Src/Core/synth_polyphony.c:36-49`). Le DSP cher est évité pour FREE, mais la complexité de contrôle est `O(voix configurées * budget global)`.
- **Sync Prism sans version.** Les paramètres immuables sont recopiés et `set_shape()` est rappelé chaque bloc et par voix rendable.
- **Sync filtre/VCA sans version.** `mixer_poly_filter_sync_config()` recopie les cibles et appelle huit setters ADSR chaque bloc par voix, même sans changement (`Src/Audio/mixer.c:208-247`). Seul le recalcul du cœur filtre est protégé par un changement de type.
- **Calculs répétables par bloc.** `synth_polyphony_get_voice_pan()` relit count/spread et effectue une division par voix (`Src/Core/synth_polyphony.c:329-337`) ; les gains de pan sont ensuite recalculés dans le mixer voix.
- **Deux étages de parcours par sample.** Une voix poly fait VCA+pan+somme dans `mixer_process_external_poly_voice()`, puis le lane refait gain/pan/mute/routage. Les deux étages ont des rôles sonores distincts, mais leurs invariants par bloc pourraient être préparés une seule fois.

### Corrects ou non démontrés comme bugs

- Les slots FREE sont exclus avant sync moteur, Braids, filtre et VCA.
- Prism continue volontairement à avancer le VCA sur un buffer source nul ; quand l'ADSR atteint IDLE, `synth_polyphony_voice_release_complete()` libère le slot dans le même bloc (`Src/Core/synth_polyphony.c:519-536`). Il n'y a pas de release Prism terminée conservée.
- Une voix RELEASE reste rendable tant que le VCA n'est pas IDLE : c'est le coût normal du release, non un leak.
- Le diagnostic audio est compilé en no-op quand `BRICK_TEST_BUILD=0` (`Inc/Audio/audio_track_diag.h:139-170`). La sélection ordinaire d'une track ne l'ouvre pas ; seuls les tests audio appellent `audio_track_diag_open/close()` (`Src/Core/audio_test_runner.c:803,1261`). Aucun tap/monitoring persistant de production n'est démontré.
- Le cleanup Track Off courant est démontré ; le plateau résiduel requiert une mesure d'état sur cible avant de conclure à un bug poly.

### Différences entre moteurs

La lane, le clear anticipé, le scan configuré, le filtre/VCA poly et la double passe mixer sont communs aux quatre moteurs. Les syncs sans version existent aussi dans leurs wrappers. Le coût source est spécifique : Prism paie Braids et ses FIFO ; Wave paie tables/interpolation et peut avancer silencieusement ; Stack paie sa synthèse/command queue ; DELUGE paie son renderer Q31. Prism est le plus sensible à la localité car une instance contient deux `MacroOscillator` volumineux. Le code Prism garantit l'avancement du VCA même si la source retourne 0 ; Wave/DELUGE ne publient la voix que si `prepare_block && render_instance` réussit, point à mesurer pour exclure une RELEASE bloquée côté moteur (`Src/Core/brick6_audio_runtime.c:329-338`, `:407-416`).

## 5. Cartographie mémoire du HEAD

Valeurs lues dans `build/Premium/BRICK6_CUBE.map` et dans les symboles ELF présents. Le build est localement modifié par des artefacts déjà présents ; les adresses restent utilisables pour le placement, mais les marges doivent être relues après un build propre autorisé.

| État | Section / domaine | Adresse, taille | Localité |
|---|---|---|---|
| `g_poly_filters_hot[16]` | `.dtcm_audio`, DTCM | `0x2000f200`, `0x3c00` (15 KiB, 960 o/slot) | excellent ; filtre+deux ADSR par voix déjà au meilleur endroit |
| `g_braids_runtime[8]` | `.dtcm_audio`, DTCM | `0x20012e00`, `0x2c60` (11.09 KiB, 1420 o/instance) | excellent pour slots préférentiels 0..7 |
| `g_braids_poly_d2[8]` | `.ram_d2_lut`, SRAM D2 | `0x30003f40`, `0x2c60` | moins bon ; instances supplémentaires et `MacroOscillator` chauds hors DTCM/D1 |
| `g_synth_voice[16]` | `.ram_d2_lut`, SRAM D2 | `0x30007840`, `0x80` | accès dispersé mais petit |
| `g_synth_slot_owner[16]` | `.ram_d2_lut`, SRAM D2 | `0x300078c0`, `0x10` | rescanné très souvent |
| `g_synth_poly[14]` | `.ram_d2_lut`, SRAM D2 | `0x300078d0`, `0xe0` | count/base/spread/active chauds |
| `g_track_filters` | `.bss`, RAM_D1 | `0x2400c5a0`, `0x3480` | bon domaine, structure large |
| `prism_tmp` | `.bss`, RAM_D1 | `0x2400ff8c`, `0x100` | bon domaine ; 64 floats |
| `g_external_track_l/r/mono` | `.dtcm_audio`, DTCM | `0x2000d5fc`, `0x2000c7fc`, plus mono ; `0xe00` chacun | excellent ; clears/sommes chauds |
| tables Braids (`wt_waves`, LUTs) | Flash interne `.rodata` | `wt_waves` `0x8100` ; LUTs dispersées | lecture cacheable, working set important selon modèle |

La section DTCM courante occupe `0x1e0a0` octets sur 128 KiB, soit environ 120.0 KiB et seulement 8032 octets libres. Le benchmark chorus temporaire du HEAD prend à lui seul `0x4d34` dans cette section ; la marge produit après retrait éventuel de ce benchmark serait donc très différente. Les objets ne sont pas tous alignés sur 32 octets : `g_poly_filters_hot` est naturellement contigu mais débute à `...f200`, les trois petits tableaux poly D2 sont contigus, tandis que la recherche logique->physique force des accès alternés entre métadonnées D2, filtre DTCM et instance Prism DTCM ou D2.

## 6. Candidats DTCM/RAM_D1, classés

1. **`g_braids_poly_d2` vers DTCM** — intérêt CPU très élevé pour les slots 8..15, risque sonore nul, complexité faible, mais 11.09 KiB excèdent la marge DTCM actuelle. À reconsidérer après retrait du benchmark temporaire ou arbitrage DTCM.
2. **`g_braids_poly_d2` vers RAM_D1** — intérêt élevé et faisable sans pression DTCM ; meilleur candidat immédiat pour supprimer les accès D2 des `MacroOscillator`, voix et FIFO supplémentaires.
3. **`g_synth_poly` + `g_synth_slot_owner` + `g_synth_voice` vers DTCM** — 368 octets seulement, intérêt moyen à élevé grâce aux scans répétés, risque et complexité très faibles.
4. **Buffers temporaires moteur `prism_tmp`, `wave_tmp`, `stack_tmp`, `deluge_tmp` vers DTCM** — 1 KiB total environ, intérêt moyen ; utiles à chaque voix séquentielle mais déjà en RAM_D1. Gain à mesurer après les métadonnées et instances.
5. **Tables Braids chaudes sélectionnées vers RAM_D1** — intérêt dépendant du modèle, risque mémoire moyen et complexité moyenne ; ne déplacer que les LUTs identifiées par compteur DWT/cache, pas les 33 KiB de wavetables en bloc sans preuve.

Les filtres poly et buffers externes sont déjà en DTCM ; les déplacer n'apporterait rien. `g_track_filters` et les temporaires sont déjà en RAM_D1. Le meilleur défaut de placement démontré est donc la moitié des instances Prism en SRAM D2.

## 7. Historique causal

- `ccdffad31` (2026-07-28) a fait passer Prism à deux `MacroOscillator`, deux états/FIFO et un mix interne. Il explique la pente par oscillateur et la taille de 1420 octets par instance, pas le lane silencieux.
- `bcfa7f0b7` (2026-07-31) a introduit `synth_polyphony`, le budget global 16, les loops `render_voice_count`, `mixer_begin_external_poly()`, les filtres/VCA par voix et les syncs moteur par bloc. C'est le changement structurel qui explique le nouveau coût fixe poly, la pente par voix et la multiplication des états Prism.
- `23d42678b` (2026-07-31) a remplacé le mapping filtre `track*8+voice` (40 filtres DTCM + 16 D3) par 16 filtres indexés par slot global, tous DTCM, et a ajouté le reset de slot. Il a aussi ajouté la désactivation poly lors d'un passage hors Synth/Drum. Le bug évident de cleanup de l'introduction n'est donc plus présent au HEAD.
- `cb31b3750` (2026-08-02) a restauré la publication poly avec l'identité séparée `mix_track_id/poly_track_id`; il n'a pas supprimé les clears/scans/syncs.

La régression temporelle est ainsi cohérente avec `bcfa7f0b7`, mais la persistance après Off observée doit être distinguée du cleanup poly déjà corrigé dans `23d42678b`.

## 8. Pistes d'optimisation sans implémentation

| Priorité | Piste | Gain probable | Risque | Complexité |
|---|---|---:|---:|---:|
| 1 | confirmer puis supprimer/corriger l'activation silencieuse persistante de `tracks[3]`; éviter le lane HW moteur quand seules les publications externes sont utilisées | élevé, proche des `2 %` mesurés | moyen (routage entrées/moteurs) | faible à moyenne |
| 2 | compter/pré-scanner les voix rendables avant `mixer_begin_external_poly()` et quitter sans clear/commit si zéro | moyen à élevé en silence poly | faible | faible |
| 3 | versionner les configs track Prism et filtre/VCA ; synchroniser une voix seulement à l'allocation ou quand la version change | moyen par voix active | moyen (automation live) | moyenne |
| 4 | conserver une liste/bitmask HELD+RELEASE ou un mapping direct des slots logiques ; ne pas rescanner 16 owners | faible à moyen, augmente avec VOICES | moyen (allocation/vol) | moyenne |
| 5 | préparer une fois par track/bloc spread, pans et coefficients ; éviter divisions et lectures répétées | faible à moyen | faible | faible |
| 6 | déplacer les instances Prism supplémentaires en RAM_D1, puis DTCM si budget réel suffisant | moyen sur voix slots 8..15 | faible | faible |
| 7 | fusionner seulement les invariants des passes VCA/pan et lane, sans changer le filtre/VCA par voix | moyen potentiel | élevé (gain/pan/routage) | élevée |

Le renderer Braids lui-même reste un coût intrinsèque attendu. Toute optimisation interne de `MacroOscillator::Render()` ou des modèles risquerait le son et vient après l'élimination du lane silencieux, des syncs et des scans.

## 9. Mesures matérielles minimales

Sans instrumentation permanente, relever sur les quatre scénarios, au même endroit que le compteur IRQ existant :

1. `track_is_enabled(3)`, nombre de familles Synth/Sampler/Drum et `ctx->{family,engine,bind_state,mix_track_id}` avant/après Off ;
2. `synth_polyphony_get_track_active()`, `voice_count`, `render_voice_count`, owner du slot de base et nombres FREE/HELD/RELEASE ;
3. `g_external_track_enabled[mix_track]` au début de `mixer_process()` et nombre de lane plans actifs ;
4. `audio_track_diag_is_enabled()` et `BRICK_TEST_BUILD` du binaire testé ;
5. DWT autour de six régions seulement : `brick6_render_prism_tracks`, clear `mixer_begin_external_poly`, scan des voix, `brick6_braids_runtime_sync_voice`, `brick6_braids_runtime_render_instance`, `mixer_process_external_poly_voice`, puis lane 3 dans `mixer_process()` ;
6. matrice VOICES `1/2/4/8` avec zéro note, une note OSC0, une note OSC0+OSC1, release courte puis longue ; consigner séparément voix configurées, rendables et RELEASE ;
7. répéter zéro note et une note sur Stack/Wave/DELUGE. Un `+2 %` identique sans note confirme le lane commun ; seule la pente note/oscillateur distingue les moteurs.

Le test décisif du plateau Off est immédiat : si `track_is_enabled(3)` passe de 1 à 0 et aucun lane externe n'est publié, la charge doit revenir au socle. Sinon, la première correction doit viser l'autorité d'activation du lane, pas Braids ni le pool poly.
