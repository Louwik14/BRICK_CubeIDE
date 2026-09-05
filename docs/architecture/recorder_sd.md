# Recorder SD, Looper et streamer BRICK

## Frontieres P0-4

Les decisions START/STOP Looper sont prises par CONTROL et publiees comme
RECORD au sample de boundary. Le handler AUDIO de boundary ne rappelle aucune
fonction CONTROL; il consulte uniquement le head capture AUDIO local pour
figer la longueur physique.
Le Recorder CONTROL enregistre directement la source page-cache generique a
l'entree `DRAINING`, depuis le track et la session qu'il possede. Il ne publie
aucun DTO live et aucun service Looper ne sonde son etat.

ARM prepare integralement la session Recorder avant toute echeance musicale:
chemins, nettoyage, reservation, writer et configuration AUDIO sont deja en
etat `PREPARED`. `seq_runtime` reste l'autorite du transport et injecte une
unique transition datee avant la publication du premier horizon. PLAY UI,
MIDI START/CONTINUE et le demarrage par note convergent sur cette transition;
`audio_recorder` effectue alors `PREPARED -> RECORDING` au meme sample que
l'activation du bus. Aucun acces FatFs/SD, polling UI ou deduction depuis le
playhead n'appartient au START.

## Autorites

`audio_recorder` est l'unique facade produit de capture SD. Ses deux clients exclusifs sont Audio Rec et Looper. Le shared Recorder contient seulement le PCM et `{head_cursor, tail_cursor, closed_session, capture_fault}`. AUDIO ecrit payload/head/fermeture/fault; STORAGE ecrit uniquement tail. Session, client, frame limit, preparation, activite, erreurs SD/FatFs, writer et finalisation restent locaux a leur proprietaire et transitent si necessaire dans RECORD. `generic_recorder` ne connait ni UI, ni Looper, ni WAV. Looper ne lit jamais la reservation mutable Storage.

La facade `audio_recorder.c` conserve ARM, client, session, commandes datees,
politique Looper et projection des statuts produit. `audio_recorder_storage.c`
possede l'etat writer, les buffers DMA, la reservation, le drain, le scheduler,
FatFs et la finalisation. Leur API est un point d'appel local CM4;
elle ne constitue pas l'ABI H747 et ne transporte aucun pointeur prive vers
AUDIO.

Audio Rec possede un unique bus stereo AUDIO, somme des entites resolues par CONTROL et, si necessaire, des sources physiques LINE et USB directes. CONTROL publie le masque d'entites, ARM et les sources effectives comme PARAM final dans la FIFO unique; AUDIO conserve ensuite cette configuration privee. LINE directe est exclue lorsque l'entree physique est deja representee par une track External routee vers REC; cette decision reste derivee de `track_input_ownership`, `entity_topology` et `track_runtime`. USB est admis par le même bus Recorder depuis `g_audio_physical_inputs.usb`, sans nouveau ring, mixer ou owner. MIC reste reserve au contrat Recorder / Audio REC et n'est jamais une source selectionnable d'une track External.

Les toggles Audio REC LINE/MIC/USB sont des etats runtime de `sample_capture`; ils ne sont pas persistes et sont reinitialises au boot comme le contrat existant LINE/MIC.

Le meme bus alimente la conversion PCM24 du Recorder et le peak brut par bloc. AUDIO publie seulement `{generation_io, peak_abs_pcm24}` avant capture; ce fait physique minimal est requis car le ring PCM ne recoit encore aucun sample avant THR. CONTROL compare le peak au seuil, latch ARM TRIG puis reutilise la quantification NOW/BAR/PATTERN de `sample_capture`. Le writer n'est active qu'apres trigger et echeance. Le vu-metre est diagnostique/UI et ne modifie ni le peak brut, ni la waveform de la prise, ni le WAV.

Une seule capture peut etre preparee ou active. La preparation Storage cree reservation et writer; CONTROL publie REC_BUS puis START/STOP dates. L'IRQ mixer appelle directement l'endpoint Recorder AUDIO, copie dans le ring puis publie le head avec une barriere. STOP, limite ou overflow ferment localement AUDIO et publient seulement `closed_session` et `capture_fault`; Storage draine puis finalise. Aucun ACK fonctionnel, config preparee, `active`, `error`, client ou generation partage ne subsiste.

## Reservation et ecriture

`recorder_file_reservation` encapsule les extensions FatFs custom pour FAT32 et exFAT. La reservation initiale puis les extensions progressives publient une carte physique append-only. Une extension ajoute des extents sans modifier ceux deja visibles au streamer. La fin de prise engage la longueur utile, libere la queue non utilisee puis permet le header et le renommage. La recovery boot supprime ou reduit une reservation interrompue sans exposer de clusters voisins.

La carte contient au plus 128 extents. Sa saturation arrete proprement la prise avec `MAP_FULL`; elle ne permet ni overwrite ni continuation non mappee. Les extensions de 2 MiB donnent une borne pratique voisine de 256 MiB dans le pire cas d'un extent par extension. `NO_SPACE`, retrait media, timeout, erreur DMA, overflow ring et echec filesystem convergent vers un etat FAILED recuperable; la prise n'est jamais annoncee finalisee avant engagement physique complet.

Le block device n'autorise qu'un WRITE DMA actif. `begin/poll/take_result` publient la completion seulement apres callback SD et fin reelle de la carte. Cache clean et alignement DMA restent dans cette couche. `sd_scheduler` est l'unique arbitre commun des READ streamer, WRITE recorder et transactions FILESYSTEM. Les deadlines de classe audio determinent la priorite; une reservation recorder critique empeche la famine d'ecriture, tandis que les operations filesystem opportunistes attendent une fenetre sure. Cette priorite de classe ne cree ni EDF par voix ni deadline individuelle dans Stream, qui conserve son round-robin strict. `sd_access_gate` reste un garde contre les appels FatFs directs pendant une fenetre streaming, pas une seconde autorite pour les commandes ordonnancees.

## Tails et reloop

Les compteurs ont des sens distincts: `head_cursor` publie par AUDIO, `tail_cursor` par Storage apres engagement physique, puis les tails packed/submitted/committed restent STORAGE-locaux. Le producteur borne son occupation par `head_cursor - tail_cursor`; Storage ne reutilise ni ne finalise au-dela du head publie. Le streamer Looper ne peut lire que jusqu'au tail `committed`.

La carte physique finale est importee une fois a l'entree `DRAINING` et la
registration conserve son epoch pendant toute la prise. Une progression
effective du tail `committed` met seulement `readable_frames` a jour; le
renommage `.REC -> .WAV` met seulement le chemin a jour. AUDIO ne lit ni
reservation, ni chemin, ni etat Storage: il publie les deux ranges physiques
de ses leases primaire et auxiliaire, wrap inclus, avec la profondeur de
prefetch Looper. Le manager Stream ne reconstruit ni playhead, ni wrap, ni
lookahead Looper.

Au stop Looper, le preroll RAM de 0,25 s permet le premier passage immediat. Le page-cache rejoint ensuite le tail SD engage et le preroll n'est pas rejoue apres le premier wrap. Le chemin du stream passe de `.REC` a `.WAV` apres finalisation sans recharger la prise. Le transport de pages et le recorder partagent le scheduler et peuvent coexister; l'absence de page produit le fallback audio existant, sans lecture synchrone depuis l'IRQ.

## Memoire et limites

La reserve fixe recorder comprend le ring Audio Rec/Looper de 12 001 frames (`~96 KiB`), deux buffers PCM24 de 32 KiB (`64 KiB`) et le preroll Looper de 12 000 frames (`~96 KiB`), hors petits contextes. La suppression des deux anciens rings de `multi_record_writer` recupere environ 188 KiB de SDRAM recorder. Les prises LEN respectent strictement `expected_frames`; les prises libres restent bornees par l'espace carte et la capacite de carte d'extents.

Les `f_write` restants hors recorder servent l'editeur REC EDIT (copie Save/Assign) et les transactions fichiers ordinaires; ils ne sont pas sur le hot path de capture. Toute evolution doit conserver: aucune attente SD en IRQ, publication du tail seulement sur completion physique, carte append-only, une commande block-device active, passage obligatoire par le scheduler pour READ/WRITE/FILESYSTEM concurrents, et finalisation WAV seulement apres drainage complet.

## Validation

Les tests hote conserves couvrent le state machine generique, le WAV et ses erreurs produit, l'ecriture block-device asynchrone, l'arbitrage scheduler et la reservation FAT32/exFAT avec extension, preservation des voisins, liberation de queue et recovery. La validation cible doit compiler BRICK puis exercer capture longue, LEN, stop pendant charge streamer, carte lente/fragmentee, retrait media et reloop immediat.

Le STOP Looper immediat est un lot fonctionnel unique de trois commandes au
meme sample: armement STOP Looper, STOP du client Recorder et boundary Looper.
Le head FIFO n'est publie qu'apres copie des trois commandes; en cas de manque
de place, aucune n'est visible et le lifecycle CONTROL reste inchange. Le STOP
attendant une boundary reste inclus dans le commit atomique de l'horizon qui
porte deja le STOP Recorder et la boundary.
