# BRICK — Référence historique SD / Streamer avant futur PCB HS

**Date de référence : 19 septembre 2026**  
**Plateforme mesurée : STM32H743, PCB actuel BRICK**  
**But :** conserver une baseline fiable de la chaîne SD/Streamer actuelle afin de pouvoir comparer proprement le futur PCB BRICK basé sur STM32H757 avec SD UHS-I / 1,8 V / fréquence plus élevée.

---

## 1. Résumé exécutif

La chaîne SD actuelle a été progressivement amenée de **24 MHz** à **50 MHz en 4 bits**, puis le chemin streamer a été optimisé pour réduire le coût CPU et la latence de refill.

État de référence actuel :

- SDMMC : **50 MHz, bus 4 bits, mode High Speed / SDR25**
- débit brut théorique : **25 MB/s**
- débit transactionnel observé sur gros reads streamer : environ **19 MB/s**
- pages streamer : **8192 frames**
- page runtime : **64 KiB float32 stéréo**
- cache : **376 pages** dans la configuration historique figée
- cache total : **23,5 MiB**
- PCM24 décodé directement dans la page finale
- plus de gros buffer float intermédiaire ni memcpy pleine page
- transport SD async LL non bloquant pour le streamer
- FatFs reste sur le chemin HAL classique
- N+1 chaining implémenté
- IRQ SDMMC très courte : environ **1–4 µs CPU sur M7 480 MHz**
- limite actuelle soutenue proche de **8 voix à +3 octaves (8×)** en PCM24 stéréo 48 kHz

Cette baseline doit être conservée pour mesurer objectivement le gain du futur PCB HS.

---

# 2. Configuration SD historique

## 2.1 Ancienne configuration

Configuration d'origine :

- SDMMC : **24 MHz**
- bus : **4 bits**
- débit brut théorique :

```text
24 MHz × 4 bits / 8 = 12 MB/s
```

Débit physique mesuré à l'époque :

```text
≈ 9,54 MB/s
```

---

## 2.2 Passage à 40 MHz puis 50 MHz

Première étape :

```text
40 MHz
```

Puis configuration finale actuelle :

```text
50 MHz
4-bit
High Speed / SDR25
```

Débit brut théorique :

```text
50 MHz × 4 bits / 8 = 25 MB/s
```

Commit associé au passage 50 MHz :

```text
4d30a40c7 perf(sd): run SDMMC at 50 MHz
```

---

# 3. Configuration horloges actuelle à 50 MHz

Référence PLL2 retenue :

```text
HSE = 12 MHz

PLL2M = 3
PLL2N = 150
PLL2P = 20
PLL2Q = 1
PLL2R = 3

VCO = 600 MHz
```

Sorties utilisées :

```text
PLL2P = 30 MHz
PLL2R = 200 MHz
```

SDMMC :

```text
PLL2R = 200 MHz
ClockDiv = 2
→ SDCLK = 50 MHz
```

FMC / SDRAM :

```text
FMC remuxé vers HCLK3 = 240 MHz
SDClockPeriod = 2
→ SDRAM reste à 120 MHz
```

Audio :

```text
PLL3 inchangée
```

L'`.ioc` a été mis à jour pour éviter qu'une régénération CubeMX ne casse cette configuration.

---

# 4. Diagnostic runtime High-Speed

Structure utilisée :

```text
g_bsp_sd_high_speed_diag
```

Valeur attendue historique :

```text
[1, 1, 2, 50000000, 4]
```

Interprétation générale :

- carte initialisée
- High Speed actif
- divider attendu
- SDCLK = 50 MHz
- bus width = 4

**Important :** les adresses changent selon les builds/LTO. Toujours faire :

```gdb
info address g_bsp_sd_high_speed_diag
```

---

# 5. Architecture streamer actuelle

Chaîne logique simplifiée :

```text
SD card
→ SDMMC / IDMA
→ raw transfer
→ decode PCM
→ page float32 stéréo finale
→ READY
→ AUDIO consomme uniquement les pages READY
```

Principe important :

- l'AUDIO ne fait pas de lecture filesystem
- l'AUDIO ne décode pas le PCM
- l'AUDIO consomme uniquement du cache prêt
- STORAGE/Streamer réalise les refills hors IRQ audio

---

# 6. Pages streamer

## 6.1 Ancienne taille

Historique :

```text
4096 frames/page
32 KiB runtime float stéréo
752 pages
≈ 23,5 MiB total
```

---

## 6.2 Taille finale retenue

Configuration validée :

```text
8192 frames/page
64 KiB runtime float stéréo
376 pages
≈ 23,5 MiB total
```

Commit :

```text
0f9cd42d7 perf(streamer): use 64 KiB cache pages
```

Le passage à 8192 frames n'augmente donc pas le budget cache total : il divise approximativement le nombre de pages par deux.

---

# 7. Budgets cache historiques

Configuration figée de référence :

```text
SAMPLE STREAM CACHE
376 pages × 64 KiB
= 24 641 536 octets
≈ 23,5 MiB
```

Budgets historiques :

```text
Multi : 304 pages
START : 304 mono-equivalent slots
```

Cette valeur **376 pages** doit être considérée comme la baseline historique du streamer avant les travaux récents du séquenceur.

---

# 8. Optimisation decode PCM direct

Ancien chemin :

```text
SD raw scratch
→ decode vers buffer float intermédiaire
→ memcpy vers cache final
→ READY
```

Mesures typiques sur pages 4096 :

```text
PCM24 decode ≈ 1,385 ms/page
memcpy       ≈ 0,643–0,70 ms/page
```

Le memcpy était donc un coût important.

---

## 8.1 Nouveau chemin

Nouveau chemin :

```text
SD raw scratch
→ decode PCM16/24/32 directement dans la page LOADING finale
→ READY
```

Le buffer float intermédiaire a été supprimé.

Gain mémoire :

```text
≈ 64 KiB SDRAM récupérés
```

Le memcpy pleine page disparaît :

```text
copy metric = 0
```

Mesure PCM24 sur page 4096 après optimisation :

```text
decode ≈ 0,594–0,616 ms/page
copy   = 0
```

Sur page 8192 :

```text
decode ≈ 1,24 ms/page
copy   = 0
```

---

# 9. Mesures refill 4096 frames

Après direct decode, avant passage final à 8192 :

```text
DMA transaction physique  ≈ 1,60 ms/page
complete → worker         ≈ 0,49–0,63 ms
decode PCM24              ≈ 0,59–0,62 ms
copy                      = 0
refill total              ≈ 4,0–4,5 ms
```

Avant N+1 :

```text
pending → next DMA ≈ 4,46 ms
```

Après N+1, sur un run faible pression :

```text
pending → next DMA ≈ 3,68 ms
```

---

# 10. Mesures finales 8192 frames

Configuration de référence actuelle :

```text
8192 frames
64 KiB runtime/page
376 pages
```

Mesures typiques :

```text
DMA physique              ≈ 2,58 ms/page
decode PCM24              ≈ 1,24 ms/page
refill total              ≈ 5,31 ms/page
pending → next DMA        ≈ 0,61 ms sur run mesuré
copy                      = 0
```

N+1 observé :

```text
armed       = 6
chained     = 6
invalidated = 0
```

---

# 11. Gain du passage 4096 → 8192

Pour la même quantité audio de 8192 frames :

Ancien 4096 :

```text
2 × ~4,04 ms
≈ 8,08 ms refill
```

Nouveau 8192 :

```text
≈ 5,31 ms refill
```

Gain refill approximatif :

```text
≈ 34 %
```

Temps SD physique :

```text
ancien :
2 × 1,60 ms = 3,20 ms

nouveau :
≈ 2,58 ms
```

Gain transactionnel SD :

```text
≈ 19 %
```

Le coût decode total reste logiquement proche :

```text
2 × ~0,62 ms
≈ 1,24 ms
```

Le gain vient principalement de la réduction du nombre de transactions, d'orchestrations et d'IRQ.

---

# 12. Transport SD LL async

Un transport SDMMC bas niveau non bloquant a été introduit pour le chemin streamer.

Machine logique :

```text
IDLE
→ CMD_START
→ DATA_ACTIVE
→ WAIT_CMD12
→ COMPLETE / ERROR
```

Commandes concernées :

```text
CMD17
CMD18
CMD12
```

Le fast path ne fait plus de CMD13 bloquant.

---

# 13. Séparation FatFs / streamer

Une première tentative avait redirigé le chemin partagé vers le LL async.

Cela avait cassé FatFs :

```text
FatFs attendait BSP_SD_ReadCpltCallback()
LL ne le fournissait pas
→ timeout
→ f_mount fail
→ SD unavailable
```

Architecture finale :

```text
FatFs / Browser
→ HAL classique

Streamer / block-device async
→ transport LL
```

Cette séparation est importante et doit être conservée lors du futur port H757.

---

# 14. IRQ SDMMC — mesures CPU réelles

L'ancien diagnostic mesurait du wall-time DWT et pouvait inclure les préemptions AUDIO/SEQ.

Une instrumentation CPU réelle a ensuite été ajoutée :

```text
g_sdmmc_async_irq_diag
```

Elle sépare :

- CPU réellement exécuté par IRQ SDMMC
- wall-time
- préemption

Exemple mesure PASS 1 :

```text
calls     = 2016

CPU avg   ≈ 512 cycles
          ≈ 1,07 µs @480 MHz

CPU max   ≈ 916 cycles
          ≈ 1,91 µs @480 MHz
```

PASS 2 / N+1 :

```text
CPU avg   ≈ 549 cycles
          ≈ 1,14 µs @480 MHz

CPU max   ≈ 1688 cycles
          ≈ 3,52 µs @480 MHz
```

Une autre estimation sur 8192 donnait un max autour de :

```text
≈ 3,9 µs
```

Conclusion historique :

```text
IRQ SDMMC = très petite
```

Le coût important du streamer n'est pas l'IRQ, mais :

```text
decode PCM
+
orchestration / refill hors IRQ
```

---

# 15. N+1 chaining

PASS 2 SD a ajouté un descriptor N+1.

Principe :

```text
pendant transfert N :
superloop prépare N+1

fin N + CMD12 :
IRQ publie N
puis lance N+1 directement si READY
```

Contraintes :

```text
max chain = 2 transferts même owner
puis retour scheduler
```

Pas de :

- cache work lourd dans IRQ
- polling
- filesystem
- release gate dans IRQ

Le gate reste libéré côté superloop.

Mesure historique :

```text
armed       3
chained     3
invalidated 0
```

Puis sur 8192 :

```text
armed       6
chained     6
invalidated 0
```

---

# 16. Instrumentation latence SD

Structure historique :

```text
g_sd_stream_latency_diag
```

Layout connu :

```text
+0x08 DMA START → COMPLETE
+0x18 COMPLETE → WORKER
+0x28 WORKER → NEXT DMA
+0x38 REFILL TOTAL begin_loading → READY
+0x48 sectors_total uint64
+0x50 bytes_total uint64
+0x58 PENDING READY → NEXT DMA
+0x68 completions with other refill pending
+0x6c completions without pending
```

Chaque metric :

```text
calls
total_lo
total_hi
max
```

Activation historique :

```gdb
write state +0 = 1
```

Le firmware remet ensuite l'état à `2`.

Arrêt :

```gdb
write state +0 = 0
```

Dump typique :

```gdb
x/28wx ADDRESS
```

Toujours rechercher l'adresse :

```gdb
info address g_sd_stream_latency_diag
```

---

# 17. Instrumentation streamer

Structure historique :

```text
g_sample_stream_metrics
```

Ancien layout :

```text
state       +0x00
service     +0x08
manager     +0x18
RR          +0x28
scheduler   +0x38
worker      +0x48
poll        +0x58
span/LBA    +0x68
decode      +0x78
copy        +0x88
publish     +0x98
refill      +0xA8
```

Chaque métrique :

```text
calls
total_lo
total_hi
max
```

Dump historique :

```gdb
x/46wx ADDRESS
```

Avec LTO :

```gdb
info address g_sample_stream_metrics
```

---

# 18. Débit pratique actuel

À 50 MHz / 4-bit :

```text
théorique brut = 25 MB/s
```

Avec les transactions 8192 observées, le débit pratique utile se situe approximativement autour de :

```text
≈ 19 MB/s
```

Soit une efficacité approximative :

```text
≈ 76 %
```

Cette valeur est particulièrement utile comme baseline future.

---

# 19. Besoin PCM24 par voix

Format runtime source typique :

```text
PCM24
stéréo
48 kHz
```

Débit à pitch normal :

```text
48 000 × 2 × 3
= 288 000 octets/s
= 0,288 MB/s
```

À +3 octaves :

```text
8× playback rate
```

Donc :

```text
0,288 × 8
= 2,304 MB/s / voix
```

---

# 20. Limite pratique actuelle +3 octaves

Pour 8 voix :

```text
8 × 2,304
= 18,432 MB/s
```

Cette charge est très proche du débit transactionnel pratique mesuré :

```text
≈ 19 MB/s
```

Conclusion historique :

```text
8 voix @ +3 octaves
≈ limite soutenue actuelle
```

Il reste très peu de marge.

---

# 21. Pages/s à +3 octaves

Une page 8192 frames à 8× couvre :

```text
8192 / 48000 / 8
≈ 21,33 ms audio
```

Une voix demande donc :

```text
≈ 46,875 pages/s
```

8 voix :

```text
≈ 375 pages/s
```

16 voix :

```text
≈ 750 pages/s
```

Avec une transaction physique d'environ 2,58 ms :

```text
8 voix :
375 × 2,58 ms
≈ 968 ms de SD physique / seconde
```

Cela explique pourquoi 8 voix @8× sont déjà proches de la saturation actuelle.

---

# 22. Coût CPU decode PCM24

Page 8192 :

```text
PCM24 decode ≈ 1,24 ms/page
```

À 8 voix @8× :

```text
375 pages/s × 1,24 ms
≈ 465 ms CPU/s
≈ 46,5 % d'un M7 480 MHz
```

À 16 voix @8× :

```text
750 × 1,24 ms
≈ 930 ms CPU/s
≈ 93 % d'un M7 480 MHz
```

Donc, sur le futur dual-core, le débit SD n'est pas le seul sujet :

```text
le decode PCM24 devient un gros budget CPU
```

---

# 23. Float32 natif — analyse historique

Avec pages float32 stéréo 8192 :

```text
64 KiB source/page
```

PCM24 stéréo équivalent :

```text
≈ 48 KiB/page
```

Donc float32 augmente le trafic SD d'environ :

```text
+33 %
```

Estimation actuelle à 50 MHz :

PCM24 :

```text
SD       ≈ 2,58 ms
decode   ≈ 1,24 ms
total    ≈ 5,31 ms
```

Float32 natif estimé :

```text
SD       ≈ 3,44 ms
decode   = 0
total    ≈ 4,93 ms
```

Gain global estimé :

```text
≈ 0,38 ms/page
≈ 7 %
```

Conclusion actuelle :

```text
passer tout le stockage en float32 ne vaut pas le coût
sur le PCB actuel
```

Mais cette conclusion devra être réévaluée avec la future SD beaucoup plus rapide.

---

# 24. Futur STM32H757 / SD HS

MCU visé :

```text
STM32H757XIH6
```

Cible pratique discutée :

```text
UHS-I
1,8 V
4 bits
SDCLK autour de 125 MHz
```

Débit brut à 125 MHz :

```text
125 MHz × 4 / 8
= 62,5 MB/s
```

Rapport brut vs PCB actuel 50 MHz :

```text
62,5 / 25
= 2,5×
```

En conservant grossièrement l'efficacité actuelle ~76 % :

```text
62,5 × 0,76
≈ 47,5 MB/s utile
```

Ce chiffre est une estimation, PAS une mesure.

---

# 25. Projection future PCM24

16 voix PCM24 @ +3 octaves :

```text
16 × 2,304
= 36,864 MB/s
```

Sur une future chaîne autour de :

```text
≈ 47,5 MB/s utile estimé
```

cela donnerait une marge plausible.

Donc côté SD pur :

```text
16 voix @8× PCM24 semblent réalistes
```

Mais le decode CPU reste le problème principal si exécuté intégralement sur le M4/M7.

---

# 26. Projection future float32

Float32 stéréo 48 kHz :

```text
48 000 × 2 × 4
= 384 000 octets/s
```

À 8× :

```text
3,072 MB/s / voix
```

16 voix :

```text
49,152 MB/s
```

Donc avec une estimation utile autour de 47,5 MB/s :

```text
16 voix float32 @8×
seraient trop proches / potentiellement au-dessus de la limite
```

Le futur arbitrage probable restera donc :

```text
PCM24 :
moins de bande passante
plus de CPU decode

float32 :
plus de bande passante
quasi zéro decode
```

À rebenchmarker réellement sur le nouveau PCB.

---

# 27. Répartition future H757 — point d'attention

Architecture envisagée :

```text
M7 :
AUDIO / DSP

M4 :
SEQ
SD / streamer
CONTROL / services
```

Le SDMMC IRQ lui-même est suffisamment petit pour être placé presque n'importe où :

```text
~1–4 µs CPU
```

Le vrai problème est le decode PCM.

Si on transpose naïvement le decode M7 480 → M4 240 :

```text
1,24 ms/page
→ environ 2,48 ms/page
```

À 8 voix @8× :

```text
375 × 2,48 ms
≈ 930 ms CPU/s
≈ 93 % M4
```

À 16 voix :

```text
≈ 186 % M4
```

Donc le futur PCB HS devra probablement aussi traiter la question decode, pas seulement augmenter SDCLK.

---

# 28. Ce qu'il faudra mesurer sur le futur PCB

Lors du passage H757 / SD HS, refaire exactement ces mesures :

## Horloge / mode

```text
SDCLK réel
bus width
1,8 V actif
mode UHS actif
tuning validé
```

## Débit physique

Sur pages comparables :

```text
DMA START → COMPLETE
bytes_total
sectors_total
MB/s pratique
```

## Latence orchestration

```text
COMPLETE → WORKER
WORKER → NEXT DMA
PENDING READY → NEXT DMA
REFILL TOTAL
```

## CPU IRQ

Mesurer comme aujourd'hui :

```text
CPU avg IRQ
CPU max IRQ
wall
preemption
```

## Decode

```text
PCM16
PCM24
PCM32
```

Sur page 8192.

## Charge produit

Rejouer au minimum :

```text
1 voix @8×
8 voix @8×
16 voix @8×
```

Mesurer :

```text
underruns
pages/s
MB/s
CPU streamer
cache pressure
```

---

# 29. Tableau baseline à conserver

| Mesure | PCB actuel H743 |
|---|---:|
| SDCLK | 50 MHz |
| Bus | 4-bit |
| Mode | HS / SDR25 |
| Débit brut | 25 MB/s |
| Débit pratique approx. | ~19 MB/s |
| Efficacité approx. | ~76 % |
| Page | 8192 frames |
| Runtime page | 64 KiB |
| Cache historique | 376 pages |
| Cache total | 23,5 MiB |
| DMA physique/page | ~2,58 ms |
| PCM24 decode/page | ~1,24 ms |
| Copy/page | 0 |
| Refill total/page | ~5,31 ms |
| pending→next DMA | ~0,61 ms sur run mesuré |
| IRQ SD avg | ~1,1 µs CPU |
| IRQ SD max | ~3,5–3,9 µs CPU |
| N+1 | validé |
| PCM24 1 voix @8× | 2,304 MB/s |
| PCM24 8 voix @8× | 18,432 MB/s |
| Limite actuelle estimée | ~8 voix @8× |
| PCM24 decode CPU 8 voix @8× | ~46,5 % M7 |
| PCM24 decode CPU 16 voix @8× | ~93 % M7 |

---

# 30. Références commits

Principaux repères historiques :

```text
4d30a40c7
perf(sd): run SDMMC at 50 MHz
```

```text
0f9cd42d7
perf(streamer): use 64 KiB cache pages
```

Autres travaux importants à retrouver dans Git autour de :

```text
direct PCM decode
SD LL async
SD N+1 chaining
SD IRQ CPU instrumentation
stream latency instrumentation
```

Les hashes exacts de ces passes ne sont pas tous conservés dans ce document ; utiliser `git log` si besoin.

---

# 31. Conclusions historiques

Le PCB actuel H743 / 50 MHz a atteint un état relativement optimisé :

1. Le bus SD est proche de sa limite pratique actuelle sur les charges extrêmes.
2. Le passage 4096 → 8192 a réduit sensiblement les frais de transaction.
3. Le decode direct a supprimé un gros memcpy et divisé fortement le coût decode.
4. Le LL async a rendu l'IRQ SD très courte.
5. N+1 a réduit les trous entre transactions.
6. L'IRQ SD elle-même n'est PAS un problème CPU.
7. Le principal compromis restant est :
   - bande passante SD
   - coût decode PCM24
   - orchestration hors IRQ
8. Le futur PCB H757 / SD HS devrait améliorer fortement la bande passante SD.
9. Le futur benchmark devra surtout vérifier si le decode PCM devient le nouveau goulot dominant.
10. Cette baseline doit être gardée intacte pour éviter de perdre le point de comparaison lors du changement de PCB.

---

# 32. Règle pour comparaison future

Lors du benchmark du futur PCB, ne comparer que :

```text
même format audio
même taille page
même nombre de voix
même pitch ratio
même instrumentation
même périmètre de mesure
```

Ne pas comparer directement :

```text
débit théorique futur
vs
débit pratique actuel
```

Comparer :

```text
débit pratique actuel
vs
débit pratique futur
```

et :

```text
CPU/page actuel
vs
CPU/page futur
```

C'est cette comparaison qui permettra de quantifier réellement le gain du nouveau PCB.
