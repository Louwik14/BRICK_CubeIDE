# Z2 - Autorite des pistes et ressources

`track_state` est l'autorite canonique des seize configurations `brick_entity_id_t`. `entity_topology` est l'unique autorite d'activite, role MAIN/GROUP_MASTER/GROUP_CHILD, parent, membre et capacites. `track_runtime` projette cette identite vers `track_audio_binding_t`: entite, moteur, instance, cible mixer, etat et generation.

Une mutation structurelle suit toujours: prevalidation globale, commit canonique, invalidation/refresh runtime puis synchronisation UI. Un getter runtime ne cree pas une seconde autorite.

Le restore AUDIO-first installe d'abord les bindings prepares. Apres commit AUDIO, CONTROL reconstruit ensuite `track_runtime` depuis `track_state`, valide les entites actives contre le snapshot AUDIO et marque la projection propre sans republier de binding. Cette projection non emettrice precede tout filtrage de parametre et toute synchronisation UI.

Le GROUP master est lie au bus post-somme, sans moteur de notes. Les children conservent leur configuration meme inactifs. Le mute CONTROL de chaque entite est local; le mute effectif child derive du local ou du parent.

Looper est `Sampler / Looper` et peut occuper tout top-level `0..7`. Son quota global est une capacite de variante, pas une identite. Etat, prises, parametres, p-locks et routes restent indexes par l'entite.

External est l'unique proprietaire possible de l'entree physique selectionnee. `track_input_ownership` interdit deux proprietaires pour une entree. L'identite d'entree est independante de la voie mixer et aucune voie fixe n'est reservee au boot.

Les snapshots et clipboards transportent des etats logiques. Un child se copie localement; le GROUP master capture ou restaure le master et ses huit children. Pattern et Project utilisent directement les seize identites.
