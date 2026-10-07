# Benchmark SD aleatoire 64 KiB (temporaire)

Ce firmware Release est volontairement un firmware de mesure. Apres
initialisation du stockage, il ne demarre ni AUDIO, ni UI, ni sequenceur. Il
monte FatFs, cree si necessaire `0:/BRICK/TEST/SD_RANDOM.B6T`, puis reste dans
le benchmark jusqu'a son etat terminal.

Le fichier fait 512 MiB lorsque l'espace libre le permet, avec repli sur
256 MiB. Sa creation est volontairement sequentielle, uniquement par
`f_write` de tranches de 64 KiB, avant `f_sync`, fermeture et reouverture.
Un fichier existant de 512 ou 256 MiB est reutilise.

Les 100 000 lectures font exactement 64 KiB. Chaque balayage utilise une
permutation pseudo-aleatoire deterministe de toutes les pages du fichier. Le
sample page cache est contourne : le chemin mesure est la map physique FatFs,
le provider STREAM, le scheduler SD, le block device SDMMC/IDMA et sa
maintenance D-cache, dans un buffer benchmark unique.

`g_sd_random_bench` contient la progression, l'etape d'echec, l'offset
d'ecriture, le dernier nombre d'octets ecrits, les etats disque/carte et les
resultats en microsecondes.

`fail_step` vaut 1 MOUNT, 2 MKDIR, 3 OPEN, 4 EXPAND (reserve, non utilise),
5 WRITE, 6 SYNC, 7 CLOSE, 8 REOPEN, 9 MAP ou 10 RANDOM_READ.
Les percentiles sont exacts sur les 100 000 echantillons. `total_time_us` est
la somme des latences request-to-ready; les debits en derivent. Les timestamps
DWT de la derniere lecture permettent de verifier request, debut SD, fin DMA et
READY.

Bloc GDB unique :

```gdb
shell cls
set pagination off
set print pretty on
info address g_sd_random_bench
p g_sd_random_bench
```
