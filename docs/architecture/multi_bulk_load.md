# Chargement massif des instruments Multi

Le chargement initial d'un instrument Multi n'emprunte plus le service du
streamer temps reel. Le gate PLAY/STOP lit uniquement l'etat runtime du transport
et son eventuel start pending. Les locks de fenetre et les pending restent des
conditions SD distinctes et ne sont jamais interpretes comme un transport actif.

L'UI verifie ce gate avant d'arreter une voix, vider un slot ou passer en etat de
preparation. Une tentative pendant PLAY retourne donc `STOP AUDIO` sans modifier
le transport ni les voix. Apres un arret manuel, la meme demande entre dans le
chemin de chargement; les travaux SD residuels sont differes par le gate SD
existant sans inverser le verdict PLAY/STOP.

Le loader calcule une fois, pour chaque sample, l'union des presocles start et
loop. Les intervalles superposes ou adjacents sont fusionnes. Toutes les pages
finales sont reservees et epinglees dans le pool permanent, puis placees dans
l'etat `LOADING`; elles ne sont donc jamais visibles dans les pending du
streamer temps reel.

## Execution

Le client SD `MULTI_BULK` rend la SD exclusive pendant toute l'operation. Les
autres clients sont differes entre les passages comme pendant une lecture. Un
passage traite au plus une lecture logique de 64 Kio afin de rendre la main a
l'UI. Le budget runtime fourni a `multi_sample_service_load()` n'est pas
applique au bulk-load.

Un seul sample est ouvert a la fois. Son fichier reste ouvert jusqu'a la fin de
ses intervalles. FatFs realise un seek par intervalle non contigu et conserve la
securite des fichiers fragmentes, sans construire une link-map de tout le WAV.
Chaque gros bloc est decode directement vers les pages FLOAT32 SDRAM finales.
Une page devient `READY` seulement apres son decodage complet.

## Progression, annulation et diagnostic

La progression repose sur `pages_remaining` et `samples_remaining`, sans scan
repetitif des pages, du pool ou des pending. Le bouton d'annulation ferme le
fichier courant, libere l'exclusivite SD et supprime l'instrument partiellement
charge. Le meme rollback est applique en cas d'erreur ou de redemarrage du
transport.

`multi_sample_load_diag_t` expose la duree, les ouvertures, seeks, lectures
logiques, transactions physiques SD, volumes logique et physique, taille
maximale d'une transaction, cycles de decodage et estimation des rescans
supprimes.
