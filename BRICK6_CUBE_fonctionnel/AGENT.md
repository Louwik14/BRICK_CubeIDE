# AGENT.md

## Rôle

Ce fichier guide Codex pour travailler efficacement dans ce repo.

Objectif :
- comprendre la logique structurante du projet
- modifier proprement sans casser les invariants
- brancher les nouvelles features au bon endroit
- garder `AGENT.md` et `ARCHITECTURE_GLOBAL.md` à jour si la structure du projet évolue

Ce fichier n’est pas une doc complète d’architecture.
La doc détaillée vit dans `ARCHITECTURE_GLOBAL.md`.

---

## 1. Philosophie de travail

- Étendre l’existant plutôt que redessiner.
- Réutiliser un sous-système existant avant d’en créer un nouveau.
- Garder les modifications locales quand c’est possible.
- Éviter les doubles autorités sur un même état runtime.
- Ne pas supposer que logique = physique : utiliser des remaps explicites.

---

## 2. Invariant principal

Le projet est **track-aware**.

Toujours raisonner avec :
- track active
- family de track
- type de track
- runtime associé

Ne pas ajouter une feature “globale” si elle dépend en réalité de la track active.

### Routing mix structurant (runtime)
- L’autorité de binding reste `track_runtime`.
- Le mapping `UI track -> mix target runtime` est explicite et unique (pas de mapping parallèle caché).
- La résolution `UI track -> filter target runtime` doit aussi passer par `track_runtime` (pas de chemin opérationnel `runtime_target`).
- Les contributions `Synth` doivent rester séparées par track jusqu’au `mixer` (pas de somme précoce globale).
- Les getters/checks runtime ne doivent pas déclencher de refresh implicite : l’invalidation est explicite (`track_runtime_invalidate_all`) et le refresh est demandé explicitement par les appelants autorisés.

---

## 3. Families et types actuels

### Families
- `Off`
- `Input1`
- `Input2`
- `Input3`
- `Input4`
- `Synth`

### Types
- pour `InputX`
  - `Audio`
  - `Hybrid`
- pour `Synth`
  - `DX7`
  - `MonoB`

### Notes
- `Off` = vraie désactivation runtime
- `Input1..4` = ressources physiques exclusives
- modèle produit: 4 entrées stéréo physiques visées (`Input1..4`)
- devboard proto actuelle: 3 entrées stéréo réellement câblées côté front-end DSP
- `Input4` reste une ressource physique valide (réservée/future tant que la 4e entrée n’est pas câblée sur la proto)
- `Hybrid` = track audio + chemin note/midi amené à évoluer
- une vraie family `MIDI` viendra plus tard

---

## 4. Où brancher les choses

- choix family/type d’une track : `CFG`
- paramètres filtre / couleur audio : `COLORS`
- paramètres moteur sonore / oscillateurs : `TONE`
- jeu clavier : `KEYBOARD`
- arpégiateur : `ARP`

Ne pas créer un nouvel ensemble si un ensemble existant est déjà le bon point d’entrée.

---

## 5. Hall modes : invariants à ne pas casser

### Track select
- `TRACK` maintenu (`BTN_PARAM_8`)
- puis `HALL 0..7`

### KEYBOARD
- `SHIFT + HALL 9` (index 8)
  - simple tap => active `KEYBOARD`
  - double tap => ouvre l’UI `KEYBOARD`

### ARP
- `SHIFT + HALL 10` (index 9)
  - simple tap => active `ARP`
  - double tap => ouvre l’UI `ARP`

### PATTERN
- `SHIFT + -` => mode `PATTERN RECALL`
- `TRACK + -` => mode `PATTERN STORE`

### Règles
- `SHIFT` doit être pressé avant le hall
- `ARP` est un sous-mode du `KEYBOARD`
- `ARP` ne doit jamais casser l’activation explicite de `KEYBOARD`

---

## 6. UI : règle de résolution

L’UI se résout par contexte, pas page par page isolée.

Toujours penser :
- ensemble demandé
- track active
- family
- type

Ensembles importants existants :
- `CFG`
- `COLORS`
- `TONE`
- `MOD`
- `MIX`
- `KEYBOARD`
- `ARP`

### MOD : résolution des destinations LFO (invariant)
- Les destinations LFO doivent venir d’une liste explicite validée runtime.
- Autorisés uniquement : domaines `COLORS` et `TONE` réellement valides pour la track.
- Interdit : domaine `PLAY` (ni affiché, ni sélectionnable, ni applicable).
- Le filtrage doit rester strictement track-aware (family/type/runtime effectif), sans fallback cross-engine.

---

## 7. LEDs : règles simples

### Boutons param
- toutes vertes
- sauf le bouton de l’ensemble UI actif : blanc
- si l’ensemble actif n’a pas de bouton param associé : toutes vertes

### Mapping boutons param (autorité unique)
- `BTN_PARAM_1` -> `COLORS`
- `BTN_PARAM_2` -> `TONE` (résolution contextuelle track-aware ; sur `Synth + DX7` ouvre l’UI DX7)
- `BTN_PARAM_3` -> `MOD`
- `BTN_PARAM_4` -> `MIX`
- `BTN_PARAM_5` -> `PLAY` (uniquement quand la track active est `Synth`)
- `BTN_PARAM_8` -> bouton spécial `TRACK` (pas un point d’entrée d’ensemble UI)
- `CALIBRATION` et `POTS DEBUG` ne sont pas exposés via les boutons param

### Halls
- `ARP` réutilise la scène LED du `KEYBOARD`
- ne pas dupliquer une scène si la même logique peut être partagée

### Remaps
Pour tout mapping logique/physique :
- boutons
- halls
- leds

toujours utiliser des tables explicites.

---

## 8. Synth engines actuels

### DX7
- moteur `Synth` stable

### MonoB
- moteur `Synth` stable
- monophonique mono
- `TONE` dédié
- `COLORS` dédié

Ne pas mélanger :
- filtres audio des tracks input
- filtre dédié d’un moteur synth comme `MonoB`

### COLORS : règles runtime UI/audio
- l’ensemble UI historique `FILTER` est renommé produit en `COLORS`
- pour `InputX + Audio` :
  - page `MOD` non exposée
  - runtime forcé : `KeyTrack = 0`, `EnvRst = Off`, `EnvDly = 0`
- pour `InputX + Hybrid` :
  - page `MOD` conservée
- page 4 `CRUNCH` :
  - paramètre unique `Drive`
  - `Drive = Off` => saturation bypass (hors traitement effectif)
  - `Drive > 0` => saturation active et pilotée par `Drive`

---

## 9. Règles Codex

Quand tu modifies le projet :

1. réutilise avant de créer  
2. garde une seule autorité par état structurant  
3. garde les remaps explicites  
4. respecte la logique track-aware  
5. ne casse pas `SHIFT before HALL`  
6. ne casse pas `KEYBOARD` / `ARP`  
7. ne fais pas de redesign gratuit  

---

## 10. Mise à jour de la doc

Si une passe modifie :
- families/types
- hall modes
- ensembles UI
- routing structurant
- logique runtime majeure
- architecture d’un sous-système

alors mettre à jour dans la même passe :
- `AGENT.md`
- `ARCHITECTURE_GLOBAL.md`


---

## 11. Cardinalités track (règle locale MIX legacy)

- Les paramètres legacy `PARAM_MIX_TRACKx_*` sont un chemin **physique audio** et restent limités à `MAX_TRACKS` (4).
- Les paramètres `PARAM_MIX_*` (sans suffixe track) suivent la track **logique/runtime** active (jusqu’à `UI_TRACK_COUNT`/`SEQ_TRACK_COUNT`).
- Dans `param_registry`, privilégier un dispatch indexé/table-driven pour les paramètres legacy trackés (pas de handlers hardcodés `track0..3` opérationnels).
