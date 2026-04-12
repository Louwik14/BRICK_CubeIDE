# AGENT.md

## Rôle

Ce fichier guide Codex pour travailler efficacement dans ce repo.

Objectif :
- comprendre vite les contraintes utiles
- faire des passes locales, ciblées, vérifiables
- éviter les refontes inutiles
- ne pas casser le runtime audio

Ce fichier n’est pas la doc produit complète.
Pour le contexte global, lire aussi :
- `readme.md`

---

## 1. Lecture minimale obligatoire

Lire d’abord :
- `AGENT.md`
- `readme.md`

Puis lire uniquement les fichiers directement concernés par la passe.

Si l’utilisateur a déjà donné :
- une cause probable
- une zone probable
- un fichier probable
- une ancienne cause déjà identifiée

alors commencer par ça.

Ne pas lancer une exploration large du repo tant que cette piste n’a pas été auditée.

---

## 2. Règles de travail

- Étendre l’existant plutôt que redessiner.
- Réutiliser avant de créer.
- Garder les modifications locales.
- Préserver le comportement existant hors zone corrigée.
- Ne pas introduire de seconde autorité pour un état déjà piloté ailleurs.
- Ne pas créer de chemin parallèle caché UI/runtime.
- Pas d’allocation dynamique.
- Pas de changement gratuit dans les zones sensibles audio/runtime.
- Une passe = un bug cohérent ou une correction cohérente.
- Ne pas mélanger plusieurs bugs non liés.
- Si une ancienne cause connue est fournie, la revalider d’abord dans l’état actuel du repo.

---

## 3. Invariants projet

- Le projet est **track-aware**.
- Toujours raisonner avec :
  - track active
  - family
  - type
  - runtime associé
- Ne pas ajouter une logique “globale” si elle dépend en réalité de la track active.
- L’autorité de binding runtime reste `track_runtime`.
- Les remaps logique/physique doivent rester explicites.
- Ne pas casser le hard real-time audio.

Quand une passe touche un état structurant, identifier d’abord :
- l’autorité réelle
- qui lit cet état
- qui l’écrit
- s’il existe une autorité parasite

---

## 4. Portée des passes

- Faire la plus petite passe locale possible.
- Ne pas partir en refonte si une correction locale suffit.
- Ne pas élargir la passe sans nécessité démontrée.
- Si l’utilisateur demande une passe précise, rester strictement dans ce périmètre.
- Ne pas ramener du contexte d’anciennes passes sans lien direct avec la passe en cours.

---

## 5. Build et validation

- Cible de travail par défaut : `Release`.
- Ne jamais lancer `Debug` ou `Debug1` sauf demande explicite.
- Ne pas lancer de build global par réflexe.
- Pour une passe locale, préférer d’abord :
  - lecture ciblée
  - recherche ciblée
  - diff minimal
  - vérification logique locale
- Ne lancer un build complet que si cela apporte une validation réellement utile à la passe.
- Si le système de build est déjà cassé hors zone touchée :
  - le constater une fois
  - le signaler clairement
  - arrêter les tentatives de build non utiles
- Si `make -C Release ...` échoue à cause du flag toolchain connu `-fcyclomatic-complexity`, ne pas insister.
- Dans ce cas :
  - signaler une seule fois que l’échec vient du build system/toolchain local et non du patch
  - faire `git diff --check`
  - faire une vérification syntaxique ciblée du ou des fichiers modifiés
  - ne pas répéter longuement la même explication à chaque passe
- Ne pas multiplier les contournements si l’échec est externe au patch.

---

## 6. Modification des fichiers

- Utiliser le moyen d’édition le plus simple et le plus fiable.
- Si `apply_patch` échoue 2 fois sur le même fichier :
  - arrêter les retries
  - faire un remplacement local strictement ciblé
  - relire le diff
  - continuer
- Après modification, vérifier que seules les lignes attendues ont bougé.
- Ne pas nettoyer, restaurer ou regénérer massivement les dossiers de build sans nécessité directe.
- Éviter les commandes à effets de bord inutiles.

---

## 7. Sortie attendue en fin de passe

Toujours fournir :
- la cause racine exacte
- quelle hypothèse était correcte ou non
- les fichiers modifiés
- la correction exacte appliquée
- ce qui a été vérifié
- ce qui n’a pas pu l’être

Ne pas présenter comme validé ce qui ne l’est pas.

---

## 8. Définition de “done”

Une passe est “done” seulement si :
- la cause traitée est identifiée clairement
- la correction est locale et cohérente
- le diff est propre
- la validation pertinente pour la passe a été faite
- aucune zone non liée n’a été modifiée inutilement

---

## 9. Plans longs

Pour une passe longue, ambiguë, ou de refonte réelle :
- faire d’abord un plan écrit
- ne pas coder avant d’avoir clarifié :
  - objectif
  - zone touchée
  - contraintes
  - risques de régression
  - validation attendue

Si un `PLANS.md` existe dans le repo, l’utiliser pour ce type de travail.
Sinon, garder un plan court dans la réponse avant implémentation.

---

## 10. Doc

Mettre à jour la doc dans la même passe uniquement si la correction modifie réellement :
- une autorité runtime structurante
- un flux majeur UI/runtime
- une règle d’intégration importante
- l’architecture d’un sous-système

Dans ce cas, mettre à jour :
- `AGENT.md`
- `readme.md`

Ne pas mettre à jour la doc pour une micro-correction locale sans impact structurel.