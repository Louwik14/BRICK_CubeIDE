# Z3 - Parametres, modulation et controle

Le registre decrit chaque parametre par ID, domaine, plage, affichage, persistance, p-lockabilite, politique de valeur et backend. Les parametres d'entite sont appliques avec leur entity; les parametres Master sont globaux.

Autorites d'ecriture:

- global: `param_set`;
- entite: `param_registry_apply_track_value`;
- configuration LFO: `mod_lfo_v1_set_track_param`;
- override AUDIO temporaire: chemin RT/audio dedie.

Pour un parametre d'entite, `param_store.active[]` est un miroir UI, pas la verite runtime. Une application batch pre-valide et publie atomiquement ses PARAM finaux.

`param_desc_t::value_policy` possede conversions canonique/affichee, pas normal/SHIFT et politique d'automation. Les p-locks continus utilisent toute la plage `uint16_t`; les discrets utilisent leur pas. La persistance stocke la valeur CONTROL typee, notamment FLOAT32, jamais une representation UI.

Un p-lock AUDIO est resolu par CONTROL en valeur finale puis transporte comme PARAM date; la restauration de base suit le meme chemin. La FIFO unique est dimensionnee pour les 1024 ecritures d'une boundary maximale plus l'horizon NOTE et les commandes de controle. Les p-locks MIDI FX restent integralement CONTROL: leur override canonique est applique au runtime Note FX avant la NOTE de la meme boundary. AUDIO ne connait ni la provenance, ni la notion de p-lock. NOTE, VELOCITY, LENGTH et MICROTIMING sont des champs PLAY structurels et non des p-locks generiques.

## Modulation

LFO, Matrix, ENV3, filtre et VCA ont une autorite unique. Une destination Matrix est `{entity_id, param_id}`. En GROUP, le master possede Matrix, trois LFO, ENV3 et operateurs; un child peut etre destination mais ne devient pas owner. AUDIO compile les plans et masques de sources a la publication, sans relire la configuration CONTROL.

Le catalogue commun LFO/Matrix filtre les parametres selon le modele courant. Les labels CONTROL et disponibilites dynamiques de Prism, Stack, Drum MD et Audio FX viennent de leurs catalogues; cote AUDIO, Matrix lit directement les MODEL des runtimes Audio FX, Prism, Stack et Drum MD qui les executent. Il ne conserve aucun miroir MODEL. Un changement de PROGRAM ou MODEL marque les Matrix dependantes dirty et recompile localement leur interpretation sans retransmettre leurs valeurs persistantes.

La valeur CONTROL du parametre est l'unique autorite de sa base. Elle est projetee par le chemin normal des commandes parametre et met a jour directement la destination AUDIO, y compris pendant une modulation. Chaque champ Matrix traverse egalement PARAM: les kinds indexes 2..9 adressent les huit slots, tandis que Multi et Slew utilisent la portee track normale. M7 conserve le petit etat canonique propre a Matrix, marque l'owner dirty et finalise une seule recompilation apres toutes les commandes dues au meme sample. Min/max, endpoints, plans et caches restent derives localement; les MODEL et le nombre de slots Drum sont lus dans le runtime moteur. Aucun descripteur partage, pool, ACK ou canal fonctionnel parallele n'existe.

Le selector Sampler est resolu par CONTROL en slot runtime Multi/Stream/RAM avant publication, et les commandes Looper transportent directement leurs valeurs finales. Le backend AUDIO n'interroge ni Project ni stockage; il applique le PARAM au programme courant et reconstruit Matrix depuis ses catalogues et son etat AUDIO local.

La resolution commune est `clamp(base_courante + somme(source * profondeur_normalisee * plage), min, max)`. Retirer le dernier slot restaure `base_courante`.

Le restore compare l'autorite CONTROL et ne transporte que les champs Matrix et
operateurs effectivement modifies. Les changements reels d'un owner peuvent
rester atomiques dans un lot; un restore deja identique ne publie rien.
La suppression du pool de 2048 snapshots (156 octets), de ses 2048 IDs, des
32 projections, des compteurs d'allocation et des generations CONTROL retire
332870 octets. La disparition des generations dans la configuration runtime
M7 retire encore 128 octets; l'etat canonique ajoute 1280 octets et les
masques/depth 5 octets. Le gain net des symboles Matrix est donc de 331713
octets avant alignement linker (343111 avant, 11398 apres, runtime derive
compris).

Les LFO produisent des segments `{start, step, frames}`. Les formes continues sont interpolees; wraps et transitions discretes creent des frontieres. Un changement de mode poly invalide la source de la voix jusqu'au prochain trigger sans redemarrer la voix.

## Parametres AUDIO dates

Les detents encodeur valides capturent TIM5 et leur cible CONTROL. CONTROL produit un PARAM final; AUDIO l'applique a l'`effective_sample_time`. Les groupes sont publies atomiquement dans la FIFO SPSC unique. Latest-wins ne concerne que l'etat de base persistant; le chemin p-lock date n'appelle jamais directement le runtime AUDIO.

AUDIO applique la cible avant le sample concerne. Le smoothing appartient au backend reel: mixer, voix ou effet. Le dispatcher date ne fabrique aucun lissage generique.

AUDIO conserve par piste les 26 positions TONE canoniques normalisees,
alimentees directement par PARAM. Elles ne sont ni une image PARAM, ni un
profil moteur: chaque renderer lit les slots qu'il connait dans sa propre
plage et ignore les autres. PROGRAM ne charge aucun default et CONTROL ne
republie aucun PARAM. Une NOTE deja tenue garde son output, sa note,
sa velocite et son gate: le nouveau renderer utilise `initialize_held_note`,
sans allocation musicale, `note_on`, retrigger LFO, VCA ou filtre commun.

CONTROL utilise les memes 26 ordinaux normalises comme unique autorite TONE;
les identifiants PARAM moteur ne sont que le catalogue de label, plage et
conversion du moteur courant. Snapshot, Clipboard, Patch, Pattern et Project
serialisent ces 26 ordinaux, y compris les slots dormants. Les p-locks TONE
serialisent egalement l'ordinal normalise, sans dependance au moteur actif.
Les runtimes moteur et voix ne conservent que leurs projections natives ou
leurs etats DSP.

Les slots Audio FX A/B possedent MODEL/P1/P2/P3. Un changement de MODEL conserve P1/P2/P3 et ne publie que MODEL; AUDIO recalcule seulement son etat derive local. Les restores installent puis publient directement l'etat final, sans passer par les defaults du modele. Seuls P1/P2/P3 sont p-lockables. En GROUP, les models appartiennent au master et les children n'exposent que LEVEL A/B.
