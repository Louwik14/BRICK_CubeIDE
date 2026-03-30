# SEQUENCER_TIME_REFACTOR_REFERENCE

## 1) Objet du document

Ce document fige le cadrage **spécifique** de la refonte du domaine temps du séquenceur BRICK6.

Il complète `SEQUENCER_TIME_ARCHITECTURE_REFERENCE.md` sans le remplacer.
En cas de divergence, `SEQUENCER_TIME_ARCHITECTURE_REFERENCE.md` est prioritaire.

But immédiat:
- fournir une référence opérationnelle pour les prochaines passes humaines/Codex,
- supprimer l’ambiguïté sur la frontière réelle du domaine temps,
- préparer un découplage propre de la dépendance superloop,
- **sans** migration timer hardware dans cette passe.

---

## 2) Constat structurel actuel

Le séquenceur V1 est fonctionnel, mais le problème restant est structurel:

- le temps est produit d’un côté (audio IRQ),
- puis consommé/servi côté superloop,
- ce qui autorise du catch-up en rafale,
- avec scheduling/dispatch possibles en paquet,
- et un flush USB physique encore opportuniste hors noyau métier.

La base existante (transport, préroll, boundary, scheduler, live-rec, guards) est valide.
Le point à corriger est la **cohérence du domaine temps** et sa frontière.

---

## 3) Problème dominant à résoudre

Le problème dominant n’est pas l’absence d’une feature.

Le problème dominant est l’absence d’une frontière complète, unifiée et explicite du domaine temps séquenceur:
- chemins métier INT/EXT encore partiellement parallèles,
- responsabilités temporelles encore mélangées avec le contexte d’appel,
- séparation incomplète entre logique temporelle, publication d’état et flush physique.

La cible n’est **ni** une rustine **ni** une extraction minimale.
La cible est la **frontière complète** du domaine temps séquenceur.

---

## 4) Frontière complète cible du domaine temps séquenceur

### 4.1 Ce qui doit impérativement être dedans

Le domaine temps séquenceur inclut **ensemble**:

1. décision temporelle (consommation tick/clock, conversion en progression musicale),
2. gate transport,
3. préroll / count-in,
4. progression musicale / boundary,
5. apply / restore p-lock,
6. scheduler PLAY complet:
   - décision au boundary,
   - enqueue horodaté,
   - dispatch when due,
7. live-rec complet:
   - `NOTE/VEL/MICTIM` au note-on,
   - `LEN` au note-off,
   - flush sur STOP / disarm / fin de pattern rec,
8. STOP sûr complet:
   - purge queue PLAY,
   - restore locks,
   - panic / all-notes-off,
   - reset guards / live-rec,
   - idempotence.

### 4.2 Ce qui doit impérativement rester dehors

Restent hors domaine temps séquenceur:

- production brute du temps (source de cadence, pas logique musicale),
- flush physique I/O (USB/DIN/host),
- UI / LED / persistance / renderer / halls.

### 4.3 Pourquoi

Cette frontière:
- respecte le pipeline canonique de référence,
- évite les demi-frontières provisoires,
- permet un découplage progressif sans big bang,
- laisse la migration timer hardware hors périmètre immédiat.

---

## 5) Rôle futur de `seq_runtime`

`seq_runtime` reste:

- façade API publique du séquenceur,
- orchestrateur unique,
- autorité d’ordre global des phases temporelles.

`seq_runtime` ne doit plus porter:

- des chemins métier parallèles INT vs EXT,
- une logique temporelle dupliquée selon la source,
- des points d’entrée qui contournent l’ordre canonique.

Principe: une seule orchestration, un seul ordre de référence, une seule autorité métier.

---

## 6) Décomposition interne cible du pipeline temporel

Ordre strict à appliquer:

1. tick/clock,
2. gate transport,
3. préroll,
4. boundary,
5. apply/restore,
6. enqueue PLAY,
7. dispatch événements dus,
8. publication d’état.

Contraintes:

- préroll avant tout running réel,
- aucune décision PLAY avant apply/restore du step courant,
- scheduler PLAY en 3 phases strictes,
- aucun NOTE ON après STOP,
- boundary initial post-START traité explicitement.

---

## 7) Place exacte de live-rec

Live-rec appartient pleinement au domaine temps séquenceur.

Règles:

- note-on: capture `NOTE/VEL/MICTIM`,
- note-off: capture `LEN`,
- flush: obligatoire sur STOP, disarm, fin de pattern rec.

Dépendances temporelles:

- autorité temporelle identique au reste du domaine temps,
- dépend des gates transport (running/count-in/rec actif),
- cohérence avec output guard (éviter collisions de notes externes déjà actives),
- pas de chemin live-rec “hors noyau temps”.

---

## 8) Unification complète INT / EXT

### 8.1 Principe

INT et EXT convergent vers un **tronc métier commun**.

Tronc commun obligatoire:
- gate transport,
- préroll,
- boundary,
- apply/restore,
- enqueue,
- dispatch,
- publication.

### 8.2 Ce qui peut rester spécifique source

Uniquement la normalisation de la source clock:
- INT: calcul/consommation de période interne,
- EXT: décodage pulses et validation tempo externe.

### 8.3 Ce qui doit devenir strictement commun

Tout le reste:
- décisions transport/step,
- progression musicale,
- scheduling PLAY,
- dispatch logique,
- publication d’état,
- lifecycle STOP/live-rec.

---

## 9) Séparation dispatch logique / publication / flush physique

### 9.1 Dispatch logique

Définition:
- décision des événements dus,
- application des garde-fous transport,
- émission logique NOTE ON/OFF vers les sinks métier.

### 9.2 Publication d’état

Définition:
- état runtime cohérent exposé aux autres couches,
- playheads, flags transport/rec, état tempo valide, etc.

### 9.3 Flush physique I/O

Définition:
- vidage effectif des buffers de transport physique (USB/DIN/host).

Règle:
- le flush physique n’est pas la responsabilité du domaine temps métier,
- mais le domaine temps doit rester cohérent même si ce flush est opportuniste/asynchrone.

---

## 10) Invariants non négociables

- Pas de big bang.
- Pas de refonte globale du firmware.
- Pas de remise en cause de `SEQUENCER_TIME_ARCHITECTURE_REFERENCE.md`.
- Préserver strictement l’ordre canonique.
- Préroll avant running réel.
- Scheduler PLAY en 3 phases strictes.
- Live-rec complet dans le cadrage temps.
- Aucun NOTE ON après STOP.
- STOP / panic / all-notes-off cohérents et idempotents.
- Une seule autorité d’orchestration (`seq_runtime`).
- Migration timer hardware hors périmètre de cette passe.

---

## 11) Plan de migration par passes

### Passe 1 — Convergence du tronc métier INT/EXT

Objectif:
- refermer les divergences métier INT/EXT,
- imposer un chemin unique de décision temporelle.

Invariant principal:
- comportement utilisateur inchangé (START/STOP/CONTINUE/clock),
- préroll conservé avant running.

### Passe 2 — Fermeture de la frontière complète (live-rec + STOP lifecycle)

Objectif:
- intégrer explicitement live-rec complet et STOP lifecycle dans le même domaine temps,
- supprimer les zones “provisoirement hors frontière”.

Invariant principal:
- NOTE/VEL/MICTIM note-on, LEN note-off, flush STOP/disarm/fin pattern rec,
- STOP sûr et idempotent.

### Passe 3 — Durcissement ownership / accès états

Objectif:
- verrouiller les accès lecture/écriture d’état temporel,
- interdire les contournements externes du pipeline canonique.

Invariant principal:
- `seq_runtime` reste l’unique autorité d’ordre.

### Passe 4 — Découplage appelant superloop vers adaptateur

Objectif:
- isoler le domaine temps de son appelant actuel superloop,
- sans changer le comportement musical.

Invariant principal:
- ordre canonique inchangé,
- aucun NOTE ON après STOP.

### Passe 5 — Validation STOP / panic / flush bout en bout

Objectif:
- valider la cohérence logique (STOP/panic/guards/live-rec) avec l’I/O physique asynchrone.

Invariant principal:
- STOP cohérent et idempotent en conditions réelles,
- pas de régression de sécurité note.

---

## 12) Risques principaux de refonte

1. off-by-one au passage préroll -> running,
2. NOTE ON tardif après STOP,
3. divergence résiduelle INT/EXT,
4. amplification des rafales de catch-up,
5. régression live-rec (LEN/flush),
6. restore locks incomplet en transitions/STOP,
7. confusion entre dispatch logique et flush physique.

Mitigation structurante:
- passes petites,
- invariants vérifiés à chaque passe,
- aucun élargissement de périmètre.

---

## 13) Conclusion opérationnelle

La refonte visée dans ce document ne demande **pas** encore la migration timer hardware.

L’objectif immédiat est de rendre le domaine temps séquenceur:
- cohérent,
- unifié (INT/EXT),
- complet (y compris live-rec et STOP lifecycle),
- découplable proprement de la dépendance superloop,
- sans changement de comportement musical.

Ce document sert de référence opérationnelle pour les prochaines passes d’implémentation incrémentales.
