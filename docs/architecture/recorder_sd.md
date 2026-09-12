# Recorder SD, Looper et streamer

## Frontieres P0-4

Les decisions START/STOP Looper sont prises par CONTROL et publiees comme
RECORD au sample de boundary. Le handler AUDIO de boundary ne rappelle aucune
fonction CONTROL; il consulte uniquement le head capture AUDIO local pour
figer la longueur physique.
Le Recorder CONTROL enregistre directement la source page-cache generique a
l'entree `DRAINING`, depuis le track et la session qu'il possede. Il ne publie
aucun DTO live et aucun service Looper ne sonde son etat.

La demande ARM conserve son intention lorsque l'admission Storage repond
`NOT_NOW`; la superloop reprend la preparation sans boucle d'attente. Aucun
trigger ni START n'est publie avant que chemins, nettoyage, reservation et
writer soient integralement en etat `PREPARED`. En mode REC, `seq_runtime`
reste l'autorite du transport et
injecte une unique transition datee avant la publication du premier horizon.
PLAY UI, MIDI START/CONTINUE et le demarrage par note convergent sur cette
transition; `audio_recorder` effectue alors `PREPARED -> RECORDING` au meme
sample que l'activation du bus. Aucun acces FatFs/SD, polling UI ou deduction
depuis le playhead n'appartient au START.

Apres SAVE, les caches waveform derives peuvent continuer en Background, mais
la session transitoire Recorder est liberee. RETURN/DISCARD annule les travaux
editor propres a la prise, ferme la reservation, supprime les fichiers de
travail et converge vers IDLE. ERROR et CANCEL utilisent le meme teardown
cooperatif; un resultat produit/UI terminal ne conserve aucun handle ni owner
Storage.

## Autorites

`audio_recorder` est l'unique facade produit de capture SD. Ses deux clients exclusifs sont Audio Rec et Looper. Le shared Recorder contient seulement le PCM et `{head_cursor, tail_cursor, closed_session, capture_fault}`. AUDIO ecrit payload/head/fermeture/fault; STORAGE ecrit uniquement tail. Session, client, frame limit, preparation, activite, erreurs SD/FatFs, writer et finalisation restent locaux a leur proprietaire et transitent si necessaire dans RECORD. `generic_recorder` ne connait ni UI, ni Looper, ni WAV. Looper ne lit jamais la reservation mutable Storage.

La facade `audio_recorder.c` conserve ARM, client, session, commandes datees,
politique Looper et projection des statuts produit. `audio_recorder_storage.c`
possede l'etat writer, les buffers DMA, la reservation, le drain, le scheduler,
FatFs et la finalisation. Leur API est un point d'appel local CM4;
elle ne constitue pas l'ABI H747 et ne transporte aucun pointeur prive vers
AUDIO.

Audio Rec possede un unique bus stereo AUDIO, somme des entites resolues par CONTROL et, si necessaire, de LINE directe. CONTROL publie le masque d'entites, ARM et les sources effectives comme PARAM final dans la FIFO unique; AUDIO conserve ensuite cette configuration privee. LINE directe est exclue lorsque l'entree physique est deja representee par une track External routee vers REC; cette decision est derivee de `track_input_ownership`, `entity_topology` et `track_runtime`. Sur Low-Cost, MIC Audio Rec selectionne la source physique mono `IN3_R` du TLV320AIC3204, routee avec un gain MICPGA fixe de +20 dB par le Right MICPGA et le Right ADC; l'ancien `IN1_R`, alors inutilise, est faiblement reference au common-mode. Le sample SAI droit alimente `mic.mono`, puis les deux canaux du bus REC existant. LINE_R et MIC partagent ce Right ADC et sont donc exclusifs. En mode MIC, LINE physique n'est pas publiee comme source External stereo; MIC n'est pas encore une source External et aucun second chemin Recorder n'est cree.

### Audit analogique TLV320AIC3204

L'etat MIC est obtenu apres reset et initialisation LINE, puis quatre ecritures de
transition. Les valeurs ci-dessous sont celles effectivement programmees ou,
pour les registres non reecrits, leur valeur de reset conservee. Le decodage est
celui du *TLV320AIC3204 Application Reference Guide* SLAA557.

| Page/reg. | MIC | Decodage utile et valeur attendue |
|---|---:|---|
| P1/R1 | `0x08` | D3=1 coupe le faible lien AVDD-DVDD; conforme avec AVDD alimente par le LDO. |
| P1/R2 | `0x01` | D3=0 active les blocs analogiques, D0=1 active le LDO AVDD; conforme. |
| P1/R10 | `0x00` | D6=0 fixe le common-mode global a 0,9 V; les autres bits concernent les sorties; conforme. |
| P1/R51 | `0x68` | D6=1 MICBIAS actif, D5:D4=`10` donne 2,5 V avec CM=0,9 V, D3=1 choisit LDOIN; conforme aux 2,45 V mesures. |
| P1/R55 | `0x04` | D3:D2=`01` route exclusivement IN3_R vers Right MICPGA+ par 10 kohm; conforme. A 1 uF, ce choix place le pole d'entree vers 16 Hz pour une source d'impedance faible. |
| P1/R57 | `0x40` | D7:D6=`01` route CM1R vers Right MICPGA- par 10 kohm; tous les autres chemins sont coupes. C'est la reference single-ended correcte et elle est symetrique avec les 10 kohm de R55. |
| P1/R58 | `0x7B` ecrit, `0x78` utile | D7=0 laisse IN1_L disponible, D6=1 reference IN1_R inutilise, D5:D3=111 referencent IN2_L, IN2_R et IN3_L inutilises, D2=0 ne charge pas IN3_R. D1:D0 sont reserves, lus a zero et exclus du masque de verification: les `11` ecrits sont non canoniques mais sans effet sur le silicium ni sur le niveau MIC. |
| P1/R60 | `0x28` | D7=0 active le gain programme; D6:D0=40, soit 40 x 0,5 dB = +20 dB avec l'impedance 10 kohm de R55. Conforme, non mute. |
| P1/R61 | `0x00` | ADC PowerTune PTM_R4; conforme. |
| P1/R71 | `0x32` | charge rapide des entrees analogiques en 6,4 ms; conforme. |
| P1/R123 | `0x05` | force la reference analogique avec montee en 40 ms; conforme. |
| P0/R18, R19, R20 | `0x81`, `0x82`, `0x80` | NADC=/1, MADC=/2, AOSR=128: 48 kHz depuis 12,288 MHz; conforme. |
| P0/R61 | `0x01` (reset) | PRB_R1, traitement ADC par defaut a gain unitaire; aucun coefficient custom ni attenuation. |
| P0/R81 | `0xC0` | D7:D6=11 alimente les deux ADC, D3:D2=00 conserve les entrees analogiques, D1:D0=00 soft-step normal; Right ADC actif. |
| P0/R82 | `0x00` | D7=0 et D3=0 demutent les ADC; gains fins gauche et droit a 0 dB. |
| P0/R83, R84 | `0x00`, `0x00` (reset) | volumes numeriques ADC gauche et droit a 0 dB; aucune attenuation. |
| P0/R86, R94 | `0x00`, `0x00` | AGC gauche et droit desactives par D7=0; les autres registres AGC restent sans effet. |

Le choix 10 kohm de P1/R55 n'est pas une attenuation cachee: la table de gain
du codec definit le gain MICPGA nominal pour 10 kohm. A code PGA identique,
20 kohm retrancherait 6 dB et 40 kohm 12 dB. Le guide recommande de choisir
l'impedance selon le compromis charge/bruit; il signale explicitement que la
haute impedance augmente le bruit ou reduit la dynamique dans un chemin micro a
fort gain. Les 10 kohm sont donc coherents ici, avec une charge restant nettement
superieure aux 2,2 kohm de polarisation de la capsule.

LINE et MIC ne different que sur P1/R51 (`0x00`/`0x68`), P1/R55
(`0x80`, IN1_R par 20 kohm / `0x04`, IN3_R par 10 kohm), les bits utiles de
P1/R58 (`0x3C`/`0x78`) et P1/R60 (`0x00`/`0x28`). P1/R57 reste volontairement
`0x40`: CM1R par 10 kohm est la reference negative correcte dans les deux modes.
Les ADC, leur demute, leur volume, PRB_R1, AGC, reference et common-mode sont
communs et immuables apres l'initialisation. Les transitions LINE->MIC,
MIC->LINE et LINE->MIC reconstruisent ainsi le meme etat final; aucun reglage
LINE incompatible ne subsiste sur le Right ADC.

L'audit ne prouve donc aucune cause codec du faible niveau. Le suspect restant
est le trajet AC capsule+ -> C13 -> IN3_R. Sans oscilloscope, verifier sous
tension environ 1,65 V cote capsule de C13 et environ 0,9 V cote IN3_R, puis
comparer au multimetre en mode AC millivolts la variation de parole sur les deux
cotes. Hors tension, verifier separement la continuite capsule+-pad C13 et
pad C13-IN3_R; une mesure de capacite ou un remplacement controle de C13 permet
de confirmer le composant, la continuite DC ne pouvant pas traverser le
condensateur.

Le meme bus alimente la conversion PCM24 du Recorder et le peak brut par bloc.
ARM TRIG publie vers AUDIO le seuil et un epoch d'armement. AUDIO observe le
signal et possede la detection physique du franchissement sous-vers-au-dessus;
il publie un evenement unique pour cet epoch. Cette surveillance est active
independamment de l'etat du transport. CONTROL consomme l'evenement et demarre
la capture immediatement lorsque le transport est arrete; lorsque celui-ci est
deja actif, NOW demarre immediatement et BAR/PATTERN conserve leur quantification.
TRANSPORT START n'est ni requis ni interprete comme le trigger et ne rearme pas
un evenement consomme. Le writer n'est active qu'apres ce trigger et son
echeance. Le vu-metre est diagnostique/UI et ne modifie ni la detection, ni la
waveform de la prise, ni le WAV.

Une seule capture peut etre preparee ou active. La preparation Storage cree reservation et writer; CONTROL publie REC_BUS puis START/STOP dates. L'IRQ mixer appelle directement l'endpoint Recorder AUDIO, copie dans le ring puis publie le head avec une barriere. STOP, limite ou overflow ferment localement AUDIO et publient seulement `closed_session` et `capture_fault`; Storage draine puis finalise. Aucun ACK fonctionnel, config preparee, `active`, `error`, client ou generation partage ne subsiste.

## Reservation et ecriture

`recorder_file_reservation` encapsule les extensions FatFs custom pour FAT32 et exFAT. La reservation initiale et chaque extension de 2 MiB sont des jobs persistants; la carte physique append-only n'est publiee qu'apres acquisition et synchronisation de l'allocation correspondante. Un STOP annule seulement les allocations pas encore commencees. Une extension ajoute des extents sans modifier ceux deja visibles au streamer. La fin de prise conserve l'ordre `COMMIT -> RELEASE -> HEADER -> SYNC -> CLOSE -> RENAME`; le header de 512 octets utilise directement la carte publiee et le block device asynchrone, avec un buffer persistant aligne.

Le socle de continuation metadata est interne a FatFs et distinct de `FRESULT`. La progression de fenetre expose `YIELD`, `NEED_IO`, `WAIT_IO` et un terminal, tandis que les API FatFs publiques conservent leur contrat synchrone par un executeur qui consomme ce meme moteur. Le flush primaire, chaque copie FAT et la lecture de la nouvelle fenetre sont des commandes separees; une fenetre dirty n'est declaree propre qu'apres confirmation de toutes ses copies. Les scans FAT32 et bitmap exFAT de reservation ont un quantum borne; allocation, liaison et materialisation des fragments exFAT reprennent sur leur checkpoint exact. Le sync objet Recorder parcourt de meme le repertoire parent exFAT cluster par cluster, charge et reecrit l'entry set entree par entree, puis engage la fenetre, FSINFO et la barriere. COMMIT, RELEASE et le SYNC apres header empruntent des jobs persistants. RELEASE confirme d'abord la longueur raccourcie, coupe la chaine FAT si necessaire, puis libere un cluster FAT32 ou un bit bitmap exFAT confirme par step avant le sync et la publication finale. Les extents de l'extension restent prives jusqu'a son terme puis sont publies sans modifier ceux deja visibles. Apres ce SYNC confirme, CLOSE ne fait qu'une transition RAM bornee. Le rename Recorder ferme, limite au meme repertoire et a une geometrie d'entrees inchangee, scanne les collisions puis reecrit les LFN FAT32 ou l'entry set exFAT entree par entree; chaque changement de fenetre rend l'arbitrage physique au scheduler.

L'ownership FatFs logique et l'ownership SD physique sont distincts. Un job Recorder suspendu interdit un second appel FatFs sur le volume, mais relache le gate physique apres chaque step coherent. Le scheduler peut donc attribuer un READ Stream/Looper ou un WRITE PCM Recorder avant le step metadata suivant. Une completion metadata ne chaine jamais automatiquement le step suivant: elle rend d'abord l'arbitrage au scheduler.

La carte contient au plus 128 extents. Sa saturation arrete proprement la prise avec `MAP_FULL`; elle ne permet ni overwrite ni continuation non mappee. Les extensions de 2 MiB donnent une borne pratique voisine de 256 MiB dans le pire cas d'un extent par extension. `NO_SPACE`, retrait media, timeout, erreur DMA, overflow ring et echec filesystem convergent vers un etat FAILED recuperable; la prise n'est jamais annoncee finalisee avant engagement physique complet.

Le block device n'autorise qu'un WRITE DMA actif. `begin/poll/take_result` publient la completion seulement apres callback SD et fin reelle de la carte. Un abort conserve le buffer jusqu'a sa completion physique; si la primitive HAL echoue ou ne complete pas avant `BRICK6_SD_TIMEOUT_MS`, le block device desinitialise SDMMC, publie un unique terminal `ABORT_FAILED` et conserve le peripherique indisponible jusqu'a sa reinitialisation. Cache clean et alignement DMA restent dans cette couche. `sd_scheduler` est l'unique arbitre commun des READ streamer, WRITE recorder et transactions FILESYSTEM. Les deadlines audio determinent la priorite; une reservation recorder critique empeche la famine d'ecriture, tandis que les operations filesystem opportunistes attendent une fenetre sure. `sd_access_gate` reste un garde contre les appels FatFs directs pendant une fenetre streaming, pas une seconde autorite pour les commandes ordonnancees.

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

Les tests hote conserves couvrent le state machine generique, le WAV et ses erreurs produit, l'ecriture block-device asynchrone, l'arbitrage scheduler et la reservation FAT32/exFAT avec extension, preservation des voisins, liberation de queue et recovery. La validation cible doit compiler LowCost puis exercer capture longue, LEN, stop pendant charge streamer, carte lente/fragmentee, retrait media et reloop immediat.

Le STOP Looper immediat est un lot fonctionnel unique de trois commandes au
meme sample: armement STOP Looper, STOP du client Recorder et boundary Looper.
Le head FIFO n'est publie qu'apres copie des trois commandes; en cas de manque
de place, aucune n'est visible et le lifecycle CONTROL reste inchange. Le STOP
attendant une boundary reste inclus dans le commit atomique de l'horizon qui
porte deja le STOP Recorder et la boundary.
