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
- `REC_SOURCE` possède les workspaces A/B et leur cycle
  building/current/retired.
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

## REC_SOURCE et workspaces A/B

```text
0:/REC/REC_WORK_A.REC -> 0:/REC/REC_WORK_A.WAV
0:/REC/REC_WORK_B.REC -> 0:/REC/REC_WORK_B.WAV
```

Une prise crée `building` dans le slot libre. Pendant DRAINING, STORAGE
enregistre la source live dans le page-cache avec une clé comprenant slot et
génération, met à jour `readable_frames` et réserve les premières pages.

Lorsque le fichier est finalisé, le chemin du cache devient le `.WAV`. La
publication attend une identité d'enregistrement valide et les premières pages
READY. `current` bascule alors atomiquement. L'ancien current devient retired
et reste intact tant que des readers ou leases le référencent. Sa clé, son
cache et son workspace ne sont recyclés qu'après libération complète.

Cette préparation fournit le reloop immédiat par le reader normal du Streamer,
sans preroll global ni relais RAM -> SD spécifique.

## Streamer et XFADE

`SOURCE=POOL` résout un asset du pool. `SOURCE=REC` résout uniquement le
snapshot immutable READY de `REC_SOURCE.current`; sans current valide, il rend
du silence.

Le XFADE appartient au chemin Streamer/mixer : 0 donne le live seul, 127 la
source REC seule et les valeurs intermédiaires un crossfade. Il ne dépend ni
d'un état PLAYING historique ni de l'existence d'une boucle. À 127, une source
REC vide donne donc du silence.

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

## SAVE / CROP

SAVE n'est pas nécessaire au playback. Il exporte `REC_SOURCE.current` vers un
fichier utilisateur `0:/REC/RECxxxx.WAV`. Un crop sélectionne une plage de
frames et recopie uniquement son PCM dans cet export.

L'export est un job de superloop. Chaque appel réalise au plus une opération
de métadonnées ou un transfert de 4 KiB, après admission comme client
BACKGROUND du scheduler, puis rend la main. La recherche du prochain nom fait
un seul `f_stat` par appel. Lecture et écriture sont des phases séparées.

La destination est d'abord `RECxxxx.TMP`. Le header est ajusté à la plage,
le contenu est synchronisé et les deux fichiers sont fermés avant le rename
final. Le workspace source reste intact jusqu'au commit. En cas d'erreur, le
job ferme ses handles, supprime le `.TMP` et ne publie aucun WAV partiel.

## Arbitrage SD et coopération

Recorder WRITE et Streamer READ restent les clients temps réel prioritaires.
Preview, Browser, caches, Pattern/Project et SAVE/CROP utilisent les contrats
du scheduler. SAVE/CROP s'annoncent comme BACKGROUND avec un quantum maximal
de 4 KiB; ils ne gardent pas le gate entre deux appels et ne contiennent pas de
boucle sur le fichier entier.

La récupération, la préparation, le drain et la finalisation du Recorder sont
des machines d'état progressées par `audio_recorder_service()` et
`generic_recorder_service()`. Les opérations FatFs sont exécutées hors IRQ.

## Looper historique

Le backend `brick6_looper_runtime`, son preroll, ses masques PLAYING/start, son
reader, ses leases, son bus d'enregistrement et `looper_storage` ont été
supprimés. Playback/loop/trigs/pitch/stretch/XFADE appartiennent au Streamer,
OVERDUB et routing universel à Audio REC, générations et durée de vie à
`REC_SOURCE`.
