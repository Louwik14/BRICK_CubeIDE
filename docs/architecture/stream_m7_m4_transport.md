# Frontière locale du futur transport M7 vers M4

L'étape 9 place une mailbox versionnée entre l'ordonnanceur et l'exécuteur I/O.
Le producteur publie une commande immutable comprenant le token, la cible de page
SDRAM et le snapshot complet de la source. Le worker est seul à appeler le module
I/O, donc seul propriétaire futur de FatFs, des fichiers et des lectures SD. Il
retourne un résultat portant le même token. Le publisher côté propriétaire audio
reste seul autorisé à rendre la page `READY`.

En monocœur, `sample_stream_transport_execute_monocore()` enchaîne submit,
worker et collecte sans attente active. Les transitions sont explicites :

```text
EMPTY -> COMMAND_READY -> RESULT_READY -> EMPTY
```

Chaque publication utilise une barrière mémoire, une version ABI et une séquence
non nulle. Une réponse d'une autre séquence n'est pas consommée. Les statistiques
exposent commandes, complétions, refus busy et erreurs de protocole.

Les données audio ne traversent pas la mailbox : la commande désigne directement
la page finale du pool SDRAM, puis le worker la remplit et renvoie seulement son
token. Owners, générations et annulations restent validés lors de la publication
finale. Il n'existe aucun appel FatFs ou transport dans l'IRQ audio.

Le portage H747 remplacera l'appel direct du worker par une notification HSEM et
placera la mailbox dans une région partagée avec une politique MPU/cache définie.
Il devra nettoyer la commande avant notification, invalider la réponse et la page
avant publication M7, et attribuer les métadonnées mutables du cache à un seul
cœur. Cette passe n'active volontairement ni second firmware, ni HSEM, ni DMA SD
asynchrone : ces choix exigent d'abord les mesures matérielles de la passe finale.
