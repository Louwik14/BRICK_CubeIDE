# Z3 - Parametres, modulation et controle

Le contrat commun `param_spec[]` ne contient que ID, type, min, max et default.
Le registre CONTROL ajoute affichage, persistance, p-lockabilite, politique de
valeur et callbacks. AUDIO consomme `param_audio.h`, rejette une valeur hors
contrat et applique une valeur deja canonique sans rejouer la politique CONTROL.

Autorites d'ecriture:

- global: `param_registry_commit_global`, vers l'autorite CONTROL explicite;
- entite: `param_registry_apply_track_value`;
- configuration LFO: `mod_lfo_v1_set_track_param`;
- override AUDIO temporaire: chemin RT/audio dedie.

Le routeur Param ne stocke aucune valeur. Keyboard, configuration Seq, PLAY et
Transport/Metronome ne sont pas des Param: leurs UI et leur persistance parlent
directement a `keyboard_runtime`, `seq_model`/`seq_runtime`/`seq_edit` et
`metronome_control`. Les autres scalaires globaux Param utilisent le sparse
`param_global_control`.
La structure Track (famille, type, canal/source MIDI et entree), le nombre de
voix, la structure Matrix/Multi/Slew et le routing Audio FX ne sont pas des
Param non plus. Leurs owners sont respectivement Track, `polyphony_control`,
Mod et `audio_fx_control_state`; leurs UI appellent directement ces APIs.
`PARAM_CFG_POLY_SPREAD` reste un Param sonore scalaire.
Les valeurs d'entite sont lues et ecrites directement dans Track, Seq, Mod,
NoteFx ou l'etat Param CONTROL par piste. L'UI ne conserve que son contexte
d'interaction et lit l'autorite a la demande.

`param_desc_t::value_policy` possede conversions canonique/affichee, pas normal/SHIFT et politique d'automation. Les p-locks continus utilisent toute la plage `uint16_t`; les discrets utilisent leur pas. La persistance stocke la valeur CONTROL typee, notamment FLOAT32, jamais une representation UI.

Un p-lock AUDIO est resolu par CONTROL en valeur finale puis transporte comme PARAM date; la restauration de base suit le meme chemin. La FIFO unique est dimensionnee pour les 1024 ecritures d'une boundary maximale plus l'horizon NOTE et les commandes de controle. Les p-locks MIDI FX restent integralement CONTROL: leur override canonique est applique au runtime Note FX avant la NOTE de la meme boundary. AUDIO ne connait ni la provenance, ni la notion de p-lock. NOTE, VELOCITY, LENGTH et MICROTIMING sont des champs PLAY structurels et non des p-locks generiques.

## Modulation

LFO, Matrix, ENV3, filtre et VCA ont une autorite unique. Une destination Matrix est `{entity_id, param_id}`. En GROUP, le master possede Matrix, trois LFO, ENV3 et operateurs; un child peut etre destination mais ne devient pas owner. AUDIO compile les plans et masques de sources a la publication, sans relire la configuration CONTROL.

Les implementations sont separees par ownership: `mod_lfo_control` et
`mod_matrix_control` modifient uniquement l'etat canonique et publient des
PARAM; `mod_lfo_v1`, `mod_env3` et `mod_matrix` ne possedent que configuration
appliquee, etats DSP, caches et plans AUDIO. Le registre Param suit la meme
frontiere: les backends CONTROL couvrent l'etat canonique/MIDI, tandis que
`param_registry_audio`, `param_filter_audio` et les backends moteur appliquent
les commandes cote AUDIO. Aucun getter UI ne lit un runtime AUDIO.

Le catalogue CONTROL LFO/Matrix derive et valide ses destinations depuis les
regles Param et le descripteur canonique de piste. AUDIO ne consulte aucune
politique Track: il verifie l'ABI puis prepare l'opcode DSP de la destination
deja legitime. Les destinations MIDI CC n'existent plus cote AUDIO et MIDI OUT
reste exclusivement CONTROL.

La valeur CONTROL du parametre est l'unique autorite de sa base. Elle est projetee par le chemin normal des commandes parametre et met a jour directement la destination AUDIO, y compris pendant une modulation. Les routes Matrix sont adressees par `{track, slot, field}` et Multi/Slew par leurs APIs typees; source, destination, depth et enable ne traversent jamais Param. Des commandes fonctionnelles typees mettent a jour l'etat Matrix M7, marquent l'owner dirty et finalisent une seule recompilation apres toutes les commandes dues au meme sample. Min/max, endpoints, plans et caches restent derives localement; les MODEL et le nombre de slots Drum sont lus dans le runtime moteur. Aucun descripteur partage, pool, ACK ou canal fonctionnel parallele n'existe.

Les selections Sample et Wavetable appartiennent a `project_control` sous forme
de references asset typees stables; leur resolution en slot runtime n'a lieu
qu'a la publication AUDIO. Elles ne sont ni Param, ni destinations de p-lock,
modulation ou MIDI. Arm, longueur et lecture automatique du Looper appartiennent
a `audio_recorder`; l'UI appelle directement cet owner. La selection d'operateur
FM est un contexte local de l'editeur. L'etat interne FM est possede par
`fm_control_state` cote CONTROL et publie comme un DTO coherent unique vers
AUDIO; aucun pack FM interne ne traverse Param.

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

Les 26 positions TONE normalisees restent exclusivement CONTROL/Seq/Persistence.
Lors d'une publication, CONTROL resout `{type, slot, valeur}` en
`{param_id, valeur canonique}` et utilise le PARAM ordinaire. AUDIO ne possede
ni cache TONE, ni mapping slot, ni commande specialisee, ni reapply apres
PROGRAM. Une NOTE deja tenue garde son output, sa note,
sa velocite et son gate: le nouveau renderer utilise `initialize_held_note`,
sans allocation musicale, `note_on`, retrigger LFO, VCA ou filtre commun.

CONTROL utilise les memes 26 ordinaux normalises comme unique autorite TONE;
les identifiants PARAM moteur ne sont que le catalogue de label, plage et
conversion du moteur courant. Snapshot, Clipboard, Patch, Pattern et Project
serialisent ces 26 ordinaux, y compris les slots dormants. Les p-locks TONE
serialisent egalement l'ordinal normalise, sans dependance au moteur actif.
Les runtimes moteur et voix ne conservent que leurs projections natives ou
leurs etats DSP.

Les slots Audio FX A/B possedent MODEL/P1/P2/P3. MODEL reste un endpoint musical stable de slot; un changement conserve P1/P2/P3 et ne publie que MODEL. Filter position, ordre et modes spatiaux appartiennent a `audio_fx_control_state` et utilisent des commandes typees. Les restores preparent, publient, puis installent directement l'etat final, sans passer par les defaults du modele. Seuls P1/P2/P3 sont p-lockables. En GROUP, les models appartiennent au master et les children n'exposent que LEVEL A/B.
# FM parameter authority

Public `PARAM_FM_*` queries and commits address the semantic FM CONTROL state
directly. Scalar edits use the PARAM FIFO path. Whole-state replacement emits
one atomic FIFO batch at a common effective sample time; AUDIO remains a
projection and no FM shared snapshot, mailbox, sequence or generation exists.
Les endpoints operateur projettent directement les champs DX7 canoniques;
FREQ convertit vers/depuis mode/coarse/fine et KEY projette la moyenne des
profondeurs gauche/droite. Aucun tableau float operateur n'est stocke.
