# Streaming sample produit

## Contrat

Classic STREAM, Multi et REC_SOURCE utilisent un seul Page Cache. Un slot fait
exactement 16 KiB. En FLOAT32 stereo entrelace, cela represente 2048 frames.
Les 376 slots produit sont conserves: 340 slots generaux et 36 slots reserves
aux neuf readers garantis et a leurs quatre roles `CURRENT`, `NEXT`,
`LOOP_START`, `LOOP_START_NEXT`. Le payload occupe donc 6 160 384 octets en
SDRAM. Le passage depuis 64 KiB libere 18 481 152 octets de SDRAM sans changer
le nombre de slots, les identifiants, les leases, les generations, l'eviction
ou les refcounts/protections existants. Ce choix libere la SDRAM au lieu de
porter le cache a 1504 slots: le pool general partage par STREAM, RAM, Multi et
Wavetable passe mecaniquement de 22 282 240 a 5 570 560 octets. Augmenter le
nombre de slots aurait ajoute environ 154 KiB de metadata chaude et depasse le
budget D1 actuel; aucun placement agressif du prototype n'est repris.

Une page suit toujours `FREE -> RESERVED -> LOADING -> READY`, avec les chemins
`EVICTING` et `FAILED` existants. `LOADING` porte en plus un watermark monotone
`available_frame_end`. Il vaut zero au debut, progresse de 64 frames par chunk
stereo de 512 octets, puis vaut 2048. Une completion n'est publiee que si la
cle, le slot, la generation et l'epoch d'enregistrement correspondent encore.

## Lecture SD progressive

Trois granularites independantes sont contractuelles:

```text
page logique du Page Cache = 16 KiB
publication progressive     = 512 octets
I/O physique sequentielle   = 16, 32, 48 ou 64 KiB
```

Le fastpath ne cree aucune page de 64 KiB. Il reserve jusqu'a quatre slots
16 KiB distincts, avec leurs propres cle, slot, generation, etat et watermark,
puis effectue au maximum un seul `CMD18` physique pour les remplir. La constante
`SAMPLE_STREAM_FASTPATH_MAX_PAGES` borne cette premiere version a quatre pages.
Il n'existe qu'un Stream Manager, un scheduler, un backend, un Page Cache et un
VoiceReader.

Une page canonique complete, alignee sur un secteur et deja au format FLOAT32
stereo utilise une seule transaction physique:

```text
CMD18, DLEN=16384
IDMABSIZE=512, IDMABNDT=16
BASE0=page+0, BASE1=page+512
32 x IDMABTC/publication
DATAEND
CMD12 unique
LOADING -> READY
```

L'IDMA fonctionne en double buffer. A chaque `IDMABTC`, le handler identifie le
buffer termine avec `IDMABACT`, rearme ce buffer vers le chunk suivant tant
qu'il en reste un, puis publie le chunk termine. Le H743 signale aussi le
32e/dernier chunk par `IDMABTC`: ce chunk n'est pas rearme et `IDMABTCIE` est
masque, tandis que DPSM et IDMA terminent naturellement `DLEN`. `DATAEND`
declenche ensuite l'unique `CMD12`; la reponse valide la completion normale.

Le descriptor prepare par le block device transporte explicitement le flag
progressif. `chain_next` le restitue au lancement suivant: une lecture
canonique chainee conserve donc les memes 32 publications et ne retombe pas
sur le chemin mono-buffer.

Les pages partielles et les formats non canoniques restent sur le chemin de
lecture asynchrone complet existant. Les writes Recorder/Looper/Overdub ne sont
pas fragmentes en 512 octets: ils conservent ring, coalescing et gros DMA. Le
scheduler et l'arbitrage READ/WRITE communs ne changent pas.

## D-cache et publication AUDIO

Avant le DMA, le block device conserve la preparation de destination existante.
Apres chaque buffer termine, il invalide uniquement ses 512 octets, execute un
`DMB`, puis augmente `available_frame_end`. Une zone deja publiee n'est jamais
invalidee de nouveau. Le chunk, la page et le pool sont alignes sur les lignes
D-cache de 32 octets.

AUDIO ne bloque jamais et ne fait aucun I/O. Apres publication de son lease, le
VoiceReader accepte un descriptor `READY`, ou `LOADING` si son snapshot du
watermark couvre la frame demandee. Le span rendu est borne a ce snapshot; a la
frontiere publiee le reader relache son cursor et retente au bloc suivant. Une
cible START situee plus loin dans la page ne devient applicable que lorsque sa
frame est dans le prefixe publie. Un miss conserve le comportement d'underrun
existant. La validation de generation, epoch, cle et slot reste identique pour
les pages partielles et completes.

START/END live reste plafonne a 12 Hz et conserve sa politique latest-wins. Un
cache hit ne lance aucune transaction SD. Une cible froide reserve une page de
16 KiB et peut etre appliquee des que sa frame appartient au prefixe publie,
avant `READY`. Multi et les transitions de loop reutilisent le meme transport;
aucune plage au-dela du watermark n'est exposee au rendu.

Le Streamer produit reste forward-only. Reverse et ping-pong demeurent dans le
Sampler RAM et ne sont pas generalises au watermark prefixe; aucun comportement
reverse existant n'est modifie.

## IRQ et charge CPU

SDMMC1 est a la priorite NVIC 1 et les DMA AUDIO Stream3/Stream4 a la priorite
2. Le prototype materiel de reference a mesure environ 131 cycles moyens et
316 cycles maximum pour le rearm, 1214/2467 cycles pour le handler complet, et
un intervalle minimum de 9734 cycles entre `IDMABTC`. Le compteur CPU AUDIO
soustrait le temps passe dans SDMMC lorsque celui-ci preempte l'IRQ AUDIO.
Aucune autre priorite n'est changee.

Le placement produit reste celui de `main`: payload en SDRAM, descriptors et
index chauds en D1, cursors/emplois Stream locaux en D2, et controle du cache en
D3. Aucun placement DTCM agressif du prototype n'est repris. Sur le build
Release de reference, le payload fait 6 160 384 octets, les 376 descriptors
font 24 064 octets, l'index fait 15 040 octets et la table last-slot fait
1 544 octets. Les occupations globales sont DTCM 130 816, D1 515 904,
D2 non-cacheable 6 592, D2 cacheable 107 776, SRAM2/D2 130 240,
SRAM3/D2 23 264, D3 52 768 et SDRAM 14 030 944 octets.

## Fastpath sequentiel

Pour un burst coalesce de quatre pages, le transport programme `DLEN=65536`,
conserve `IDMABSIZE=512`, traite 128 `IDMABTC`, puis un seul `DATAEND` et un
seul `CMD12`. Le callback scatter traduit chaque index de chunk en page logique
et chunk dans la page, puis rearme la base IDMA inactive vers le payload final
du slot concerne. Les quatre slots n'ont pas besoin d'etre contigus en SDRAM et
aucun buffer temporaire 64 KiB n'est alloue.

Chaque `IDMABTC` invalide uniquement les 512 octets reellement ecrits et publie
le watermark de la page concernee: 64, 128, ... 2048 frames, puis repart a 64
sur la page suivante. Une page complete reste `LOADING` avec un watermark a
2048 jusqu'au `DATAEND` global. Le contrat `LOADING -> READY` reste donc lie a
la completion backend, tandis qu'AUDIO peut deja lire tout le prefixe publie.
A la completion physique, les quatre tokens sont valides et termines
individuellement.

### Eligibilite, reservation et mapping

L'eligibilite est dynamique et appartient a une voix et sa sequence courante,
pas a un type de track. Le burst est envisage uniquement pour `NEXT`, lorsque
`CURRENT` est deja `READY`, que le kernel est forward, que le lease decrit une
suite sequentielle, qu'aucun saut hors de `CURRENT/NEXT` n'est pending et que
la borne END ou LOOP END laisse au moins deux pages completes utiles. Le pitch
ne sert pas de drapeau d'eligibilite: x1, x2, x4 et x8 utilisent le meme
contrat. La premiere politique tente simplement jusqu'a quatre pages utiles.

Avant la soumission, le Stream Manager fige les keys et generations, verifie
le format FLOAT32 stereo canonique, le mapping physique, puis reserve les slots
necessaires. Le resolver FAT peut traverser plusieurs extents uniquement si le
LBA suivant est exactement adjacent. Il reduit le burst de 4 a 3, 2, puis 1;
une rupture d'extent n'est jamais traversee silencieusement. Un manque de slot,
une page deja presente/loading, une page finale partielle ou un format non
direct reduit egalement le burst. Une seule page retombe sur la transaction
normale 16 KiB. AUDIO n'attend jamais que quatre slots deviennent disponibles.

### Scheduler, sauts et compatibilite

Le scheduler reste unique. L'ordre de besoin est: page `CURRENT` froide/saut
urgent, continuation `NEXT`, puis preparation de boucle ou prefetch plus
lointain. Un fastpath non encore soumis physiquement est annule si le lease de
la voix change; le nouveau seek peut alors lancer son 16 KiB urgent. Une
transaction deja active finit normalement: cette version n'ajoute pas de
preemption `CMD12`.

Un P-lock START, SLICE ou sample publie une nouvelle key/cible. La demande
froide correspondante passe par le chemin normal 16 KiB. Des que le nouveau
lease redevient forward et stable, sa continuation redevient eligible. Un
START live qui reste dans la page utile courante ne retire pas cette
eligibilite; un START hors sequence conserve latest-wins et le meme retour au
fastpath apres stabilisation.

Chaque voix Multi possede toujours son lease et son fastpath. Aucun burst ne
melange deux voix, fichiers ou zones, meme si leurs LBA sont voisins. Classic
garde START live et les P-locks. Pour une loop forward, la borne sequentielle
est LOOP END et les roles `LOOP_START`/`LOOP_START_NEXT` restent prepares par
le mecanisme existant; rien n'est lu au-dela. Reverse et ping-pong restent sur
leur contrat READY/RAM et n'utilisent pas ce fastpath. Les writes
Recorder/Looper/Overdub restent de gros blocs asynchrones et partagent sans
changement l'admission du scheduler; ils ne sont jamais decoupes en writes de
512 octets.

Les compteurs produit exposes sont `fastpath_requests`,
`fastpath_pages_requested`, les comptes 2/3/4 pages,
`fastpath_fallback_noncontiguous`, `fastpath_fallback_cache_reserve`,
`fastpath_cancel_before_submit`, `fastpath_completed` et
`normal_16k_requests`.

## Projection RT1172 (pas une mesure hardware)

Pour 16 voix, 48 kHz, stereo FLOAT32 et pitch x8, le debit utile projete est
`16 * 48000 * 8 * 8 = 49,152 MB/s`. Avec une hypothese RT1172 de 80 MB/s et
une latence fixe conservatrice de 300 us:

| I/O physique | transactions/s | cout fixe | transfert | total SD theorique |
|---|---:|---:|---:|---:|
| 16 KiB | 3000 | 900 ms/s | 614,4 ms/s | 151,4 % |
| 64 KiB coalesce | 750 | 225 ms/s | 614,4 ms/s | 83,9 % |

Ces nombres dimensionnent la future cible et supposent une lecture entierement
sequentielle/coalescee. Ils ne sont ni un benchmark ni un critere d'acceptation
du H743. Sur le build H743 Release introduisant le fastpath, le delta statique
par rapport au build STREAM 16 KiB de reference est: DTCM +192 octets, D1 0,
D2 cacheable +736 octets (D2 non-cacheable, SRAM2 et SRAM3: 0), D3 0 et SDRAM
+96 octets. Aucun payload Page Cache ni buffer de donnees supplementaire n'est
alloue.
