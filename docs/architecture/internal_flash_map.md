# Carte Flash interne STM32H743

| Region | Adresse | Taille | Contrat |
|---|---:|---:|---|
| `FLASH` | `0x08000000-0x081BFFFF` | 1792 KiB | firmware et images RAM |
| `GROOVE_FLASH` | `0x081C0000-0x081FFFFF` | 256 KiB | cache compile BGRB v1 des grooves SD |

`GROOVE_FLASH` couvre les secteurs B2 S6 et S7 et ne contient aucune section
firmware. `__groove_flash_start__` et `__groove_flash_end__` formalisent ses
bornes. La banque BGRB est un cache reconstructible depuis `0:/Grooves/` : une
reconstruction efface les deux secteurs, programme records puis catalogue, et
programme le flashword de commit en dernier. Une banque interrompue reste donc
invalide et sera reconstruite au boot suivant.

Le format reserve 256 octets de header, 127 entrees de catalogue de 80 octets,
puis des records alignes sur 32 octets. Un record maximal contient un header de
32 octets et 128 points de 12 octets, soit 1568 octets. La borne complete est
de 209568 octets et laisse 52576 octets libres dans la region. Les donnees sont
lues directement par adresse memory-mapped ; aucun template permanent n'est
copie en SDRAM. Les CRC de records sont controles a la reconstruction et a la
demande, pas pendant le scan normal du boot.

Le catalogue publie des indices runtime `1..127` (`0` reste OFF). Les tracks
ne persistent pas ces indices : elles conservent le nom du groove et leurs
propres valeurs Base/Quantize/Timing/Random/Velocity. Une selection manuelle
applique les defaults du record, tandis qu'un restore resout seulement le nom
et conserve exactement les valeurs de track sauvegardees. Un nom non resolu
reste conserve avec l'etat MISSING et un template neutre.

BOOT context, calibration Hall et capsules Crash sont des fichiers SD et ne
possedent plus de region Flash interne.
