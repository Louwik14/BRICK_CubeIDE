# Audit architecture ciblé — runtime track-aware / parallélisme tracks / séparation UI focus vs runtime

Date: 2026-03-27  
Scope: séquenceur + p-lock runtime + clavier/arp + route paramètres track-aware

---

## 1) Résumé exécutif

Le bug observé (p-locks qui cessent ou « sautent » de track après changement de focus UI) est cohérent avec une **dette d’architecture transversale** : le runtime séquenceur est déjà multi-track dans sa boucle, mais le plan d’application des paramètres et des notes reste partiellement adossé à des abstractions globales ou UI-centrées (active track, target runtime non injecté, moteurs globaux).

Constat principal:
- `seq_runtime` avance bien les 8 tracks en parallèle et applique les p-locks avec un `track` explicite.
- Mais la couche d’atterrissage (`param_registry`, moteurs synth/filter, keyboard engine) n’est pas entièrement « runtime track-explicit ».
- Résultat: incohérences de binding track->cible runtime, collisions inter-tracks, et perturbations lors des sync UI liées au changement de focus.

Conclusion:
- Ce n’est pas un bug local à corriger par patch ponctuel.
- Une **refonte incrémentale ciblée** est justifiée pour séparer strictement:
  - focus UI (édition/affichage),
  - runtime track context (exécution indépendante par track),
  - dispatch vers cibles runtime (mixer/synth/midi) via APIs explicites.

---

## 2) Architecture actuelle observée (état réel)

### 2.1 Focus UI / track active

Le focus UI vit dans `ui_core` (`g_ui_track_state.active_track`) et est modifié via SHIFT+HALL.  
Ce focus pilote aussi des synchronisations de paramètres côté UI (`ui_core_sync_active_track_cfg_params()`), incluant un `param_registry_sync_filter_ui_for_active_track()`.  

Implication: un simple changement de track visible déclenche des écritures runtime param/filter liées à la track active UI.

### 2.2 Runtime séquenceur

`seq_runtime_process()` est appelé en superloop, hors UI.  
`seq_runtime`:
- maintient un playhead **par track**,
- applique/restaure p-locks par track avec pipeline dédié,
- gère clock interne/externe MIDI,
- schedule les événements PLAY (note on/off) avec `track` conservé dans l’événement.

Le cœur temps-réel du séquenceur est donc déjà orienté « multi-track parallèle ».

### 2.3 P-lock runtime / interface paramètres

`seq_runtime` passe par `seq_param_iface_*` avec `track` explicite.  
`seq_param_iface` délègue ensuite à `param_registry_get_track_value()` / `param_registry_apply_track_value()`.

Point critique: dans `param_registry_apply_track_value()` et `param_registry_get_track_value()`, seuls certains paramètres (famille FILTER générique) sont réellement traités par chemin track-aware dédié; le reste retombe sur `param_set()` / `param_get()` globaux.

### 2.4 Paramètres runtime et mapping track->cible

Le mapping FILTER utilise `resolve_filter_target_track_for_ui_track(ui_track, ...)`:
- INPUT1->0, INPUT2->1, INPUT3->2,
- SYNTH->3 (toutes tracks synth vers la même cible mixer filter track 3).

En parallèle, de nombreux paramètres synth (DX7/MonoB) appliquent directement sur moteurs globaux (`microdexed_synth_set_param`, `monob_synth_set_*`) sans contexte `track`.

Conséquence: même si le séquenceur fournit un `track`, l’atterrissage final n’est pas isolé par track pour toutes les familles de paramètres.

### 2.5 Clavier runtime / arp runtime

`keyboard_engine` lit explicitement `ui_get_active_track()` pour:
- déterminer le type de track/synth actif,
- router note path,
- résoudre la cible filtre via `ui_resolve_filter_target_track()`.

Donc clavier/arp sont explicitement **focus-driven** aujourd’hui (normal pour jeu « track active »), et partagent le même risque latent si on ne formalise pas la séparation « source d’événements » vs « contexte track runtime ».  
Ce n’est pas le bug principal du séquenceur, mais c’est la même dette structurelle.

### 2.6 MIDI events

`seq_runtime` envoie les notes PLAY sur canal MIDI = `track & 0x0F`, donc le message garde un contexte track.  
Mais les changements de paramètres (p-locks) n’ont pas encore partout une destination runtime totalement track-explicite équivalente.

---

## 3) Problèmes structurels identifiés

### 3.1 Couplage fautif UI focus -> runtime filter sync

`ui_core_set_active_track()` appelle `ui_core_sync_active_track_cfg_params()`, qui appelle `param_registry_sync_filter_ui_for_active_track()`.  
Cette sync applique des valeurs FILTER sur la cible runtime de la track UI active.

Donc changer de focus peut réécrire l’état runtime filter en cours, y compris pendant lecture séquenceur.

### 3.2 Ambiguïté track UI vs cible runtime réelle

Le mapping `resolve_filter_target_track_for_ui_track()` force `SYNTH -> target 3`.  
Si plusieurs tracks UI sont de type synth, elles convergent vers la même cible filter runtime.

Effets possibles:
- p-locks d’une track synth écrasent l’autre,
- perception d’« effet qui suit la track focus » ou « mauvaise track bougée »,
- impossibilité d’isoler correctement des séquences en parallèle pour ces paramètres.

### 3.3 Track-awareness partielle du param engine

`seq_param_iface` est track-aware côté API, mais retombe sur `param_registry` dont une partie du registry est global (via `param_set/get`, apply callbacks non trackées).  
=> rupture de contrat: l’appelant croit cibler une track, la destination runtime ne l’est pas toujours.

### 3.4 Défaut latent partagé clavier/arp

Le clavier/arp s’appuie sur `ui_get_active_track()` à l’émission des notes.  
Pour un mode « performer la track focus », c’est acceptable; mais architecturalement cela confirme qu’il n’existe pas encore de « runtime context object » explicite propagé entre services.

### 3.5 Nature de la dette

Dette transversale (UI, param registry, keyboard engine, synth/filter backends), pas simple bug local seq runtime.

---

## 4) Architecture cible recommandée

## 4.1 Principe de séparation stricte

1. **UI Focus Context (édition/affichage)**
- track regardée/éditée,
- page, feedback, templates,
- aucune écriture runtime implicite hors action utilisateur explicite.

2. **Track Runtime Context (exécution)**
- état par track: playhead, locks actifs, bases, note events,
- source clock/transport,
- routing destination résolu explicitement par track.

3. **Runtime Target Resolver (service central)**
- fonction pure: `ui_track`/`seq_track` -> `runtime_target` (mixer lane, synth instance/logical voice group, midi channel/port).
- plus de résolution implicite via active track.

4. **Parameter Runtime Engine (track-explicit)**
- API unique: `apply(track_ctx, param_id, value, source)` et `read(track_ctx, param_id)`.
- interdit toute dépendance à `ui_get_active_track()` dans ce chemin.

## 4.2 Modèle d’articulation services

- **Sequencer runtime**: producteur de commandes track-explicites (step boundary, p-lock apply/restore, play note events).
- **Keyboard runtime / Arp runtime**: producteur de notes avec un `source_context` explicite (focus-performer aujourd’hui, extensible demain).
- **Parameter engine**: applique sur cibles runtime résolues (mixer/filter/synth/midi) par `runtime_target`.
- **UI**: édite modèles + visualise états; déclenche des commandes, mais ne « resynchronise » pas silencieusement le runtime d’une autre track juste par changement de focus.

## 4.3 Contrats à imposer

- Aucune fonction runtime appelée depuis seq/keyboard/arp ne lit `ui_get_active_track()`.
- Les APIs `param_registry_apply_track_value/get_track_value` deviennent contractuellement track-explicites sur l’intégralité des paramètres p-lockables/PLAY.
- Les sync UI (affichage) ne doivent pas réappliquer les DSP states; elles ne mettent à jour que les caches UI.

---

## 5) Plan d’action incrémental

## Étape A — Geler les effets de bord focus->runtime

Objectif:
- empêcher le changement de focus UI d’écraser l’état runtime pendant transport.

Actions:
- isoler `param_registry_sync_filter_ui_for_active_track()` en mode « UI cache only » ou conditionnel hors runtime write.
- auditer tous les appels indirects provoqués par `ui_core_sync_active_track_cfg_params()`.

Validation:
- test repro: track A joue p-locks filter, switch focus vers B -> A continue sans rupture.

Risque:
- désalignement temporaire affichage vs état moteur si sync UI mal gérée.

## Étape B — Introduire un resolver runtime explicite

Objectif:
- centraliser mapping track->target runtime dans un seul service.

Actions:
- créer une structure `runtime_target_t` et une API resolver partagée (seq, keyboard, param).
- retirer les résolutions éparses `ui_resolve_filter_target_track()` dans chemins runtime.

Validation:
- tests unitaires mapping pour toutes families/types.
- vérifier absence de régression sur routing existant.

Risque:
- erreurs de mapping lors migration partielle.

## Étape C — Rendre `param_registry_apply/get_track_value` réellement track-aware

Objectif:
- fermer la rupture de contrat actuelle.

Actions:
- migrer les paramètres p-lockables (COLORS/TONE/PLAY) vers apply/read track-explicites.
- éliminer fallback implicite `param_set/get` global pour paramètres qui doivent être trackés.

Validation:
- scénario multi-track synth/input en lecture parallèle avec p-locks distincts.

Risque:
- découverte de limites moteurs (synth global unique) nécessitant abstraction intermédiaire.

## Étape D — Formaliser les sources d’événements runtime

Objectif:
- unifier séquenceur, clavier, arp autour d’un contrat commun de dispatch.

Actions:
- introduire `event_source_t` + `track_context` injecté.
- keyboard/arp restent focus-driven en V1, mais via contexte explicite (pas lecture active_track au fond du moteur).

Validation:
- non-régression KBD/ARP live + maintien du comportement actuel utilisateur.

Risque:
- coût de refactor API sans gain visible immédiat (mais dette levée).

## Étape E — Validation parallélisme final

Objectif:
- garantir lecture simultanée propre multi-track.

Validation cible:
- 2+ tracks avec séquences et p-locks distincts en parallèle,
- changements de focus UI sans impact runtime,
- MIDI/PLAY events correctement isolés par track.

---

## 6) Ce qui peut attendre

Peut rester hors-scope de cette refonte:
- smoothing/lissage des paramètres continus (plocs),
- refonte persistance globale,
- refonte LEDs,
- extension feature PLAY/voix réelles avancées,
- MIDI out complet enrichi.

Important:
- le smoothing n’est pas bloquant pour corriger le défaut architectural focus/runtime.
- il sera à traiter ensuite, une fois le contrat track-aware fiabilisé.

---

## 7) Questions ouvertes réellement bloquantes

1. **Modèle synth runtime visé à court terme**: mono instance globale synth (DX7/MonoB) conservée temporairement, ou séparation logique minimale par track (même sans vraie poly-instances) ?
2. **Contrat PLAY**: les paramètres PLAY doivent-ils être strictement séquenceur-only, ou partageables en édition live avec clavier/arp sur même track context ?

Ces deux points impactent la granularité exacte du `runtime_target_t`, mais ne bloquent pas l’Étape A (découplage focus->runtime) ni l’Étape B (resolver central).

