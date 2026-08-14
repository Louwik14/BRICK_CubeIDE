# BRICK — Handoff chantier 5/5 après reset de pilotage

Date : 2026-08-13

## Rôle du prochain ChatGPT

Piloter Codex pour terminer le chantier 5/5 de BRICK sans repartir dans les boucles de la conversation précédente.

Le rôle n’est pas de coder directement, mais de :

- challenger les audits Codex ;
- distinguer vrai défaut architectural et pinaillage ;
- éviter les rustines et la surarchitecture ;
- produire les prompts successifs ;
- expliquer au user, très brièvement et sans jargon, ce qui se passe et pourquoi ça compte.

Format de réponse préféré au user :

**Verdict → en clair → enjeu → décision → prompt suivant.**

Les détails techniques doivent surtout aller dans les prompts Codex.

---

# 1. État global des 5 gros chantiers

1. Identity / Topology / Runtime / Binding — **CLÔTURÉ**
2. CONTROL↔AUDIO / Scheduler / Note FX — **CLÔTURÉ**
3. PLAY 8/8/1 — **CLÔTURÉ**
4. GROUP complet — **CLÔTURÉ**
5. Persistance canonique + cleanup architectural final — **EN COURS**

Ne pas réouvrir les chantiers 1–4 sauf contradiction réelle trouvée dans le code.

Le chantier 5 est le dernier chantier avant validation globale de la grosse refonte.

---

# 2. Règles de travail toujours valables

- Aucun nouveau test Codex par défaut.
- Pas d’instrumentation runtime pour latence/jitter/etc. avant le vrai H747.
- Préférer : audits statiques, invariants, recherches négatives, contrôle de diff, builds Low-Cost Release + Premium.
- Aucun push automatique.
- Commits locaux par passe d’implémentation.
- H743 doit rester supporté.
- H747 est préparé architecturalement, mais ne doit pas contaminer le code actuel par des dépendances prématurées.
- Aucune rétrocompatibilité avec les anciens projets n’est requise.
- Meilleur rapport **simplicité / robustesse** : ne pas construire un système théoriquement parfait si une solution plus simple répond au besoin produit.
- Chaque nouvelle abstraction doit résoudre un problème concret actuel.

---

# 3. Ce que le chantier 5 cherche réellement à obtenir

Le but n’est pas de rendre le séquenceur théoriquement remplaçable ni de lancer un chantier 6 caché.

Le but est :

1. refaire proprement la sauvegarde Project / Pattern / Patch ;
2. arrêter les struct-dumps V1 liés directement aux layouts C ;
3. sauvegarder uniquement l’intention logique CONTROL ;
4. ne jamais utiliser un état physique AUDIO/runtime comme identité persistante ;
5. sauvegarder correctement le produit actuel, notamment GROUP, MOD et les banks d’assets ;
6. supprimer ensuite les vraies anciennes autorités V1 restantes ;
7. vérifier à la fin que la frontière CONTROL↔AUDIO reste propre pour la future migration H747 M4/M7.

Le critère H747 est donc :

> Le passage H743 monocore → H747 M4 CONTROL / M7 AUDIO ne doit pas nécessiter de redistribuer les responsabilités métier déjà refondues.

Le vieux format Project n’est pas intrinsèquement incompatible H747. Sa refonte est faite parce qu’il ne représente plus proprement le produit actuel et reste trop mêlé aux anciennes structures/runtime.

---

# 4. Contrats acquis des chantiers précédents utiles à la persistance

## Identité / topology

- `entity_id` est l’identité logique canonique.
- Topology est l’autorité rôle/activité/parenté.
- Mixer target, runtime slot, engine instance ne sont jamais une identité logique.

## CONTROL / AUDIO

CONTROL possède notamment :

- séquenceur ;
- timing musical ;
- PLAY / p-locks ;
- Note FX ;
- MIDI ;
- configuration logique ;
- modulation logique.

AUDIO possède notamment :

- bindings physiques ;
- engine instances ;
- mixer targets ;
- polyphonie physique ;
- moteurs / DSP / mixer ;
- résolution physique finale de la modulation.

La persistance doit sauvegarder le CONTROL logique, jamais le runtime AUDIO.

## PLAY

- Track top-level normale : 8 PLAY.
- GROUP master : 8 PLAY stockés mais non exposés dans son UI actuelle.
- GROUP child : 1 PLAY.

## GROUP final

- Une seule GROUP par projet.
- Entity 7 peut être une track normale ou devenir GROUP master si GROUP est configuré.
- GROUP master n’émet aucune note.
- Entities 8..15 = GROUP children lorsque GROUP actif.
- Chaque child possède sa configuration/moteur, TONE, ENV, MIX, PLAY, p-locks et 3 Note FX propres.
- Le moteur child peut être mono ou stéréo ; le chemin mono optimisé est utilisé quand la source est mono.
- Une seule Mod Matrix est partagée par la GROUP et possédée par le master.
- Les sources de cette matrice sont celles du master ; les ENV children ne servent pas de sources.
- Les destinations peuvent viser le master ou un child précis (`SUBx + paramètre`).
- Les p-locks du master ne ciblent que les paramètres master.
- Le dry des children va vers le vrai bus AUDIO GROUP ; leurs sends restent individuels avant la somme.
- Le master traite ensuite la somme et possède son MIX/sends/routing.
- Mute effectif child = mute child local OU mute parent GROUP.
- Copier le GROUP master copie toute la GROUP, children inclus.

---

# 5. Vision simple de la persistance cible

Le prochain pilotage doit repartir de cette carte simple, pas des anciens plans compliqués.

## PROJECT contient

- les métadonnées/globals réellement Project ;
- macros/scènes ;
- la/les banks logiques d’assets chargés nécessaires au projet ;
- working Pattern ;
- Pattern actif ;
- banque de Patterns ;
- autres états logiques Project réellement utilisateur.

## PATTERN contient

- configuration logique des entities ;
- GROUP actif/inactif ;
- séquences ;
- PLAY ;
- p-locks ;
- Note FX ;
- paramètres TONE / ENV / MIX ;
- MOD ;
- mute ;
- routing ;
- globals réellement Pattern.

## PATCH contient

- réglages sonores logiques d’une track ;
- family/type ;
- paramètres pertinents ;
- références logiques d’assets nécessaires selon le contrat produit existant ;
- pas de nouvelle feature Patch inventée pendant la migration.

## Ne doivent jamais être sauvegardés comme identité

- `mix_track_id` ;
- engine instance ;
- binding / binding generation ;
- voice/runtime slot ;
- mixer slot ;
- cache DSP ;
- pointeurs/buffers ;
- état loaded/loading/error ;
- phases/enveloppes runtime ;
- mute effectif hérité ;
- bus GROUP physique ;
- page UI / sélection UI ;
- playhead / notes actives / queue runtime.

---

# 6. Contrat asset clarifié par le user

Ce point a provoqué plusieurs faux départs ; ne pas le compliquer à nouveau.

L’utilisateur veut un modèle basé sur **des banks logiques chargées avec le projet**.

Exemple :

```text
bank sample
slot logique 12 → /Samples/snare.wav
```

Alors :

```text
SAMPLE = 12
p-lock SAMPLE = 12
```

Le p-lock n’a PAS besoin de sauvegarder le chemin du fichier.

Au load :

1. le projet recharge la bank dans le même ordre logique ;
2. le slot logique 12 désigne à nouveau `snare.wav` ;
3. le runtime peut charger ce fichier dans n’importe quel emplacement physique disponible ;
4. une table interne traduit slot logique → slot runtime si nécessaire.

Même principe pour les wavetables.

Donc distinguer strictement :

- **index logique de bank** : fait partie du projet et peut être utilisé par paramètres/p-locks ;
- **slot/index physique runtime** : détail interne reconstruit au load et jamais identité persistante.

### Banks produit identifiées par le dernier audit

- Stream/RAM : bank de fichiers chargés + ordre logique de sélection.
- Wavetable : bank de fichiers chargés + ordre logique ; OSC1 et OSC2 utilisent des indices logiques de cette bank.
- Multi : choix logique = fichier d’index d’instrument ; ses zones/samples internes sont reconstruits depuis cet index.
- Aucune autre bank utilisateur persistante identifiée à ce stade.

Le projet doit recharger ses samples/wavetables chargés. Ne pas inventer des références fichier complètes dans chaque p-lock.

---

# 7. Travaux chantier 5 déjà implémentés et validés

## 7.1 DTO CONTROL canoniques

Commit :

`1c926f61117d74b3d063ef849c4c650c5fdc1b28`

Ajout de DTO canoniques Project / Pattern / Patch :

- GROUP 16 entities représentable ;
- PLAY 8/8/1 ;
- MOD représentable ;
- assets logiques prévus ;
- exclusion des états AUDIO/runtime/UI.

Builds Low-Cost Release + Premium : PASS.

## 7.2 Codec canonique explicite

Commit :

`a630d8d8a106ba4b0a3637d65f57a75ada729833`

Ajout :

- codec little-endian explicite ;
- sections versionnées ;
- CRC32 ;
- aucun struct-dump comme nouveau format.

Builds : PASS.

### Décision postérieure importante

Le staging Project complet d’environ **1,19 Mio** et le rollback transactionnel intégral ont été abandonnés.

Ce staging n’était pas nécessaire à l’architecture cible ; c’était un choix de robustesse trop coûteux en RAM.

Cible retenue :

```text
fichier
→ prévalidation simple (header/taille/CRC/structure)
→ décodage progressif par unité
→ application CONTROL
→ reconstruction normale du runtime AUDIO
```

Pas de copie complète de tout le Project en RAM.

## 7.3 Passe A — clés persistantes CONTROL

Commit :

`12310880e7da6d5b61c4dfd4e79cb300b5b14a5a`

PASS.

Ajouts :

- catalogue paramètres et conversions stables ;
- MOD ;
- family/type ;
- MIDI/input ;
- clock ;
- Note FX ;
- LFO ;
- record modes.

Entity 7 normale vs GROUP master est désormais dérivée du type GROUP actif.

Coût : 0 octet RAM ; ~5,1 Kio Flash avant LTO.

Builds : PASS.

## 7.4 Passe B — façade Pattern CONTROL canonique

Commit :

`5e917a8ddc490ae6b5222118c7a5fdd729c6b119`

PASS.

Ajouts :

- capture/validation/apply Pattern canonique ;
- 16 entities ;
- GROUP actif/inactif ;
- routing CONTROL 16×16 ;
- disparition de `PatternSaveV1` comme intermédiaire nécessaire dans cette façade.

Builds : PASS.

Doc : `z6_pattern_control_canonical.md`.

## 7.5 Passe C — autorités Project CONTROL

Commit :

`0d2e8e558`

PASS.

Ajouts :

- autorités CONTROL macros/scènes ;
- assets logiques entity→asset ;
- façade Project canonique.

Coût : +6112 octets SDRAM.

`project_v1` restait actif pour le disque/runtime historique.

Builds : PASS.

## 7.6 Passe Pattern déterministe après audit exhaustif

Commit : hash non fourni dans la conversation de pilotage.

PASS.

Résultat rapporté par Codex :

- PLAY sparse rendu réversible ;
- reset CONTROL déterministe avant apply ;
- MOD complet par owner ;
- routes `enabled` et destination `NONE` restaurées correctement ;
- p-locks et validation renforcés ;
- anciennes valeurs ne doivent plus survivre par simple omission d’un champ sparse.

Assets/p-locks asset-valued volontairement laissés pour la passe suivante.

Builds Low-Cost Release + Premium : PASS.

Doc : `z6_pattern_control_pass1.md`.

## 7.7 Micro-patch de prévalidation Project

Commit :

`eab2e737c`

La tentative suivante de Passe 2 a FAIL, mais ce micro-patch a été conservé :

- prévalidation complète avant mutation ;
- assets/macros appliqués avant le working Pattern.

Builds : PASS.

Docs passes 1/2 mises à jour.

---

# 8. Faux départs / décisions abandonnées à ne pas réintroduire

## Gros staging transactionnel Project

ABANDONNÉ.

Ne pas réserver ~1,19 Mio pour une copie complète du Project uniquement afin d’avoir un rollback parfait.

Un workspace Pattern temporaire d’environ 500 Kio est acceptable s’il est réutilisé et rentre dans le budget.

## Référence fichier complète dans chaque p-lock asset

ABANDONNÉ / inutile pour le produit.

Les p-locks gardent une valeur logique `N` dans la bank du projet.

## Framework générique de sérialisation / reflection / repository / asset manager futur-proof

À éviter.

Privilégier quelques fonctions explicites et des structures simples.

## « Séquenceur totalement remplaçable » comme objectif chantier 5

ABANDONNÉ comme objectif propre.

Le vrai objectif est la frontière CONTROL↔AUDIO suffisamment propre pour H747.

---

# 9. Dernier audit avant reset : deux blocages encore ouverts et compris

Après l’échec de la Passe 2, un audit ciblé a conclu :

`2 BLOCAGES FERMÉS ET COMPRIS`

## Blocage 1 — banks d’assets

### Autorité actuelle legacy

- `ProjectSaveV1.sample_pool`
- `sample_autoload[]`
- `multi[]`

### Stream / RAM

- contenu logique : fichiers chargés et ordre de sélection ;
- `SAMPLE` transporte aujourd’hui un index lié à `sample_global_pool`, ensuite résolu vers un slot Stream/RAM physique ;
- il faut séparer explicitement index logique de bank et slot runtime.

### Wavetable

- contenu logique : fichiers chargés et ordre de sélection ;
- `OSC1_TABLE` / `OSC2_TABLE` utilisent encore directement un `global_slot`, ensuite résolu vers `wavetable_slot` ;
- cible : ces paramètres utilisent un index logique de bank Wavetable.

### Multi

- contenu logique : fichier d’index d’instrument ;
- `instrument_id` sert encore à la fois d’identité de sélection et de slot `multi_sample_pool` ;
- les zones/samples internes restent reconstructibles depuis le fichier Multi et ne doivent pas être persistés comme snapshots physiques.

### Autorité cible proposée par l’audit

`persist_control_project_t.assets[]` représentant chaque entrée logique par quelque chose de l’ordre de :

```text
{ slot/id logique, kind, path }
```

Le détail exact doit rester simple et correspondre aux banks réellement utilisées.

Le runtime peut maintenir des tables bornées de traduction :

- sample logique → runtime ;
- wavetable logique → runtime ;
- Multi logique → runtime.

Ces tables ne sont pas des données persistantes.

### Limite actuelle

`project_control` ne représente encore qu’un petit nombre de références rattachées aux entities et ne représente pas les banks complètes ; `project_control_apply_assets()` ne charge pas encore réellement les fichiers.

## Blocage 2 — banque de Patterns SD canonique

Ce qui est déjà prêt :

- `persistent_pattern_control_capture/apply()` sait traiter le Pattern vivant canonique ;
- le codec Project sait fournir/consommer un `persist_control_pattern_record_t` un par un ;
- slot logique `(bank, pattern)`, présence, working Pattern et Pattern actif existent.

Ce qui reste legacy :

- `pattern_sd_bank_*` ;
- `pattern_live_capture_to_slot()` ;
- chargement différé ;
- `project_sd_bank` ;

lisent/écrivent encore `PatternSaveV1`.

### Pièce manquante

Un adaptateur SD canonique unitaire permettant :

- lire/écrire un `persist_control_pattern_t` pour un slot ;
- énumérer les slots présents ;
- provider ordinal pour l’encodage Project ;
- consumer `put/commit/abort` pour la restauration.

Objectif : un Pattern canonique à la fois, jamais les 256 simultanément.

---

# 10. Pourquoi le chantier 5 est devenu confus

Le code n’est pas forcément à jeter.

Le problème principal a été le pilotage :

- des passes ont été lancées avant que tous leurs prérequis soient cartographiés ;
- certaines solutions de robustesse ont été prises pour des exigences architecturales ;
- le modèle asset a été rendu trop compliqué avant de revenir au fonctionnement réel du produit ;
- plusieurs audits ont découvert un nouveau prérequis après chaque FAIL.

Les chantiers 1–4 ne sont pas remis en cause.

Le chantier 5 doit maintenant être **replanifié à partir de l’état réel du code**, en conservant les bons commits déjà faits mais en jetant les anciens plans de passes comme source d’autorité.

---

# 11. Première mission recommandée dans la nouvelle conversation

Ne pas coder immédiatement.

Faire un **reset de pilotage** :

1. lire ce handoff entièrement ;
2. inspecter l’état actuel du code/commits ;
3. repartir de la vision simple Project / Pattern / Patch / banks d’assets ci-dessus ;
4. vérifier ce qui est réellement terminé et ce qui manque encore ;
5. produire un plan minimal et fermé pour terminer le chantier 5 ;
6. idéalement 2–3 grosses passes utiles, pas une succession de micro-prérequis ;
7. ne pas remettre en cause les décisions produit acquises.

Le prochain audit doit surtout répondre simplement :

> « Depuis l’état actuel du code, quel est le chemin le plus court et propre pour que le nouveau Project/Pattern/Patch remplace réellement V1 ? »

---

# 12. Style de pilotage demandé par le user

Pour chaque retour Codex :

- réponse courte ;
- expliquer sans jargon, comme à quelqu’un qui ne code pas ;
- donner juste assez d’information pour que le user puisse vérifier que le raisonnement tient ;
- éviter les pavés ;
- détails techniques dans le prompt Codex ;
- ne jamais présenter une hypothèse comme une décision produit acquise.

Format recommandé :

**Verdict → en clair → enjeu → décision → prompt suivant.**
