# Recorder SD, REC_SOURCE et lecture Streamer

Ce document décrit l'architecture livrée. Il n'existe plus de moteur Looper
séparé : le Recorder fabrique le contenu, `REC_SOURCE` publie une génération
immuable et le Streamer assure la lecture.

## Responsabilités

- CONTROL porte Audio REC : armement, trigger, longueur, quantize, routing,
  STOP, SAVE et état UI.
- AUDIO construit le bus REC stéréo float, applique l'OVERDUB et convertit le
  résultat en PCM24 stocké dans des mots `int32_t`.
- STORAGE draine le ring, réserve et mappe le fichier, arbitre les accès SD,
  finalise le WAV et publie la nouvelle génération.
- `REC_SOURCE` possède quatre descripteurs générationnels bornés et leur cycle
  `FREE/PREPARED`, `BUILDING`, `CURRENT`, `UNDO`, `RETIRED`.
- Le Streamer est l'unique moteur de playback. Une track Stream utilise
  `SOURCE=POOL` ou `SOURCE=REC` avec le même reader, le même cache paginé, le
  même décodeur PCM24 et les mêmes leases.

Le Recorder ne connaît aucune track consommatrice. Le Streamer ne connaît ni
les fichiers temporaires `.REC`, ni la finalisation, ni l'alternance A/B.

## Data-plane live

À 48 kHz, stéréo PCM24 représente 6 octets par frame et 288 000 octets/s.

```text
mixer AUDIO, sources routées après leur traitement pertinent
  -> bus REC stéréo float
  -> conversion/saturation PCM24 int32
  -> ring AUDIO -> STORAGE de 12 001 frames (~250,02 ms)
  -> generic_recorder
  -> deux buffers préalloués de 32 KiB
  -> extents physiques pré-réservés
  -> scheduler SD partagé
  -> SDMMC DMA direct
```

Le head du ring appartient à AUDIO. Le tail accepté/committé appartient à
STORAGE. AUDIO ne fait aucun appel FatFs et n'attend jamais la carte. Un vrai
dépassement head-tail ferme la capture avec `AUDIO_RECORDER_ERROR_RING_OVERFLOW`.
Chaque buffer de 32 KiB représente environ 113,8 ms de PCM.

## Préparation, arrêt et finalisation

Avant START, STORAGE crée le fichier temporaire, pré-réserve sa première zone
et obtient sa carte d'extents physiques. START et STOP sont publiés dans la
FIFO CONTROL -> AUDIO avec un sample time et un identifiant de session.

Après STOP, STORAGE continue à drainer le ring. La finalisation progresse par
étapes coopératives : commit des données, libération de la réservation,
écriture du header WAV, synchronisation, fermeture puis rename `.REC` vers
`.WAV`. Aucun `f_write` FatFs ne se trouve dans le data-plane live.

Le trigger `PATTERN` ne gouverne que le départ. Dès qu'une longueur fixe est
configurée, le compteur de frames gouverne l'arrêt automatique, quel que soit
le trigger qui a démarré la prise.
L'armement reste en phase d'admission tant que PREPARE n'a pas publié une
session Recorder prête ; `WAIT` commence ensuite. Si le transport est déjà
actif, la prochaine frontière de pattern lance la prise, y compris avec
`QUANT=NOW`. Au démarrage du transport, `QUANT=NOW` peut lancer à sa frontière
initiale.

Avant de réserver un nouveau BUILDING, la session Recorder terminale de la
prise précédente est fermée. Le fichier de `REC_SOURCE CURRENT` reste détenu
par sa génération et ne fait pas partie du discard de cette session.

## REC_SOURCE et générations

```text
RETIRED + UNDO + CURRENT + BUILDING = 4 descripteurs maximum
```

Les noms physiques `REC_GEN_n` ne portent aucun rôle. Le descripteur est
l'autorité et porte la clé de génération, l'état, l'ownership
`TEMPORARY/PERSISTENT`, le chemin, la carte d'extents, l'epoch média et le
résumé waveform. Une prise crée `BUILDING` dans un descripteur libre. Pendant DRAINING, STORAGE
enregistre la source live dans le page-cache avec une clé comprenant slot et
génération, met à jour `readable_frames` et réserve les premières pages.

Lorsque le fichier est finalisé, le chemin du cache devient le `.WAV`. La
publication attend une identité d'enregistrement valide, son waveform READY et
les premières pages READY. La même opération canonique bascule `CURRENT` pour
une publication, un Undo ou un Redo. L'ancien `CURRENT` devient `UNDO`.
L'expiration de l'unique action audio le rend `RETIRED`; sa clé, son cache et
son workspace ne sont recyclés qu'après extinction des leases, readers et I/O.
Un descripteur `PERSISTENT` peut être libéré du cache mais son fichier n'est
jamais supprimé par ce recyclage.

## Undo/Redo global

L'historique unique est une chronologie typée SEQ/AUDIO. Il conserve huit
transactions SEQ et au plus une transaction AUDIO supplémentaire. Une
transaction AUDIO porte `before_generation` et `after_generation`; zéro est
l'état EMPTY. Undo et Redo ne reconstruisent aucun PCM : ils rebasculent le
`CURRENT`, sa waveform et ses pages initiales. Une nouvelle action après Undo
supprime la branche Redo. Une nouvelle action AUDIO retire sélectivement
l'ancienne action AUDIO sans modifier l'ordre relatif des entrées SEQ.

Cette préparation fournit le reloop immédiat par le reader normal du Streamer,
sans preroll global ni relais RAM -> SD spécifique.

## Streamer

`SOURCE=POOL` résout un asset du pool. `SOURCE=REC` résout le snapshot READY
de `REC_SOURCE CURRENT`. Quitter REC EDIT ne modifie pas ce CURRENT : STOP,
retrigger et wraps résolvent donc la même génération. Une publication REC,
REPLACE, OVERDUB ou Undo/Redo bascule le CURRENT par la transition canonique.
Les readers conservent leurs leases physiques jusqu'à leur arrêt ; STORAGE
attend leur extinction et celle des I/O/cache avant de recycler RETIRED.

Le Streamer ne porte aucun crossfade produit. Le crossfade entre une track et
MASTER, LINE, USB ou une autre track est le modèle XFADE du domaine Insert
Audio FX. `REC_SOURCE` ne constitue pas un bus live et n'est pas une cible
XFADE; il reste lisible uniquement par un Streamer.

## OVERDUB

OVERDUB prend un snapshot immutable de la génération N. Son reader AUDIO est
indépendant du moteur de voix Streamer mais partage les pages immuables du
page-cache avec des leases et un playhead propres. Le mixer somme ce flux avec
les sources live routées, puis alimente le ring Recorder qui construit N+1.
Il n'existe aucune écriture in-place.

Un page miss ou une invalidation du snapshot ferme la prise avec
`AUDIO_RECORDER_ERROR_OVERDUB_UNDERRUN`. Cette cause est conservée jusqu'au
status produit; elle n'est pas reclassée en ring overflow. Les erreurs média,
état invalide et manque d'espace restent distinctes.

## Waveform de prise

AUDIO publie dans le ring le PCM24 final qui est l'unique point source du
résumé. Avant de rendre les frames du ring recyclables, STORAGE alimente 4096
couples min/max int16. Une longueur fixe utilise le mapping direct
frames-vers-bins. Une longueur libre utilise des niveaux bornés et compacte
progressivement, entièrement hors IRQ. Le résumé READY
(environ 16 KiB) est copié dans le descripteur `BUILDING` et publié avec lui.
L'éditeur l'utilise directement pour une prise fraîche; le scan SD reste le
fallback des fichiers externes, anciens, récupérés ou sans résumé valide.

## Navigation waveform REC EDIT

Le zoom horizontal va désormais de l'overview à 126 frames visibles sur les
126 colonnes internes de la waveform OLED : au maximum, une colonne correspond
à une frame PCM. Le modèle mémorise la conversion zoom-vers-fenêtre afin de ne
pas répéter `powf` à chaque rendu. Les bornes de colonne sont avancées par
quotient/reste; seules les divisions initiales dépendent de la largeur.

Le chemin de données est gradué :

- le résumé REC 4096 bins min/max en SDRAM fournit l'overview immédiat pour
  une génération READY cohérente, avec le pas réel des bins libres conservé ;
  un scan séquentiel de 8 KiB par passage
  STORAGE reste le fallback sans résumé ;
- les WAV persistants d'au moins 60 s possèdent un index `.brkwave` version 2
  avec cinq niveaux min/max (16384, 4096, 1024, 256 et 64 frames/bin), chargé
  en tuiles RAM de 512 bins ; les colonnes du build sont écrites par lots.
  Les anciens index version 1 sont invalidés et reconstruits depuis le WAV ;
- le détail local réutilise le moteur de l'éditeur : 16 tuiles PCM mono de
  24 000 frames et les niveaux dérivés 16/64/256 avec min/max/first/last
  (environ 1 MiB). La vue et ses tuiles voisines sont demandées, les tuiles
  proches du focus restent chaudes, et une inversion de pan réutilise leurs
  préfixes chargés. Une lecture PCM24 stéréo d'au plus 4092 octets est admise
  par passage STORAGE. Les colonnes précises déjà présentes en RAM se
  dessinent sans attendre la fin de la tuile ; seules les colonnes absentes
  utilisent l'overview REC.

Les lectures FatFs de la waveform ne partent plus du rendu ou du tick UI :
`waveform_service_storage_service()` est appelé dans la phase STORAGE de
la superloop. Le moteur local utilise l'admission BG du scheduler SD et
laisse la main aux besoins audio; les jobs min/max globaux restent utiles aux
zooms larges. L'UI ne choisit ni tuile ni fallback : le service retourne les
colonnes min/max et, quand le détail local est READY, les points de ligne
first/last destinés au tracé continu. La composition OLED et
son flush restent des opérations sur toute la page ; elles ne sont pas un
scroll matériel incrémental.

## SAVE / CROP

SAVE n'est pas nécessaire au playback. Pour une prise complète temporaire, il
écrit et synchronise d'abord un des deux slots `REC_PROMOTE.0/1`, renomme le
workspace sur le même volume, met à jour le chemin du page-cache puis passe la
même génération en ownership `PERSISTENT`. Aucune copie PCM n'a lieu. Ce
checkpoint expire seulement l'action AUDIO; les entrées SEQ restent en place.

Le journal redondant, séquencé et contrôlé par checksum publie successivement
`INTENT`, `RENAMED` et `COMMITTED`. Au boot, ancien chemin seul signifie
rollback, nouveau chemin seul signifie adoption `PERSISTENT` et rebind du
cache. En cas d'état ambigu, aucun des deux côtés n'est supprimé.
Un vrai crop reste l'export transactionnel séquentiel 32 KiB : il sélectionne
une plage de frames et recopie uniquement son PCM.

L'export est un job de superloop. Les `FIL` source et destination restent
ouverts et avancent séquentiellement. Chaque appel réalise au plus une opération
de métadonnées ou un transfert de 32 KiB, après admission comme client
BACKGROUND du scheduler, puis rend la main. La recherche du prochain nom fait
un seul `f_stat` par appel. Lecture et écriture sont des phases séparées.

La destination est d'abord `RECxxxx.TMP`. Le header est ajusté à la plage,
le contenu est synchronisé et les deux fichiers sont fermés avant le rename
final. Le workspace source reste intact jusqu'au commit. En cas d'erreur, le
job ferme ses handles, supprime le `.TMP` et ne publie aucun WAV partiel.

## Arbitrage SD et coopération

Recorder WRITE et Streamer READ restent les clients temps réel prioritaires.
Preview, Browser, caches, Pattern/Project et SAVE/CROP utilisent les contrats
du scheduler. La copie séquentielle SAVE/CROP s'annonce comme BACKGROUND avec
un quantum maximal de 32 KiB; elle ne garde pas le gate entre deux appels et ne
contient pas de boucle sur le fichier entier. Ce quantum de données ne
s'applique ni à EXTEND ni à RELEASE: leurs transactions de métadonnées FAT/exFAT
restent sectorielles afin de conserver les points d'arbitrage.

La récupération, la préparation, le drain et la finalisation du Recorder sont
des machines d'état progressées par `audio_recorder_service()` et
`generic_recorder_service()`. Les opérations FatFs sont exécutées hors IRQ.

La granularité canonique des gros transferts séquentiels est une page de
32 KiB: une lecture Streamer contiguë, un buffer PCM Recorder ou une phase de
copie SAVE/CROP peut consommer au plus cette quantité par admission. Une limite
plus petite reste volontaire pour les services couplant I/O et calcul sous un
budget CPU, ainsi que pour Preview. EXTEND, RELEASE, allocation et sync restent
à la granularité native secteur/cluster des métadonnées FAT/exFAT.

## Looper historique

Le backend `brick6_looper_runtime`, son preroll, ses masques PLAYING/start, son
reader, ses leases, son bus d'enregistrement et `looper_storage` ont été
supprimés. Playback/loop/trigs/pitch/stretch appartiennent au Streamer,
OVERDUB et routing universel à Audio REC, générations et durée de vie à
`REC_SOURCE`.
