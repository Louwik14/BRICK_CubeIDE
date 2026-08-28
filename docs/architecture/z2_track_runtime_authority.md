# Z2 - Autorite des pistes et programmes

`track_state` est l'autorite canonique des seize configurations
`brick_entity_id_t`. `entity_topology` possede l'activite, les roles
MAIN/GROUP_MASTER/GROUP_CHILD, le parent, le membre et les capacites.
`track_runtime` derive uniquement l'etat CONTROL necessaire aux catalogues,
routes et descripteurs PROGRAM.

Une mutation structurelle suit: prevalidation globale, commit canonique,
reconstruction synchrone de `track_runtime`, publication PROGRAM, puis
synchronisation UI. Les getters sont de pures lectures. Navigation, keyboard,
scheduler et PLAY ne reconstruisent rien et ne declenchent aucune publication.

AUDIO conserve seulement son contexte local d'execution par entite. Il ne le
republie pas vers CONTROL. Il n'existe aucun etat public d'installation, aucune
generation musicale, aucun ACK, aucune comparaison desired/applied et aucun
retry. Un PROGRAM pre-valide est obligatoire; un echec interne est un diagnostic
d'invariant.

Le restore valide d'abord son plan au fence physique existant, puis CONTROL
reconstruit son etat et publie PROGRAM/PARAM/TRANSPORT. AUDIO ne reconstruit ni
Project, ni Pattern, ni projection de piste.

Le GROUP master utilise le bus post-somme sans moteur de notes. Les children
conservent leur configuration meme inactifs. Le mute CONTROL de chaque entite
est local; le mute effectif child derive du local ou du parent.

Looper est `Sampler / Looper` et peut occuper tout top-level `0..7`. Son quota
global est pre-valide par CONTROL. Etat, prises, parametres, p-locks et routes
restent indexes par l'entite.

External est l'unique proprietaire possible de l'entree physique selectionnee.
`track_input_ownership` interdit deux proprietaires pour une entree. Les
snapshots et clipboards transportent uniquement des etats logiques CONTROL.
