# Recorder SD, Looper et streamer

## Frontieres P0-4

Le stop Looper cote AUDIO consulte uniquement la vue capture D3
(`accepted/released/error/active/stop_generation`), jamais l'etat du writer M4.
Le live stream publie aussi sequence, client, tails et chemin inline; sa carte
physique reste le snapshot append-only de reservation. Inversement, les
conflits Storage consultent un bit publie par le runtime Looper M7, jamais sa
structure de boundary interne. Le service Looper/page-cache reste background
M7 et ne s'execute plus dans le service Storage.

## Autorites

`audio_recorder` est l'unique facade produit de capture SD. Ses deux clients exclusifs sont Audio Rec et Looper. Il publie un endpoint SPSC generationnel dans D3 et une projection live complete, extents de reservation inclus, dans la zone partagee Recorder; il recoit le PCM `int32` stereo 48 kHz dans le ring Recorder non-cacheable, configure `generic_recorder` et publie etats, erreurs, metriques et prise finalisee. `generic_recorder` ne connait ni UI, ni Looper, ni WAV: Storage lui transmet le head accepte publie par AUDIO; il possede les deux buffers d'ecriture, les descripteurs asynchrones et le tail accepte/engage. Looper ne lit jamais la reservation mutable Storage.

Audio Rec possede un unique bus stereo AUDIO, somme des entites resolues par CONTROL et, si necessaire, de LINE directe. CONTROL publie vers AUDIO un snapshot D3 fixe et pointer-free contenant le masque d'entites, ARM et les sources effectives. LINE directe est exclue lorsque l'entree physique est deja representee par une track External routee vers REC; cette decision est derivee de `track_input_ownership`, `entity_topology` et `track_runtime`. MIC reste un etat logique silencieux: aucune source physique, configuration codec ou voie ADC ne lui est associee.

Le meme bus alimente la conversion PCM24 du Recorder et le peak brut par bloc. AUDIO publie seulement `{generation, peak_abs_pcm24}` dans un snapshot D3; CONTROL compare ce peak au seuil THR, latch ARM TRIG une fois puis reutilise la quantification NOW/BAR/PATTERN de `sample_capture`. Le writer n'est active qu'apres trigger et echeance. Le vu-metre applique son lissage uniquement dans l'UI et ne modifie ni le peak brut, ni la waveform de la prise, ni le WAV. La production du bus et du peak ne depend pas de la page visible.

Une seule capture peut etre preparee ou active. La preparation Storage cree la reservation et arme une generation; le start AUDIO ne fait que publier l'activation. L'IRQ copie dans le ring puis publie le head avec une barriere. Le stop publie un marqueur de la meme generation et ferme immediatement le producteur; Storage observe ce marqueur, draine jusqu'au head publie, puis seulement finalise. L'IRQ ne touche jamais FatFs, le scheduler, la SD ni l'etat mutable du writer. La superloop empaquette, reserve, soumet les ecritures et finalise. Audio Rec et Looper produisent directement un fichier temporaire `.REC`, renomme en `.WAV` apres drainage, liberation de la queue reservee et ecriture du header final de 512 octets. Il n'existe plus de reservoir RAW, d'export RAW vers WAV ni de second writer.

## Reservation et ecriture

`recorder_file_reservation` encapsule les extensions FatFs custom pour FAT32 et exFAT. La reservation initiale puis les extensions progressives publient une carte physique append-only. Une extension ajoute des extents sans modifier ceux deja visibles au streamer. La fin de prise engage la longueur utile, libere la queue non utilisee puis permet le header et le renommage. La recovery boot supprime ou reduit une reservation interrompue sans exposer de clusters voisins.

La carte contient au plus 128 extents. Sa saturation arrete proprement la prise avec `MAP_FULL`; elle ne permet ni overwrite ni continuation non mappee. Les extensions de 2 MiB donnent une borne pratique voisine de 256 MiB dans le pire cas d'un extent par extension. `NO_SPACE`, retrait media, timeout, erreur DMA, overflow ring et echec filesystem convergent vers un etat FAILED recuperable; la prise n'est jamais annoncee finalisee avant engagement physique complet.

Le block device n'autorise qu'un WRITE DMA actif. `begin/poll/take_result` publient la completion seulement apres callback SD et fin reelle de la carte. Cache clean et alignement DMA restent dans cette couche. `sd_scheduler` est l'unique arbitre commun des READ streamer, WRITE recorder et transactions FILESYSTEM. Les deadlines audio determinent la priorite; une reservation recorder critique empeche la famine d'ecriture, tandis que les operations filesystem opportunistes attendent une fenetre sure. `sd_access_gate` reste un garde contre les appels FatFs directs pendant une fenetre streaming, pas une seconde autorite pour les commandes ordonnancees.

## Tails et reloop

Les compteurs ont des sens distincts: `received` publie par AUDIO dans le ring, `released` republi par Storage apres engagement physique, `packed` converti, `submitted` transmis au block device, `committed` confirme physiquement. Le producteur borne son occupation par `received - released`; Storage ne reutilise ni ne finalise au-dela du head publie. Le streamer Looper ne peut lire que jusqu'au tail `committed`. Ses lectures de la prise active utilisent exclusivement la carte physique; aucun fallback FatFs n'est permis tant que le fichier est en cours de construction.

Le page-cache Looper remplace sa carte live uniquement lorsque la generation de reservation change et qu'aucune lecture Looper n'est en vol. Le remplacement construit d'abord une carte valide, la publie atomiquement, puis libere l'ancienne; le renommage seul ne change pas la generation.

Au stop Looper, le preroll RAM de 0,25 s permet le premier passage immediat. Le page-cache rejoint ensuite le tail SD engage et le preroll n'est pas rejoue apres le premier wrap. Le chemin du stream passe de `.REC` a `.WAV` apres finalisation sans recharger la prise. Le transport de pages et le recorder partagent le scheduler et peuvent coexister; l'absence de page produit le fallback audio existant, sans lecture synchrone depuis l'IRQ.

## Memoire et limites

La reserve fixe recorder comprend le ring Audio Rec/Looper de 12 001 frames (`~96 KiB`), deux buffers PCM24 de 32 KiB (`64 KiB`) et le preroll Looper de 12 000 frames (`~96 KiB`), hors petits contextes. La suppression des deux anciens rings de `multi_record_writer` recupere environ 188 KiB de SDRAM recorder. Les prises LEN respectent strictement `expected_frames`; les prises libres restent bornees par l'espace carte et la capacite de carte d'extents.

Les `f_write` restants hors recorder servent l'editeur REC EDIT (copie Save/Assign) et les transactions fichiers ordinaires; ils ne sont pas sur le hot path de capture. Toute evolution doit conserver: aucune attente SD en IRQ, publication du tail seulement sur completion physique, carte append-only, une commande block-device active, passage obligatoire par le scheduler pour READ/WRITE/FILESYSTEM concurrents, et finalisation WAV seulement apres drainage complet.

## Validation

Les tests hote conserves couvrent le state machine generique, le WAV et ses erreurs produit, l'ecriture block-device asynchrone, l'arbitrage scheduler et la reservation FAT32/exFAT avec extension, preservation des voisins, liberation de queue et recovery. La validation cible doit compiler LowCost et Premium puis exercer capture longue, LEN, stop pendant charge streamer, carte lente/fragmentee, retrait media et reloop immediat.
