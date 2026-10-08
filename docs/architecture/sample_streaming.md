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
