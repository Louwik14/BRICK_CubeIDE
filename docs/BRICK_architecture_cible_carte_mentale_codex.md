# BRICK — Architecture cible / carte mentale Codex

**Statut : architecture de référence avant implémentation**  
**Projet : BRICK groovebox STM32**  
**But : servir de carte mentale stable à Codex pendant les 5 chantiers de refonte**

> Ce document n'est pas un plan de patch fichier-par-fichier.
> Il fixe les autorités, contrats, invariants et frontières de responsabilité.
> Chaque chantier d'implémentation doit respecter ces règles et peut proposer la solution de code la plus simple compatible.

---

# 1. Objectif de la refonte

Le firmware actuel mélange plusieurs notions historiquement proches mais qui doivent devenir distinctes :

```text
track
lane
GROUP child
runtime ctx
mixer target
engine instance
PLAY capacity
engine polyphony
physical voices
```

La cible est une architecture où chaque notion possède **une seule autorité**, et où CONTROL et AUDIO communiquent par un contrat explicite.

```text
                    CONTROL
                       │
             modèle musical canonique
                       │
              commandes / événements
                       ▼
                     AUDIO
                       │
             runtime de réalisation
                       ▼
              moteurs / DSP / mixer
```

## Compatibilité MCU

### H743 — cible obligatoire

```text
CONTROL → contrat explicite → AUDIO
```

Les deux domaines tournent encore physiquement sur le M7, mais doivent déjà respecter la frontière future.

### H747 — évolution future

```text
M4 CONTROL → même contrat métier → M7 AUDIO
```

Le passage H743 → H747 doit surtout changer le placement physique et le transport, **pas le modèle métier**.

Le H747 ne doit jamais être considéré comme fournissant une deuxième réserve complète de SRAM indépendante.

---

# 2. Règle centrale : une notion = une autorité

| Notion | Autorité cible |
|---|---|
| identité logique | CONTROL / topology |
| rôle MAIN / GROUP master / GROUP child | CONTROL / topology |
| membership GROUP | CONTROL / topology |
| PLAY capacity | dérivée du rôle CONTROL |
| contenu PLAY | CONTROL / sequence model |
| droit d'émettre des notes | CONTROL / topology |
| moteur configuré | CONTROL |
| binding moteur réellement installé | AUDIO |
| polyphonie nominale/configurée | CONTROL config |
| disponibilité des voix physiques | AUDIO |
| Note FX | CONTROL |
| base paramètres / p-locks | CONTROL |
| état DSP effectif | AUDIO |
| mute logique | CONTROL |
| mute DSP effectif | AUDIO |
| horloge sample | AUDIO |
| mixer slot | détail AUDIO interne |
| bus GROUP runtime | AUDIO |
| persistance projet | CONTROL |

Les autres sous-systèmes peuvent avoir des caches/projections, mais **pas une deuxième source de vérité**.

---

# 3. Identité logique et topology

## Identités

Il existe 8 entités top-level stables :

```text
entity_id 0..7
```

Lorsque l'entité top-level `7` est en rôle GROUP :

```text
entity 7      = GROUP_MASTER
entities 8..15 = GROUP_CHILD
```

Les children disposent d'une identité logique propre lorsqu'ils sont actifs.

Le terme historique `lane` peut continuer à exister dans le séquenceur si utile, mais ne doit plus devenir une seconde identité métier concurrente.

## Rôles logiques

```text
MAIN
GROUP_MASTER
GROUP_CHILD
```

La topology doit être l'autorité unique permettant de répondre à :

```text
is entity active?
what is its role?
who is its parent/group?
can it emit notes?
what is its PLAY capacity?
```

Éviter les redéductions dispersées via :

```text
lane >= 8
track == 7
runtime.type
engine == NONE
mixer target
```

Une convention numérique interne reste acceptable si elle est encapsulée derrière l'API topology.

---

# 4. Contrat PLAY définitif

PLAY est une **donnée propre du séquenceur**, pas une voix moteur et pas un paramètre DSP générique.

```text
PLAY storage != engine polyphony
PLAY storage != physical voice availability
PLAY storage != can_emit_notes
```

## Capacités

```text
MAIN          : 8 PLAY / step
GROUP_MASTER  : 8 PLAY / step
GROUP_CHILD   : 1 PLAY / step
```

## Émission

```text
MAIN          : can_emit_notes = true
GROUP_MASTER  : can_emit_notes = false
GROUP_CHILD   : can_emit_notes = true
```

Le GROUP master peut donc conserver des PLAY mais ne produit aucune note.

## Changement de moteur

Un changement de moteur ne doit **jamais tronquer ni supprimer les PLAY**.

Exemple :

```text
step = 8 notes
poly 8 → moteur mono → poly 8
```

Les 8 notes restent stockées pendant tout le changement.

---

# 5. Layout physique PLAY

Le layout uniforme ×8 sur les 16 lanes est rejeté comme gaspillage mémoire inutile.

Cible :

```text
8 top-level × 8 PLAY
8 children  × 1 PLAY
```

Projection auditée :

```text
g_seq_project HEAD    ≈ 127 104 B
g_seq_project cible   ≈ 129 664 B
delta                 ≈ +2 560 B
```

Le dernier audit de fermeture a conclu que le modèle complet tient dans les placements actuels Low-Cost/Premium sans déplacement massif de `g_seq_project`.

## Règle API

Le layout physique doit être invisible hors `seq_model`.

Conceptuellement :

```text
play_capacity(entity)
play_get(entity, step, voice)
play_set(...)
play_delete(...)
play_clear(...)
play_iterate(...)
```

Ne pas remplacer les anciens hardcodes par des `if (GROUP_CHILD)` dispersés.

---

# 6. PLAY et registre générique de paramètres

Décision : **option B**.

PLAY reste l'autorité du séquenceur.

Le système générique de paramètres peut exposer V1…V8 si l'UI en a besoin, mais uniquement comme **projection/adaptateur**.

```text
PLAY canonique
├─ voice index
└─ field
   ├─ NOTE
   ├─ VEL
   ├─ LEN
   └─ MICTIM
        │
        ▼
 éventuel mapping param_id
```

Ne pas faire de `PARAM_V1...PARAM_V8` la définition fondamentale de PLAY.

---

# 7. Note FX

Les Note FX musicaux appartiennent à CONTROL :

```text
Probability
Gate
Groove
Harmony / Voicing
Euclid
ARP
```

Ils peuvent :

```text
supprimer
décaler
transformer
générer plusieurs notes
gérer leurs propres occurrences/tokens/note-offs
```

## GROUP children

Décision produit figée : **chaque GROUP child / sub-track possède ses propres Note FX individuels**, comme une track normale.

## Important : PLAY avant limitation moteur

La polyphonie moteur ne doit jamais tronquer les sources PLAY avant Note FX.

Correct :

```text
8 PLAY source
→ Note FX / ARP
→ résultat musical
→ AUDIO admission physique
```

Incorrect :

```text
8 PLAY
→ truncate à la polyphonie moteur
→ ARP
```

Exemple visé : un accord de 8 notes peut alimenter un ARP même si le moteur final est mono.

---

# 8. Pipeline CONTROL cible

```text
SEQUENCE
├─ PLAY
├─ p-locks
├─ timing / microtiming
└─ progression musicale
      │
      ▼
MUSICAL SCHEDULER
      │
      ▼
NOTE FX
      │
      ▼
FINAL TIMESTAMPED EVENTS
```

Après transformation en événement final, AUDIO n'a plus besoin de connaître :

```text
step
PLAY index
LEN
MICTIM
ARP
Euclid
```

CONTROL décide **ce qui doit musicalement arriver**.

---

# 9. AUDIO cible

AUDIO reste propriétaire de tout ce qui concerne la réalisation temps réel :

```text
sample clock
temporal event execution
binding installé
physical admission
voice allocation / stealing
sampler streaming
oscillators / FM
filters
VCA
envelopes DSP
audio-rate modulation
inserts
delay / reverb
mixer
routing effectif
GROUP bus rendering
panic d'exécution
télémétrie RT
```

AUDIO décide **comment rendre physiquement ce que CONTROL demande**.

Il n'y a qu'un seul allocateur/stealing physique : **AUDIO**.

---

# 10. Contrat CONTROL → AUDIO

Les messages doivent être autonomes et transportables plus tard entre M4 et M7.

Contenu conceptuel :

```text
protocol/version
message sequence
entity_id
topology_generation
binding_generation
due_sample
ON / OFF
occurrence/token
note
velocity
route / provenance / flags utiles
```

À ne jamais transporter :

```text
pointer
step index
PLAY index
LEN
MICTIM
mixer slot
engine instance
runtime ctx pointer
```

AUDIO ne doit pas avoir à lire une structure CONTROL mutable pour comprendre l'événement.

---

# 11. Horloge, lookahead et queues

M7/AUDIO reste l'autorité du **sample clock**.

CONTROL planifie des événements futurs avec `due_sample`.

```text
AUDIO sample clock
      │
      └──► CONTROL time snapshot

CONTROL
      │ calcule en avance
      ▼
timestamped events
      │
      ▼
AUDIO temporal queue
      │
      ▼
exact sample execution
```

## Décision actuelle

**Ne pas figer maintenant** :

```text
lookahead exact
taille des queues
marge inter-core
```

Ces valeurs seront mesurées selon :

```text
latence/jitter M4↔M7
tempo max
microtiming négatif
coût CONTROL
fan-out Note FX
marge de sécurité
```

Le contrat doit permettre d'ajuster ces valeurs sans refonte métier.

## Communication H747 future

Préférence architecturale : petite zone IPC partagée, idéalement zero-copy et correctement gérée côté cohérence/cache.

Ne pas partager naïvement tout le modèle CONTROL avec AUDIO.

---

# 12. Binding moteur

`entity_id` reste stable ; le moteur n'est qu'un binding AUDIO.

```text
entity_id
   │
   ▼
audio binding
├─ engine
├─ instance
├─ mixer target
├─ runtime association
└─ binding_generation
```

Lors d'un changement moteur :

```text
binding generation N
→ old binding removed
→ new binding installed
→ binding generation N+1
```

Les événements appartenant à une ancienne génération deviennent stale et doivent pouvoir être rejetés.

Un ancien NOTE OFF ne doit jamais fermer une note du nouveau moteur.

Mixer target, runtime slot et engine instance ne sont jamais des identités logiques.

---

# 13. Paramètres / p-locks

## CONTROL possède

```text
base persistent value
p-lock state
base restoration
modulation configuration
engine configuration
routing configuration
```

## AUDIO possède

```text
effective DSP state
oscillator/filter/envelope runtime
audio-rate modulation
ramps/interpolation nécessaires au rendu
```

Transport possible plus tard :

```text
dated parameter commands
versioned structural snapshots
ramps
hybride
```

Ne pas copier automatiquement un gros snapshot complet à chaque step.

## Capacité p-lock — décision figée

Ne pas refondre ce contrat dans ce chantier global :

```text
maximum 32 p-locks par step
pool fixe 1024 p-locks par track/lane
```

La gestion actuelle de capacité reste un choix produit assumé.

---

# 14. Snapshots / Undo / Clipboard

Décision : **snapshot fixe dimensionné au maximum 8 PLAY**.

Ne pas introduire un format variable uniquement pour économiser quelques octets.

Les snapshots/undo/clipboard contiennent uniquement du **CONTROL state** :

```text
steps
PLAY
p-locks
metadata utile
```

Ils ne contiennent jamais :

```text
engine runtime
DSP state
voice allocator
mixer slots
binding instance
```

Les grosses frames stack existantes (~14–20 KiB dans certaines fonctions) sont un problème structurel séparé ; si une passe les touche, éviter de les aggraver et les nettoyer si naturel.

---

# 15. GROUP

Une seule GROUP par projet.

```text
GROUP
├─ master entity = 7
├─ child entities = 8..15
└─ common state explicite
```

## GROUP master

```text
identité top-level stable
séquence conservée
8 PLAY stockables
can_emit_notes = false
pas de source note propre
```

Les moteurs ne doivent pas connaître le cas GROUP master.

## GROUP children

Chaque child possède :

```text
identité propre
séquence propre
1 PLAY / step
p-locks propres
Note FX propres
runtime/binding propre
source audio propre
```

## GROUP common

Les paramètres réellement communs doivent être explicitement GROUP :

```text
common bus
common inserts
common modulation/LFO selon contrat produit
parent mute
routing commun dédié
```

Ne jamais implémenter un fan-out implicite du type :

```text
master p-lock → recopier magiquement sur tous les children
```

---

# 16. GROUP bus AUDIO

Le GROUP bus devient une vraie notion de routing AUDIO.

```text
child source
   ↓
child processing
   ↓
GROUP BUS
├─ common processing
└─ routing
   ↓
MAIN
```

Le mixer target utilisé pour le réaliser reste un détail interne.

```text
GROUP_BUS != mixer slot number
```

---

# 17. Mute

Autorité :

```text
CONTROL logical mute
```

Puis calcul du mute effectif avec éventuel héritage GROUP :

```text
effective mute
= entity mute + GROUP inheritance
```

Projection vers AUDIO.

Contrat :

```text
mute ON
→ CONTROL cesse les nouveaux NOTE ON
→ AUDIO applique le mute RT
→ NOTE OFF restent acceptés
```

Ne pas conserver plusieurs autorités indépendantes UI/scheduler/runtime/mixer.

---

# 18. Live recording / Step Hold

Live-rec appartient à CONTROL et doit travailler contre `play_capacity(entity)`, jamais contre la polyphonie moteur.

Workflow Step Hold → clavier :

```text
notes jouées → PLAY
VEL = vélocité jouée
MICTIM = 0
anciennes NOTE/VEL PLAY remplacées
LEN existant préservé
autres p-locks conservés
ROLL conservé
```

Quick Length :

```text
plusieurs steps tous vides   → capture autorisée
plusieurs steps tous remplis → capture autorisée
un seul step                 → capture possible sans casser short press
sélection mixed filled+empty → Quick Length prioritaire
```

---

# 19. Persistance

Aucune rétrocompatibilité projet n'est requise.

À supprimer/remplacer :

```text
PatternSaveV1
ProjectSaveV1
struct dumps legacy
migration d'anciens projets
```

Nouveau format canonique explicitement sérialisé, conceptuellement :

```text
HEADER
PROJECT
TOPOLOGY
ENTITY CONFIG
SEQUENCE
PLAY
PLOCKS
NOTE FX
PARAM BASES
GROUP COMMON
ROUTING
ASSET REFERENCES
```

Aucun runtime AUDIO ne doit être sauvegardé.

Ne pas dépendre de `sizeof(struct)` / padding natif comme format disque.

---

# 20. Mémoire — invariants de pilotage

Régions actuellement serrées :

```text
DTCM : ~8 KiB libres au HEAD audité
D1 LC : ~20 KiB libres
D2 : ~18–19 KiB libres avant projection PLAY
SDRAM : ~0,5 MiB libre
```

Audit PLAY 8/8/1 conservateur :

```text
DTCM   +640 B
D2     +3 920 B
SDRAM  +14 292 B
```

Le modèle tient Low-Cost/Premium sans déplacement massif.

## Règle

Ne pas déplacer `g_seq_project` ou faire une refonte mémoire globale sans mesure prouvant qu'elle est nécessaire.

Ne jamais supposer :

```text
M4 = RAM gratuite
H747 = deuxième copie de SRAM
```

---

# 21. Déterminisme AUDIO

Le futur chemin CONTROL→AUDIO doit respecter :

```text
aucune allocation dynamique dans AUDIO
aucun mutex bloquant dans IRQ audio
aucun pointeur CONTROL dans les événements
queues de taille fixe / bornée
coût borné
overflow détectable
late events détectables
stale events rejetables
télémétrie consultable hors IRQ
```

Le worst-case :

```text
8 PLAY → Note FX / Harmony / ARP → fan-out
```

sera mesuré pendant le chantier CONTROL↔AUDIO/scheduler afin de dimensionner queues/lookahead.

---

# 22. Builds et validation

Pour cette refonte, validations obligatoires :

```text
Low-Cost Release
Premium
```

Debug et anciennes variantes de build sont hors priorité sauf dépendance directe.

H743 doit rester fonctionnel après chaque chantier clôturé.

Aucun push automatique.

Commits locaux propres par passe une fois l'implémentation commencée.

---

# 23. Anti-objectifs / choses à ne pas préserver

La migration doit supprimer progressivement :

```text
tests numériques de rôle dispersés
polyphonie moteur utilisée comme PLAY capacity
accès directs step->play hors modèle
scheduler appelant directement les moteurs
Note FX couplés au runtime AUDIO
mixer target utilisé comme identité
GROUP reconstruit localement partout
mutations CONTROL directes vers structures DSP
formats persistence V1
autorités redondantes
wrappers temporaires devenus inutiles
code mort issu des anciens chemins
```

Ne pas maintenir deux architectures en parallèle par simple conservatisme.

---

# 24. Découpage officiel de la refonte — 5 chantiers

## Chantier 1 — MODEL

```text
Identity / Topology / Runtime / Binding
```

Objectif :

```text
entity_id stable
→ topology unique
→ audio binding distinct
```

Traiter notamment :

```text
MAIN/GROUP_MASTER/GROUP_CHILD
membership
can_emit_notes
suppression des identités implicites runtime/mixer/engine
binding_generation foundation
```

Ne pas encore refondre PLAY, scheduler ou GROUP bus.

---

## Chantier 2 — CONTROL ↔ AUDIO

```text
frontière explicite
+ scheduler musical
+ Note FX
```

Objectif :

```text
sequence / PLAY / Note FX
→ final timestamped events
→ AUDIO admission / engines
```

Traiter :

```text
queue H743 monocore
sample timestamps
tokens/generations
sortie scheduler/Note FX du domaine AUDIO
worst-case event burst
lookahead / queue sizing mesurés
```

Clôture idéale : AUDIO ne connaît plus step/PLAY/LEN/MICTIM/ARP/Euclid.

---

## Chantier 3 — PLAY complet

```text
model 8/8/1
+ UI
+ edit
+ live-rec
+ undo/clipboard
```

Traiter :

```text
layout top-level×8 / child×1
API PLAY canonique
suppression hardcodes 4
mapping UI V1…V8 comme projection
PLAY 1/2 et 2/2
Step Hold
live-rec
snapshots fixes 8
undo/clipboard
```

Pool p-lock inchangé.

---

## Chantier 4 — GROUP complet

Traiter :

```text
master + 8 children
Note FX individuels children
GROUP common
GROUP bus AUDIO
mute inheritance
common inserts/modulation selon contrat produit
routing GROUP
suppression des déductions GROUP dispersées
```

---

## Chantier 5 — STORAGE + CLEANUP

Traiter :

```text
nouveau format persistence canonique
suppression PatternSaveV1 / ProjectSaveV1
suppression wrappers temporaires
suppression anciennes autorités
recherche code mort
RAM/stack audit final
architectural audit final
Low-Cost Release + Premium validation
```

---

# 25. Dépendances entre chantiers

Ordre recommandé :

```text
1 MODEL
   ↓
2 CONTROL↔AUDIO
   ↓
3 PLAY
   ↓
4 GROUP
   ↓
5 STORAGE + CLEANUP
```

Les chantiers ne sont pas conçus pour être implémentés en parallèle.

Chaque chantier doit laisser un état compilable et clôturable avant le suivant.

---

# 26. Format de handoff entre conversations

À la fin de chaque chantier, produire un handoff court contenant uniquement :

```text
CHANTIER X — PASS / FAIL
commit(s)
contrats désormais vrais
API introduites/supprimées
changements structurels
build Low-Cost Release
build Premium
mesures RAM/stack utiles
wrappers temporaires encore présents
chantier prévu pour leur suppression
points volontairement hors périmètre
blockers réels éventuels
```

Objectif : quelques dizaines de lignes, pas un nouveau dossier de 40 pages.

---

# 27. Questions volontairement non figées

Ces valeurs ne doivent pas être inventées avant mesure :

```text
lookahead CONTROL→AUDIO exact
taille exacte des queues
jitter/latence réelle M4↔M7
stratégie IPC H747 finale
```

Le contrat doit simplement permettre leur calibration future.

---

# 28. Résumé mental ultra-court

```text
ENTITY
│
├─ TOPOLOGY (CONTROL)
│   ├─ role
│   ├─ membership
│   ├─ can_emit_notes
│   └─ PLAY capacity
│
├─ SEQUENCE (CONTROL)
│   ├─ PLAY
│   ├─ p-locks
│   └─ Note FX
│
└─ AUDIO BINDING
    ├─ generation
    ├─ engine / instance
    ├─ physical admission
    ├─ voices / DSP
    └─ mixer / routing
```

Puis :

```text
CONTROL
sequence + PLAY + Note FX
        │
        ▼
final timestamped commands
        │
        ▼
AUDIO
sample clock + binding + admission + DSP
```

PLAY :

```text
MAIN          = 8
GROUP_MASTER  = 8, no note emission
GROUP_CHILD   = 1
```

GROUP child :

```text
séquence propre
p-locks propres
Note FX propres
```

H743 : même contrat sur un seul M7.  
H747 : M4 CONTROL → M7 AUDIO, sans nouvelle refonte métier.

---

# 29. Règle de travail pour Codex

Avant chaque patch, vérifier :

1. **Quelle notion est modifiée ?**
2. **Qui en est l'autorité selon ce document ?**
3. **Le patch crée-t-il une nouvelle source de vérité ?**
4. **Le patch fait-il fuiter une identité AUDIO dans CONTROL ou inversement ?**
5. **Le patch dépend-il prématurément du H747 ?**
6. **Le patch conserve-t-il un legacy uniquement par habitude ?**
7. **La validation Low-Cost Release + Premium est-elle définie ?**

Si une décision produit nécessaire n'est pas présente ici, la signaler comme ouverte au lieu de l'inventer.
