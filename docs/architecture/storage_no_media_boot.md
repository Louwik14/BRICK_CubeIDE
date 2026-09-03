# STORAGE sans carte SD au boot

Sur H743 LowCost, aucun GPIO card-detect n'est câblé. `BSP_SD_IsDetected()`
reflète uniquement la dernière initialisation BSP réussie et ne constitue pas
une mesure physique de hotplug. `HAL_SD_Init()` est la sonde média.
Son échec n'est fatal que si le diagnostic ne correspond pas à une absence de
média. Le chemin FatFs (`disk_initialize()` puis `f_mount()`) publie alors
`SD_STORAGE_STATUS_NO_MEDIA`; une erreur HAL non liée à l'absence de réponse
et une erreur de système de fichiers publient `SD_STORAGE_STATUS_FAULT`.

Dans la HAL H7 locale, `0x10000000` est
`HAL_SD_ERROR_UNSUPPORTED_FEATURE`. Pendant `SD_PowerON()`, la HAL remplace par
ce code les échecs CMD55/ACMD41, y compris l'absence de réponse observée sans
carte. Ce code est donc assimilé à `NO_MEDIA` uniquement par le classifieur
d'échec d'initialisation; il n'est pas reclassé globalement.

`STORAGE_IO` reste planifié dans les deux cas. Le gate FatFs mémorise l'état,
ce qui borne l'échec des services et évite les remounts répétés. CONTROL,
AUDIO et UI ne dépendent pas de ce résultat.

Au boot, l'UI attend que STORAGE ait publié `READY` avant de demander une
restauration. Avec `NO_MEDIA` (ou `FAULT`), elle termine directement l'écran
de chargement et conserve le projet blank/default déjà initialisé en mémoire;
aucun Project Load, scan catalogue ou loader SD n'est lancé.

La requête CONTROL ne déclenche pas le quiesce. STORAGE valide d'abord la
restauration et construit le candidat; seul ce candidat valide demande ensuite
le quiesce avant son remplacement atomique. Ainsi, une restauration refusée
par `NO_MEDIA` ne ferme pas les ingress et ne touche pas à la preview. Le stop
preview du quiesce reste conditionné à l'existence d'une session active.

Le refresh explicite d'un browser SD réutilise sa requête catalogue vers
`STORAGE_IO`. Si l'état est `NO_MEDIA`, le service effectue une unique nouvelle
tentative init/mount avant le scan. Un succès publie `READY` puis poursuit le
refresh normal; un nouvel échec sans carte reste `NO_MEDIA`. Il n'existe ni
polling, ni retry automatique, ni restauration Project associée au refresh.
