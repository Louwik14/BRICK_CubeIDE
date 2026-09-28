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

Une longueur fixe gouverne l'arrêt automatique par compteur de frames. Son
unité est la barre du transport (quatre noires, soit seize steps au pas courant)
pour tous les triggers. `LEN 4B` correspond donc à quatre barres indépendamment
de `LEN` et `DIV` des tracks. Le cycle de la plus longue track active
(traversée et division comprises) sert uniquement à choisir la frontière de
départ du trigger `PATTERN` ; il ne multiplie pas la durée de la prise.
L'armement reste en phase d'admission tant que PREPARE n'a pas publié une
session Recorder prête ; `WAIT` commence ensuite. Si le transport est déjà
actif, la prochaine frontière de pattern lance la prise, y compris avec
`QUANT=NOW`. Au démarrage du transport, `QUANT=NOW` peut lancer à sa frontière
initiale.
Le service de frontière quantifiée s'exécute dans la superloop CONTROL avant
les services Storage. Sa cadence ne dépend pas du tick UI, plus lent que la
fenêtre de publication AUDIO d'un demi-bloc.

Avant de réserver un nouveau BUILDING, la session Recorder terminale de la
prise précédente est fermée. Le fichier de `REC_SOURCE CURRENT` reste détenu
par sa génération et ne fait pas partie du discard de cette session.

Après SAVE, `REC_SOURCE CURRENT` conserve la génération et le fichier promu.
ASSIGN vers une piste Streamer crée ou retrouve séparément un asset classique
dans le POOL, attend qu'il soit jouable puis sélectionne cet asset et met
`SOURCE=POOL`. Si le transport tourne, la création dans le POOL attend son arrêt.
La restauration de projet conserve, elle, la valeur SOURCE sauvegardée.

La LED REC suit la capture AUDIO effective, même pendant le drain et la
finalisation. Lorsqu'une piste Streamer déjà en lecture rebinde une nouvelle
génération `REC_SOURCE`, elle conserve sa phase de boucle : la date de
publication du fichier ne devient pas un nouveau départ musical.

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

## Cycle REC ARM et déclenchement

TRACK+REC ouvre temporairement AUDIO REC et arme le modèle de capture. Le
relâchement de TRACK restaure la page et le mode Hall précédents, quel que soit
l'ordre de relâchement de REC ; la capture armée continue en arrière-plan.
L'annulation du REC global libère aussi l'armement AUDIO REC.

En mode PATTERN, un transport démarrant avec une quantification NOW déclenche
la prise au départ. Si le transport tourne déjà, le service CONTROL programme
le prochain cycle musical fourni par le moteur SEQ. La quantification BAR
attend la prochaine barre du transport ; PATTERN attend le cycle de la plus
longue track active. La limite calculée est conservée jusqu'à son échéance ;
START porte le timestamp de cette limite quand elle entre dans le prochain bloc
non publié. Si elle a déjà été publiée, le modèle attend la frontière musicale
suivante au lieu de décaler START au présent. Une prise finale de zéro frame libère son slot REC_SOURCE et remonte
au modèle comme fin de cycle, ce qui rend possible un nouvel armement.

## Trace GDB du cycle REC

`g_rec_sd_trace_next` est le nombre total d'événements écrits. Les 64
entrées de `g_rec_sd_trace` forment un anneau ; l'entrée de séquence `n`
est à l'indice `(n - 1) % 64`. Chaque entrée occupe treize mots de 32 bits :
`sequence, event, states, context, detail, frames, session, sample_lo,
sd_owner, sd_flags, sd_io, sd_result, sd_hal_error`.
Une séquence nulle signale une entrée vide ou incomplète. La trace est en
SRAM3 non cacheable pour rester visible après l'arrêt par GDB.

`states` contient, de l'octet faible au fort, l'état Recorder avant/après
puis la phase Storage avant/après. `context` contient l'état Overdub
avant/après, l'armement puis le trigger. `0xFF` indique une valeur non
pertinente. `detail` dépend de l'événement. `frames` est le curseur de
capture, ou l'identifiant de source pour Overdub. `sample_lo` est le bas du
sample cible lorsque disponible. `session`
contient l'identifiant de session Recorder, la génération de source Overdub
ou celle de la prise visible selon le producteur.

Codes `event` : 1 armement, 2 trigger, 3 préparation, 4 demande START,
5 demande STOP, 6 annulation, 7 transition Recorder, 8 transition Storage,
9 START audio, 10 STOP audio, 11 fermeture audio, 12 liaison Overdub,
13 arrêt Overdub, 14 faute Overdub, 15 prise prête, 16 refus du gate SD,
17 attente/refus du scheduler SD, 18 erreur bloc/disque SD,
19 erreur FatFs/métadonnées. Pour Storage,
`detail` contient la sous-phase finale (octet 0), l'erreur Recorder (octet 1),
l'état du writer générique (octet 2) et son erreur (octet 3).
Pour Overdub, une liaison refusée
porte la raison 1 (source), 2 (plan) ou 3 (reader) ; la faute porte l'erreur
dans l'octet 0, `underrun` dans l'octet 1 et le nombre de frames produites
dans les deux octets hauts.

Etats Recorder : `0 IDLE, 1 PREPARED, 2 RECORDING, 3 DRAINING,
4 FINALIZING, 5 TAKE_READY, 6 FAILED`. Phases Storage : `0 IDLE,
1 PREPARING, 2 PREPARED, 3 DRAINING, 4 FINALIZING, 5 TAKE_READY,
6 FAILED`. Armement : `0 OFF, 1 REC, 2 TRIG`. Trigger : `0 NOW,
1 THRESHOLD, 2 PATTERN, 3 THRESHOLD_PLAY`. Overdub : `0 inactif,
1 actif`. Pour les demandes START/STOP, `detail=1` signifie accepté ;
`0` signifie refusé. Côté Recorder, START utilise aussi `2` pour un
remplacement projet actif et `3` pour un refus de publication. Dans le modèle
de capture, les refus START portent
`0x100` (session non prête), `0x200` (bus non publié) ou `0x300`
(commande Recorder refusée). Le trigger porte `1` à la réception du
seuil et `2` lorsque la limite musicale est retenue.

Pour `event=12` émis par le modèle de capture, `detail=0x100 | enabled` indique
le basculement du réglage Overdub (`0x101` = activé). Ce n'est pas un résultat
de liaison AUDIO. La liaison effective n'est tentée que pendant une capture
active ; ses événements AUDIO portent `detail=0` en cas de succès ou `1`, `2`,
`3` pour un refus de source, de plan ou de reader.

Les cinq mots SD sont remplis uniquement par les événements CONTROL/STORAGE
qui prennent un instantané SD ; zéro ailleurs :

| Mot | Octets, du faible au fort |
| --- | --- |
| `sd_owner` | owner du gate, client demandeur, owner scheduler, classe scheduler |
| `sd_flags` | nombre de prises du gate, indicateurs, état matériel bloc, nombre d'opérations en file |
| `sd_io` | opération bloc active, client bloc, bit 16 faute bloquée / bit 17 erreur IRQ, statut du support |
| `sd_result` | admission, code bloc, FRESULT FatFs, opération Recorder |
| `sd_hal_error` | masque `HAL_SD_GetError(&hsd1)` |

Indicateurs `sd_flags` octet 1 : bit 0 streaming critique, bit 1 filesystem
Recorder logique actif, bit 2 background actif, bit 3 exclusivité demandée,
bit 4 exclusivité active. `0xFF` dans les codes bloc/FatFs signifie
non disponible. Admissions : `0 sans décision, 1 accepté, 2 différé,
3 erreur, 4 terminé`. Opérations : `0 aucune, 1 mount, 2 mkdir,
3 slot/source, 4 prepare, 5 write, 6 finalize, 7 stop, 8 cancel,
9 DMA, 10 disk read, 11 disk write`. Clients gate : `0 aucun,
1 RECORDER, 2 SAMPLE_BOOT, 3 PATTERN, 4 PROJECT, 5 SAMPLE_CACHE,
6 PREVIEW, 7 WAV_CONVERT, 8 EDITOR_CACHE, 9 WAVEFORM_CACHE,
10 SAMPLE_STREAM, 11 PATCH, 12 SCHEDULED_RECORDER, 13 BACKGROUND,
14 CRASH`. Owner scheduler : `0 idle, 1 read DMA,
2 write DMA, 3 filesystem, 4 background, 5 recovery abort`. Classe :
`0 aucune, 1 read, 2 write, 3 filesystem`. Etat bloc : `0 idle,
1 read DMA, 2 read card ready, 3 write DMA, 4 write card ready,
5 aborting, 6 error latched`. Statut support : `0 inconnu, 1 prêt,
2 absent, 3 faute`.

`event=16` porte en `detail` le motif gate : `1 filesystem Recorder actif,
2 streaming critique, 3 conflit d'owner`. `event=17` avec `detail & 0x100`
indique une attente de scheduler : raison basse `1 background, 2 exclusif
actif, 3 exclusif demandé, 5 read DMA, 6 write DMA, 9 recovery abort` ;
sinon `detail` décrit le résultat de provider ou le refus de métadonnées.
`event=18` avec opération `10/11` donne un `DRESULT` en octet bas de
`detail`, puis l'étape `1 attente carte, 2 départ DMA, 3 fin DMA,
4 carte prête` en octet 1 ; `frames` contient le secteur et `session` le
nombre de secteurs. Les autres `event=18` donnent le LBA en `detail` et
le code `sd_block_device_result_t` en `sd_result` octet 1. `event=19`
donne le code FatFs dans `sd_result` octet 2 lorsqu'il est connu ;
les refus de slot utilisent `detail=3` (slot déjà en construction),
`4` (aucun slot libre) ou `0x100 | slot` (unlink refusé).
Pour le montage, `detail=0x100 | statut support` signifie que FatFs n'a pas
été appelé : le support était déjà marqué absent ou fautif. Les codes
FatFs principaux sont `0 OK, 1 DISK_ERR, 2 INT_ERR, 3 NOT_READY,
4 NO_FILE, 5 NO_PATH, 7 DENIED, 8 EXIST, 13 NO_FILESYSTEM,
15 TIMEOUT, 17 NOT_ENOUGH_CORE`. Les codes bloc sont ceux de
`sd_block_device_result_t` : `0 OK, 1 INVALID_ARG, 2 ISR_CONTEXT,
3 GATE_NOT_HELD, 4 READ_FAIL, 5 WRITE_FAIL, 6 QUEUE_FULL, 7 BUSY,
8 TIMEOUT, 9 MEDIA_CHANGED, 10 CARD_REMOVED, 11 ABORTED,
12 DMA_START_FAIL, 13 ABORT_FAILED`.

Le message UI `SD I/O` regroupe plusieurs origines : FatFs au montage ou
au choix du slot, échec de préparation ou de finalisation du fichier,
erreur du writer/transport, et prise finalisée avec zéro frame. Un gate
occupé renvoie normalement `NOT_NOW` et doit laisser la préparation en
attente. Corréler la séquence des événements 3, 8, 15 à 19 pour distinguer
le cas réel sur le matériel.

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
