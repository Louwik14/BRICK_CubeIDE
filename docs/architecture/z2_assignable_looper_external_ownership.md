# Looper assignable et propriété des entrées

Les huit pistes logiques utilisent le même catalogue. `Sampler / Looper` peut être choisi sur n'importe quel slot `0..7`; son état runtime, ses prises, ses paramètres, ses p-locks et ses routes restent indexés par ce slot. La limite d'instances reste une capacité de variante (`BRICK6_LOOPER_GLOBAL_CAP`) et non une identité de piste.

Une piste `External` est l'unique propriétaire possible de l'entrée physique qu'elle sélectionne. Une entrée peut ne pas avoir de propriétaire, mais deux pistes ne peuvent pas revendiquer la même entrée. La validation en masse précède l'application des snapshots Pattern, Project et Track afin qu'une structure incompatible échoue sans mutation partielle.

L'identité d'entrée physique est indépendante de la voie mixer. Aucune voie mixer n'est réservée aux entrées au démarrage: seules les pistes audio réellement configurées consomment une voie, allouée comme pour les autres moteurs. Il n'existe plus de repli vers une piste Input fixe.

Les changements Family/Type restent hors historique Undo/Redo. Ils conservent donc le sommet d'historique existant; les remplacements complets Pattern/Project suivent leur contrat d'invalidation centralisé.
