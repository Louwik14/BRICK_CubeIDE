# Z3 - Parametres, modulation et controle

Le registre decrit chaque parametre par ID, domaine, plage, affichage, persistance, p-lockabilite, politique de valeur et backend. Les parametres d'entite sont appliques avec leur entity; les parametres Master sont globaux.

Autorites d'ecriture:

- global: `param_set`;
- entite: `param_registry_apply_track_value`;
- configuration LFO: `mod_lfo_v1_set_track_param`;
- override AUDIO temporaire: chemin RT/audio dedie.

Pour un parametre d'entite, `param_store.active[]` est un miroir UI, pas la verite runtime. Une application batch pre-valide, coalesce les refresh et publie une projection unique.

`param_desc_t::value_policy` possede conversions canonique/affichee, pas normal/SHIFT et politique d'automation. Les p-locks continus utilisent toute la plage `uint16_t`; les discrets utilisent leur pas. La persistance stocke la valeur CONTROL typee, notamment FLOAT32, jamais une representation UI.

Un p-lock est un override temporaire d'un parametre p-lockable, restaure ensuite vers sa base. NOTE, VELOCITY, LENGTH et MICROTIMING sont des champs PLAY structurels et non des p-locks generiques.

## Modulation

LFO, Matrix, ENV3, filtre et VCA ont une autorite unique. Une destination Matrix est `{entity_id, param_id}`. En GROUP, le master possede Matrix, trois LFO, ENV3 et operateurs; un child peut etre destination mais ne devient pas owner. AUDIO compile les plans et masques de sources a la publication, sans relire la configuration CONTROL.

La valeur CONTROL du parametre est l'unique autorite de sa base. Elle est projetee par le chemin normal des commandes parametre et met a jour directement la destination AUDIO, y compris pendant une modulation. Le snapshot Matrix ne transporte que la topologie, les plages et la configuration des operateurs; il ne contient aucune base. Lorsqu'une nouvelle destination devient routee, sa valeur CONTROL courante est projetee avant rendu. Une recompilation conserve la base AUDIO deja projetee et ne peut donc pas restaurer une ancienne valeur.

La resolution commune est `clamp(base_courante + somme(source * profondeur_normalisee * plage), min, max)`. Retirer le dernier slot restaure `base_courante`.

Les LFO produisent des segments `{start, step, frames}`. Les formes continues sont interpolees; wraps et transitions discretes creent des frontieres. Un changement de mode poly invalide la source de la voix jusqu'au prochain trigger sans redemarrer la voix.

## Parametres AUDIO dates

Les detents encodeur valides capturent TIM5 et un binding pointer-free. CONTROL produit une commande finale `SET_TARGET`; AUDIO la planifie a l'`effective_sample_time`. Le ring est SPSC, borne et transactionnel pour les groupes. Saturation rejette le nouvel evenement ou le groupe complet; les valeurs latest-wins persistantes restent pending et sont retentees.

AUDIO applique la cible avant le sample concerne. Le smoothing appartient au backend reel: mixer, voix ou effet. Le dispatcher date ne fabrique aucun lissage generique. Notes, panic et parametres gardent des capacites distinctes.

Les slots Audio FX A/B possedent MODEL/P1/P2/P3. Seuls P1/P2/P3 sont p-lockables. En GROUP, les models appartiennent au master et les children n'exposent que LEVEL A/B.
