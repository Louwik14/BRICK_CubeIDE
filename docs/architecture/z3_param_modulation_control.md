# Z3 - Parametres, modulation et controle

Le registre decrit chaque parametre par ID, domaine, plage, affichage, persistance, p-lockabilite, politique de valeur et backend. Les parametres d'entite sont appliques avec leur entity; les parametres Master sont globaux.

Autorites d'ecriture:

- global: `param_set`;
- entite: `param_registry_apply_track_value`;
- configuration LFO: `mod_lfo_v1_set_track_param`;
- override AUDIO temporaire: chemin RT/audio dedie.

Pour un parametre d'entite, `param_store.active[]` est un miroir UI, pas la verite runtime. Une application batch pre-valide, coalesce les refresh et publie une projection unique.

`param_desc_t::value_policy` possede conversions canonique/affichee, pas normal/SHIFT et politique d'automation. Les p-locks continus utilisent toute la plage `uint16_t`; les discrets utilisent leur pas. La persistance stocke la valeur CONTROL typee, notamment FLOAT32, jamais une representation UI.

Un p-lock AUDIO est resolu par CONTROL en override temporaire puis transporte comme `PARAM_SET` date vers AUDIO; la restauration de base suit le meme chemin. Le transport compact pointer-free contient seulement `{sample_low16, parameter_id, value16, track, operation}`. Sa capacite 1024 couvre exactement les 16 lanes qui peuvent chacune restaurer 32 destinations disjointes et en appliquer 32 nouvelles au meme boundary. Les p-locks MIDI FX restent integralement CONTROL: leur override canonique est applique directement au runtime Note FX avant la note de la meme boundary, sans passer par la queue live/structurelle de 31 commandes. AUDIO ne connait pas la notion de p-lock. NOTE, VELOCITY, LENGTH et MICROTIMING sont des champs PLAY structurels et non des p-locks generiques.

## Modulation

LFO, Matrix, ENV3, filtre et VCA ont une autorite unique. Une destination Matrix est `{entity_id, param_id}`. En GROUP, le master possede Matrix, trois LFO, ENV3 et operateurs; un child peut etre destination mais ne devient pas owner. AUDIO compile les plans et masques de sources a la publication, sans relire la configuration CONTROL.

Le catalogue commun LFO/Matrix filtre les parametres selon le modele CONTROL courant. Les labels et disponibilites dynamiques de Prism, Stack, Drum MD et Audio FX viennent de leurs catalogues de parametres; un changement de MODEL invalide le catalogue et republie le snapshot sans changer l'identite `{entity_id, param_id}` conservee par les routes.

La valeur CONTROL du parametre est l'unique autorite de sa base. Elle est projetee par le chemin normal des commandes parametre et met a jour directement la destination AUDIO, y compris pendant une modulation. Le snapshot Matrix ne transporte que la topologie, les plages et la configuration des operateurs; il ne contient aucune base. Lorsqu'une nouvelle destination devient routee, sa valeur CONTROL courante est projetee avant rendu. Une recompilation conserve la base AUDIO deja projetee et ne peut donc pas restaurer une ancienne valeur.

Le terminal AUDIO est projection-driven: le selector Sampler est resolu par CONTROL en slot runtime Multi/Stream/RAM avant publication, et les commandes Looper transportent directement mode, pitch et grain. Le backend AUDIO n'interroge ni Project, ni catalogues, ni `track_tone_sound_state`; le contexte de binding externe est consomme par valeur depuis le snapshot coherent.

La resolution commune est `clamp(base_courante + somme(source * profondeur_normalisee * plage), min, max)`. Retirer le dernier slot restaure `base_courante`.

Les LFO produisent des segments `{start, step, frames}`. Les formes continues sont interpolees; wraps et transitions discretes creent des frontieres. Un changement de mode poly invalide la source de la voix jusqu'au prochain trigger sans redemarrer la voix.

## Parametres AUDIO dates

Les detents encodeur valides capturent TIM5 et un binding pointer-free. CONTROL produit une commande finale `SET_TARGET`; AUDIO l'applique a l'`effective_sample_time`. Le ring est SPSC, borne et transactionnel pour les groupes. Latest-wins ne concerne que l'etat de base persistant; le chemin p-lock date n'appelle jamais directement le runtime AUDIO.

AUDIO applique la cible avant le sample concerne. Le smoothing appartient au backend reel: mixer, voix ou effet. Le dispatcher date ne fabrique aucun lissage generique. Notes, panic et parametres gardent des capacites distinctes.

Les slots Audio FX A/B possedent MODEL/P1/P2/P3. Seuls P1/P2/P3 sont p-lockables. En GROUP, les models appartiennent au master et les children n'exposent que LEVEL A/B.
