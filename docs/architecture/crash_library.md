# Bibliothèque persistante de capsules crash

## ABI fixe

Le STM32H743 efface par secteurs de 128 KiB et programme par flashwords de 32
octets. Les secteurs 4 et 5 de la bank 2 sont exclusivement réservés :

```
CRASH_LIBRARY_BASE     0x08180000 (génération A, 128 KiB)
CRASH_LIBRARY_SECTOR_B 0x081A0000 (génération B, 128 KiB)
CRASH_SLOT_SIZE        0x1000 (4096 octets)
CRASH_SLOT_COUNT       20
slots                  sector_base + 0x1000 + index * 0x1000
```

Le header de génération est le premier flashword : magic `CSEC`, version,
generation, CRC puis réservés. Chaque capsule est autonome : header 32 octets,
payload, padding, puis flashword de commit à l'offset `0xFE0`. Le header capsule
contient aux offsets 0x00..0x1C : magic `CRAS`, version, taille, séquence, CRC,
taille payload, flags, réservé. Le commit contient `COMT`, séquence, CRC,
complément du CRC. Le CRC couvre `[0x000,0xFE0[` avec le champ CRC mis à zéro.

Payload à `+0x20` : firmware_id `0x020`, message `0x040`, file `0x0A0`,
function `0x100`; line/code/entity/context/requested/capacity `0x140..0x154`;
PC/LR/SP/xPSR/MSP/PSP/CONTROL/IPSR/PRIMASK/BASEPRI/FAULTMASK/EXC_RETURN
`0x158..0x184`; CFSR/HFSR/DFSR/AFSR/MMFAR/BFAR `0x188..0x19C`; reset RSR,
tick HAL, TIM5 CNT et FIFO head/tail/count/overflow/invariant `0x1A0..0x1BC`;
8 commandes FIFO de 16 octets à `0x1C0`; extra_count `0x240`, puis 128 mots
d'extension à `0x244`. L'ABI de capsule vaut toujours 4096 octets.

## Rotation et sûreté

Au boot, les deux secteurs sont scannés et les 20 plus grandes séquences sont
indexées. Après une capsule pleine, le boot suivant efface le secteur de
secours, y écrit une génération, copie les 19 capsules les plus récentes, puis
réutilise le vingtième slot au fatal suivant. La source n'est effacée qu'à une
rotation ultérieure. Une coupure laisse donc toujours la génération source;
le flashword de commit écrit en dernier exclut toute capsule partielle. Aucun
erase n'a lieu sur le chemin fatal. Double fatal/writer actif et absence de slot
retournent sans récursion.

## Vue et commandes GDB sans symboles

Une vue reconstruite au boot réside à `0x38800000`: magic/version/count/latest,
oldest/latest-address/next-address/generation (8 mots), puis 20 descripteurs de
4 mots `{sequence,address,crc,status}`. Le dernier descripteur valide est le
plus récent.

```gdb
shell cls
x/88wx 0x38800000
x/8wx 0x08180000
x/8wx 0x081A0000
x/1024wx 0x08181000
# lire le dernier: relever newest_address à 0x38800014 puis x/1024wx ADRESSE
set {unsigned int}0x388001fc = 0x434c5243
monitor reset
```

Le clear est détecté au boot et efface proprement les deux secteurs. Coûts :
256 KiB Flash réservée, 4096 octets de buffer fatal et 512 octets BKPSRAM;
aucun travail périodique. Un fatal programme 128 flashwords (typiquement
quelques millisecondes, dépendant tension/horloge HAL). Une rotation fait un
erase secteur et recopie 19 capsules au boot, jamais pendant le fatal.
