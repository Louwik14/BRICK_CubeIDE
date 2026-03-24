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
- `Hybrid` = track audio + chemin note/midi amené à évoluer
- une vraie family `MIDI` viendra plus tard

---

## 4. Où brancher les choses

- choix family/type d’une track : `CFG`
- paramètres filtre : `FILTER`
- paramètres moteur sonore / oscillateurs : `TONE`
- jeu clavier : `KEYBOARD`
- arpégiateur : `ARP`

Ne pas créer un nouvel ensemble si un ensemble existant est déjà le bon point d’entrée.

---

## 5. Hall modes : invariants à ne pas casser

### Track select
- `SHIFT` maintenu
- puis `HALL 0..7`

### KEYBOARD
- `SHIFT + HALL 9` (index 8)
  - simple tap => active `KEYBOARD`
  - double tap => ouvre l’UI `KEYBOARD`

### ARP
- `SHIFT + HALL 10` (index 9)
  - simple tap => active `ARP`
  - double tap => ouvre l’UI `ARP`

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
- `FILTER`
- `TONE`
- `KEYBOARD`
- `ARP`

---

## 7. LEDs : règles simples

### Boutons param
- toutes vertes
- sauf le bouton de l’ensemble UI actif : blanc
- si l’ensemble actif n’a pas de bouton param associé : toutes vertes

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
- `FILTER` dédié

Ne pas mélanger :
- filtres audio des tracks input
- filtre dédié d’un moteur synth comme `MonoB`

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