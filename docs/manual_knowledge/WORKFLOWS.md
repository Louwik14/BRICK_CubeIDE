# Workflows techniques

## WF-PROJ-NEW — Nouveau projet

Statut : CONFIRME. Etat initial : Settings disponible, aucune operation Storage exclusive. SETTINGS -> PROJECT -> `[ + NEW PROJECT ]` -> LOAD. `project_product_new_blank()` construit un candidat Blank et emprunte le pipeline Project Load. Apres `T_FORWARD`, l'ancien runtime est retire sans rollback ; au succes, nouveau Project/Pattern, assets et UI sont publies ensemble. Un refus media/workspace avant la frontiere conserve l'etat courant. Retour : SETTINGS remonte ; une operation engagee n'est pas annulable par navigation.

## WF-PROJ-LOAD — Charger un projet

Statut : CONFIRME, test SD requis. SETTINGS -> PROJECT -> selectionner slot -> PAGE 2 LOAD. Le candidat est scanne/decode/valide, le Pattern actif prepare, l'ingress ferme, PANIC/quiesce effectue, assets recharges puis CONTROL/SEQ/AUDIO commites. Progression 0..1000. Avant `T_FORWARD`, erreur = projet courant intact ; apres perte media post-forward = `FAILED_FORWARD_MEDIA`, transport arrete et ingress ferme jusqu'a un nouveau Load/Blank valide.

## WF-PROJ-SAVE — Save / Save As

Statut : CONFIRME. Transport obligatoirement arrete. Dans PROJECT, PAGE 3 SAVE sur un slot existant ecrase ce projet ; sur `[+ NEW]`, l'editeur de nom ouvre Save As. Confirmer le nom engage l'ecriture asynchrone et bloque les inputs Settings jusqu'au resultat. Save As publie un dossier autonome ; Save Project ecrit `PROJECT.B6C` et les Patterns dirty dans une transaction globale. Annulation possible uniquement dans Name Edit avant soumission.

## WF-PATTERN — Store et recall

Statut : CONFIRME. Entrer en mode PATTERN, choisir banque puis pattern sur Hall ; mode RECALL ou STORE selon le raccourci. Recall resout Working -> Saved Project -> default. A l'arret, commit immediat ; en lecture, candidat prepare puis commit a la boundary globale calculee sur la lane au cycle le plus long. STOP annule un candidat en attente. Le Pattern quitte est capture avant mutation et reconcilie cooperativement dans Working.

## WF-PATCH — Charger/sauver un patch

Statut : CONFIRME. Entrer PATCH -> filtrer catalogue -> choisir `[+ NEW PATCH]` ou fichier. PAGE 2 LOAD initialise le produit sur le slot virtuel ou charge le patch ; PAGE 3 SAVE ouvre Name Edit pour un nouveau patch ou ecrase la cible ; PAGE 4 maintenu PREVIEW, relachement stoppe. SHIFT+PAGE2 Rename, SHIFT+PAGE3 Delete. Un delete peut administrer un document invalide.

## WF-TRACK-ENGINE — Choisir piste et moteur

Statut : CONFIRME. Maintenir TRACK, toucher Hall/STEP cible, relacher TRACK. Ouvrir CFG MAIN ; les encodeurs de type/famille appellent l'owner Track, valident disponibilite/topologie/ressources, reconstruisent PROGRAM puis resynchronisent l'UI. GROUP n'est valide que sur top-level 7 ; ses children sont RAM mono. External refuse une entree deja possedee et affiche `USED Pn`.

## WF-ASSET-STREAM — Importer et assigner un sample

Statut : PARTIEL, essai SD requis. SETTINGS -> SAMPLE -> STREAM, choisir un slot, LOAD/REPLACE FROM SD, parcourir `0:/Samples`, valider. Un WAV incompatible propose conversion ; transport/Recorder actifs peuvent produire STOP TO LOAD/STOP REC TO LOAD. Apres completion, selectionner une piste STREAM -> TONE/PLAY -> selecteur asset et choisir l'identite chargee. La selection persistante est `{kind,path}`, le slot runtime est derive.

## WF-ASSET-MULTI — Preparer un Multi

Statut : CONFIRME cote code. SETTINGS -> SAMPLE -> MULTI, parcourir `0:/Multi`, choisir dossier/instrument, confirmer PREPARE/REPLACE. Pipeline : scan -> conversion -> index -> commit -> prepare -> refresh. Assigner ensuite depuis TONE/INST d'une piste MULTI. Unload retire l'admission des notes puis reprojette les pistes portant la reference lors d'un futur READY.

## WF-SEQ-EDIT — Creer une sequence et un p-lock

Statut : CONFIRME. Choisir lane sequencable, mode SEQ. STEP court bascule le trig ; STEP maintenu + encodeur d'une page Param promeut le geste held puis cree/met a jour le lock pour chaque step tenu. SHIFT+encodeur en ASSIGN Scene efface une cible Scene, pas un p-lock ; un STEP tenu a priorite. PLAY edite Note/Velocity/Length/Microtiming par voix et step. COPY/PASTE opere sur steps tenus, sinon sequence entiere ; SHIFT+COPY efface. Les limites sont 64 steps, 32 locks par step et 512 entrees par lane.

## WF-MOD — Affecter une modulation

Statut : CONFIRME. MOD -> MATRIX, choisir slot/source/destination/depth ; puis LFO1..3 pour Rate/Shape/Trig/Phase. Le premier bind LFO publie toute sa configuration avant la destination. En GROUP, l'owner est le master ; un child montre destinations communes + locales. Depth 0/disable/source none retire la route du plan sans perdre sa destination.

## WF-SCENE — Assigner et jouer une Scene

Statut : CONFIRME. SHIFT + touche Hall C2 (ID 7) entre en MACRO. Double tap ouvre ASSIGN ; maintenir une des 14 touches Hall blanches et tourner un encodeur sur n'importe quelle page admissible pour enregistrer une valeur absolue ; SHIFT+encodeur efface la destination ; relacher ferme la capture. Tap revient en LIVE. PRESSURE interpole base->cible selon pression ; TOGGLE verrouille/deverrouille. La navigation ne quitte pas l'overlay.

## WF-AUDIO-REC — Enregistrer, editer, sauver, assigner

Statut : CONFIRME, niveaux et timing a tester. Ouvrir Audio REC, choisir ARM OFF/REC/TRIG, trigger NOW/THRESHOLD/PATTERN/THRESHOLD+PLAY, longueur FREE ou 1..64 bars, quant NOW/BAR/PATTERN, seuil et sources LINE/MIC/USB/routes track. Demarrer/declencher ; stop conduit a TAKE_READY. PAGE 4 ouvre REC EDIT : encodeurs trim/loop/zoom/scroll, SHIFT modifie la fonction alternative, ZCROSS optionnel. Auditionner, sauver WAV, choisir piste d'assignation ; l'assign peut attendre l'arret transport.

## WF-MIDI-SYNC — Synchroniser

Statut : PARTIEL. CFG MIDI choisit canal 1..16 et source INTERNAL/EXTERNAL/ALL. La clock possede INTERNAL, EXTERNAL_MIDI et EXTERNAL_USB ; Master emet 24 PPQN vers UART, USB ou BOTH, Slave consomme Start/Continue/Stop/Clock horodates. La selection UI exacte de toutes les options clock n'a pas ete localisee dans une page Settings actuelle : validation/atteignabilite materielle requise.

Sources communes : handlers UI cites dans les fiches de domaine, `Src/Storage/project_product.c`, `pattern_*`, `patch_product.c`, `sample_capture.c`, `Src/Seq/seq_edit.c`, `Src/Mod/mod_matrix_control.c`, `Src/MIDI/midi.c`.
