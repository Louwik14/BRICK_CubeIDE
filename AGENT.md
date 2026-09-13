# AGENT.md

## Role documentaire

Le code courant est l'autorite finale.

`docs/architecture/ARCHITECTURE_GLOBAL.md` est l'unique porte d'entree documentaire.
Les documents de domaine qu'il reference portent les contrats techniques actuels de leur zone.

BRICK est un prototype : ne pas imposer de compatibilite avec d'anciens formats,
migrations ou comportements historiques sauf demande explicite.

## Mode de travail

* Auditer l'autorite canonique et ses consommateurs avant toute mutation.
* Corriger la cause racine, pas seulement le symptome observe.
* Preserver les changements utilisateur et les travaux hors perimetre.
* Ne pas creer de seconde source de verite.
* Ne pas introduire de refonte generale sans gain net et proportionne.
* Ne pas ajouter d'allocation dynamique, lock ou attente bloquante dans le runtime critique.
* Ne pas ajouter de cout AUDIO non borne.
* Mettre a jour la documentation proprietaire lorsqu'un invariant architectural change.

## Autorite produit

CONTROL est l'autorite produit.

CONTROL decide :

```text
quoi
ou
quand
```

CONTROL porte notamment :

```text
etat canonique
validation produit
admission
scheduler
timestamps
```

Une representation canonique traverse ensuite les frontieres necessaires.

Une couche aval ne doit pas redecouvrir l'intention produit ni posseder une copie
independante du meme contrat.

## Frontiere CONTROL -> AUDIO

AUDIO :

```text
valide uniquement ce qui est local a son domaine
resout logical -> physical
gere ses ressources physiques
applique
rend
```

Une operation produit acceptee par CONTROL ne doit normalement pas etre rejetee
plus tard parce qu'une couche aval possede un contrat divergent.

Un refus tardif n'est legitime que pour une impossibilite reellement locale, par exemple :

```text
corruption
index physique impossible
ressource reellement indisponible
capacite physique locale impossible
violation hard-RT
```

Ne pas utiliser un ACK/NACK aval pour deplacer une decision produit hors de son autorite.

## Autorites et projections

Chaque concept possede une seule autorite canonique.

Toute projection :

```text
UI
runtime
mapping
cache
wire
vue physique
```

doit etre derivee de cette autorite et ne jamais devenir une autorite concurrente.

Une representation derivee doit pouvoir etre reconstruite depuis sa source canonique.

Les identites logiques, identites de lifetime, ressources physiques, quotas et voies
d'execution sont des concepts distincts.

Ne pas confondre une ressource d'implementation avec une identite produit.

## Transactions et commit

Toute mutation structurelle ou restauration multi-etapes doit conserver un etat coherent.

Modele cible :

```text
ancien etat coherent
-> preparation / validation
-> commit
-> nouvel etat coherent
```

Une mutation ne doit pas rendre visible un nouvel etat canonique alors que ses projections
necessaires sont encore sur l'ancien.

En cas d'echec avant commit, l'ancien etat doit rester coherent.

Eviter tout etat partiel du type :

```text
autorite = B
projection 1 = B
projection 2 = A
```

## Admission et capacites

Toute limite bornee previsible doit etre prise en compte avant l'execution critique.

Une operation admise ne doit pas decouvrir en cours d'execution qu'elle depasse une
capacite qui pouvait etre connue ou bornee en amont.

Ne pas corriger une saturation uniquement en augmentant une constante.

Identifier d'abord la nature de la limite :

```text
capacite produit
capacite physique
accumulation temporelle
fan-out
lifetime
lookahead
retard
```

## Ordre causal

Preserver l'ordre causal lorsque l'ordre des operations porte une semantique observable.

Ne pas regrouper, differer ou reordonner des operations si cela peut modifier :

```text
le resultat produit
le lifetime d'une ressource
l'etat visible par une autre couche
```

Lorsqu'une operation de nettoyage peut legitiment rencontrer une cible deja absente,
preferer une semantique idempotente plutot qu'un echec tardif artificiel.

## Fatals et late failures

Un chemin produit normal et valide ne doit pas finir en fatal pour une situation
previsible.

Les fatals servent aux violations d'invariants internes, pas au controle de flux produit.

Lorsqu'un fatal apparait :

```text
identifier l'autorite
identifier la premiere divergence
identifier pourquoi elle n'a pas ete empechee plus tot
```

Ne pas supposer automatiquement que la couche emettrice est fautive ni que la couche
qui detecte l'erreur possede le bon contrat.

## Simplicite

Principe :

```text
LESS IS MORE
```

Rechercher :

```text
moins de sources de verite
moins de representations semantiques
moins de conversions
moins de validations repetees
moins de late failures
```

Mais ne pas supprimer une complexite necessaire a :

```text
hard-RT
H747
scheduler
timestamps sample-accurate
logical -> physical
resource lifetime
STORAGE asynchrone
```

Ne pas mesurer la simplicite au nombre de fichiers ou de couches.

Une couche est justifiee si elle porte une responsabilite reelle et non dupliquee.

## Complexite necessaire vs accidentelle

Ne pas confondre :

```text
complexite intrinseque
```

avec :

```text
complexite accidentelle
```

Sont typiquement legitimes si necessaires au contrat :

```text
scheduling
lookahead
timestamps
mapping physique
allocation de ressources
lifetime
fences
asynchronisme
```

Sont suspects lorsqu'ils n'ajoutent aucune information nouvelle :

```text
encodages multiples du meme concept
tables dupliquees
validations produit repetees
etat canonique duplique
repacking sans nouvelle responsabilite
compatibilite historique inutile
```

## Changements architecturaux

Eviter sans justification forte :

```text
registry universel
state machine generale supplementaire
ACK business inter-domaines
locks / mutex dans le runtime critique
allocation dynamique runtime
reecriture globale d'un sous-systeme
```

Preferer une simplification locale lorsque le gain architectural est equivalent.

## Persistence

Les objets persistants ont un format courant unique sauf besoin explicite contraire.

Aucune retrocompatibilite avec d'anciens formats n'est exigee par defaut.

Un SAVE annonce comme reussi doit etre durable et relisible apres redemarrage.

Un objet invalide ou corrompu doit rester administrable sans exiger que son contenu
puisse etre charge avec succes.

## Topologie

La topologie logique est une autorite de domaine.

Les roles, parents, activites et capacites doivent etre derives de cette autorite,
pas reconstruits independamment dans plusieurs couches.

Une ressource physique, un quota ou une voie d'execution ne doit jamais devenir
implicitement une identite logique.

## Critere de convergence

Une architecture est consideree convergee lorsque :

```text
chaque couche a une responsabilite claire
chaque concept possede une seule autorite
les projections sont derivees
les validations tardives restent locales
les representations intermediaires inutiles sont supprimees ou justifiees
aucune refonte majeure supplementaire n'apporte un gain proportionne au risque
```

La cible n'est pas une architecture parfaite.

La cible est une architecture :

```text
simple
comprehensible
robuste
compatible hard-RT
compatible H747
avec une surface de divergence minimale
```
