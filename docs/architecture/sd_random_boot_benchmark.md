# Benchmark SD aleatoire 64 KiB (temporaire)

Ce firmware Release demarre par le boot BRICK normal complet, AUDIO et UI
compris. Lorsque l'ecran de boot est termine et que l'audio a demarre, il attend
trois secondes. Il attend ensuite que le gate, le scheduler et le block device
SD soient idle, arrete l'audio et bascule dans le benchmark exclusif. A partir
de cette bascule, UI, sequenceur et services Storage normaux ne sont plus
servis; le DMA AUDIO est arrete, TIM12 est stoppe et l'ecran reste fige sur sa
derniere image normale. Le benchmark cree si necessaire
`0:/BRICK/TEST/SD_RANDOM.B6T`, puis reste actif jusqu'a son etat terminal.
Juste avant l'arret AUDIO, `usb_audio_transport_reset()` desactive les flux
Audio USB IN et OUT et remet leurs rings a zero. Le device USB n'est pas
arrete : les autres classes restent intactes, mais aucun callback Audio OUT ne
peut continuer a remplir `pc_to_brick` sans consommateur.

Le fichier fait 512 MiB lorsque l'espace libre le permet, avec repli sur
256 MiB. Sa creation est volontairement sequentielle, uniquement par
le wrapper normal `persistent_fatfs_io` et son sink de tranches de 64 KiB,
avant `f_sync`, fermeture et reouverture via ce meme wrapper. Le benchmark ne
remonte pas FatFs : il exige le statut READY etabli par le boot normal. Un
fichier existant de 512 ou 256 MiB est reutilise.

Les 100 000 lectures font exactement 64 KiB. Chaque balayage utilise une
permutation pseudo-aleatoire deterministe de toutes les pages du fichier. Le
sample page cache est contourne : le chemin mesure est la map physique FatFs,
le provider STREAM, le scheduler SD, le block device SDMMC/IDMA et sa
maintenance D-cache, dans un buffer benchmark unique.

Pour cette campagne a demi-frequence, la source SDMMC est PLL2R a 200 MHz.
Le chemin normal High Speed utilise `CLKCR.CLKDIV=2`, soit 50 MHz selon
`SDMMC_CK = kernel / (2 * CLKDIV)`. A l'entree du benchmark exclusivement,
`hsd1.Init.ClockDiv` et le seul champ `CLKCR.CLKDIV` passent a 4, soit 25 MHz.
Largeur 4 bits, mode carte, commandes multi-blocs et IDMA restent
inchanges. `sd_clock_divider` et `sd_clock_hz` publient les valeurs relues et
calculees dans `g_sd_random_bench`.

`g_sd_random_bench` contient la progression, l'etape d'echec, l'offset
d'ecriture, le dernier nombre d'octets ecrits, les etats disque/carte et les
resultats en microsecondes. Quatre histogrammes de 2 048 buckets a pas de
10 us donnent P50/P90/P99/P99.9 pour `request_to_ready`,
`before_transaction`, `physical_transaction` et `after_transaction`.
Chaque metrique contient aussi `count`, `sum_us`, `average_us`, `min_us` et
`max_us` mesures directement.

Les frontieres DWT mesurees sur le vrai chemin sont :

- `request_cycles` : juste avant l'inscription de la requete dans le backend
  physique ;
- `perf_accept_cycles` : requete acceptee par ce backend ;
- `perf_map_start_cycles` / `perf_map_end_cycles` : resolution de la map
  physique par le provider STREAM ;
- `perf_submit_enter_cycles` : entree dans la soumission block device ;
- `perf_submit_cycles` : requete acceptee dans la FIFO du block device ;
- `perf_launch_enter_cycles` : debut de preparation du lancement materiel ;
- `perf_pre_cache_start_cycles` / `perf_pre_cache_end_cycles` : invalidate
  D-cache conservateur avant lecture ;
- `perf_command_cycles` : instant juste avant l'appel qui ecrit ARG/CMD et
  active le CPSM pour la commande READ SDMMC initiale ; cette valeur est donc
  posee avant qu'une IRQ CMDREND puisse preempter le chemin, et n'est jamais
  remplacee par celle de CMD12 ;
- `perf_data_start_cycles` : reponse commande observee dans l'IRQ SDMMC et
  passage du transport en phase DATA ;
- `perf_data_end_cycles` : drapeau SDMMC DATAEND observe dans l'IRQ ;
- `perf_complete_cycles` : fin du transport apres CMD12 pour la lecture
  multi-blocs ;
- `perf_cache_start_cycles` / `perf_cache_end_cycles` : invalidate D-cache
  post-DMA obligatoire ;
- `perf_publish_cycles` : completion publiee par le block device ;
- `io.perf_complete_cycles` : completion consommee par le backend physique ;
- `ready_cycles` : resultat vu READY par le benchmark.

Les quatre decompositions principales sont exactement : request -> READY,
request -> commande SDMMC, commande SDMMC -> completion physique, puis
completion physique -> READY. Les sous-metriques exposent en plus acceptation
backend, attente avant map, map, soumission et admission block device,
attente de lancement, preparation materielle, maintenance cache, reponse
commande, transfert DATA, CMD12, publication block device et remontee backend.
`data_start` et `data_end` sont necessairement des observations IRQ :
le debut du premier octet sur le bus et la fin electrique exacte ne sont pas
exposes separement par le controleur. Leur ecart inclut donc la latence IRQ aux
deux bornes, sans phase artificielle.

Avant toute soustraction, les 19 timestamps sont verifies selon leur ordre
causal. Une distance DWT non signee superieure a la demi-periode signale une
borne inversee, incremente `timestamp_order_errors`, memorise l'index de
transition dans `last_timestamp_order_error_edge`, puis arrete le benchmark en
erreur. Une transition correcte qui traverse le wrap 32 bits reste valide.
`perf_dma_cycles` est le retour CPU du lancement : CMDREND peut le preempter,
donc il n'est pas contraint par rapport a `perf_data_start_cycles`. La chaine
physique validee reste commande READ -> CMDREND/DATA -> DATAEND -> CMD12.

RAM statique : les quatre histogrammes occupent 32 768 octets, contre 65 536
octets pour les deux anciens histogrammes. Les resultats et timestamps ajoutent
1 460 octets. Le bilan net est donc une economie de 31 308 octets par rapport
au benchmark precedent.

`fail_step` vaut 1 MOUNT, 2 MKDIR, 3 OPEN, 4 EXPAND (reserve, non utilise),
5 WRITE, 6 SYNC, 7 CLOSE, 8 REOPEN, 9 MAP ou 10 RANDOM_READ.
Les percentiles couvrent les 100 000 echantillons avec une quantification de
10 us. `total_time_us` est
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
