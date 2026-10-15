# Pool Wavetable persistant en Flash

## Implantation H743

Le pool global occupe les secteurs 4 et 5 de la banque Flash 2, soit
`0x08180000..0x081BFFFF` (256 Kio). Le firmware s'arrête à la fin du secteur 3
et les secteurs 6 et 7 restent réservés à Groove. La région linker
`WAVETABLE_FLASH` empêche toute section firmware d'occuper ce pool.

`wavetable_flash_store` utilise une interface backend minimale : géométrie,
mapping en lecture, programmation par flashword, effacement global et barrière
de publication. Le port H743 est isolé dans `wavetable_flash_backend.c`; un
port RT1172 peut remplacer cette couche sans ajouter de système de fichiers.

## Format et robustesse

Chaque record aligné sur 32 octets contient un en-tête versionné de 256 octets,
huit descripteurs de bande de 32 octets, le payload float32 préparé, un
flashword de commit et un flashword de suppression. L'en-tête porte une
identité 64 bits dérivée du CRC source et du chemin canonique, les longueurs,
la géométrie et les CRC source/base/payload. Le commit, écrit en dernier, porte
le CRC du record complet.

Le scan de boot ne publie que les records dont les bornes, le marqueur de
commit et les CRC sont valides. Un record incomplet ou corrompu n'est jamais
visible par AUDIO. Si deux records valides portent le même chemin, le dernier
commit l'emporte ; un record de longueur vérifiable mais invalidé est sauté,
ce qui permet de retrouver son successeur après une coupure pendant le retrait.
La suppression est logique et programme son flashword dédié ; elle n'efface pas
individuellement un secteur de 128 Kio.

Une table standard de huit frames occupe 65 536 octets de payload et 66 112
octets au total. Trois tables occupent 198 336 octets et laissent 63 808 octets.
La base 1024 points est la première bande du payload et n'est pas dupliquée.

## Import, publication et compatibilité

L'import SD décode et prépare les mipmaps dans le workspace SDRAM de
persistance, sous le propriétaire exclusif `PERSISTENCE_WORKSPACE_WAVETABLE_IMPORT`.
La réservation couvre tous les tours de superloop, de l'ouverture SD jusqu'au
nettoyage terminal. Il ne réserve aucune page du pool Sampler. Le flux audio est explicitement arrêté
pour la programmation ou la suppression Flash, puis redémarré après
invalidation des caches. AUDIO reçoit ensuite des pointeurs directs vers la
Flash : l'IRQ ne fait ni allocation, ni accès SD, ni décompression, ni écriture
Flash.

Les références Project, Resume, Pattern, Working, Patch, Preview et Boot restent
des références canoniques `{kind, path}` compatibles avec les documents
existants. Leur API de résolution consulte exclusivement le catalogue Flash.
Une référence absente reste enregistrée dans la banque logique avec un runtime
invalide, est exposée comme `UNAVAILABLE`, et ne déclenche ni accès SD, ni
préparation, ni programmation Flash. Les autres références disponibles restent
résolues normalement.

La seule API d'écriture exposée est l'import asynchrone appelé par `P2 LOAD` dans
le Browser Wavetable. Une réimportation manuelle ultérieure réutilise le chemin
persistant, rattache le runtime Flash et republie les sélections de piste qui
portaient cette référence. Les API synchrones historiques d'import ont été
retirées afin qu'un chemin de restore ne puisse pas les réutiliser par erreur.

Le Browser applique la même règle avant tout import : un chemin déjà présent
dans le catalogue Flash global est rattaché à la banque logique courante sans
réécriture, même si les trois slots Flash sont occupés. La colonne gauche reste
le catalogue SD et marque `*` les fichiers déjà importés ; la colonne droite ne
montre que les références de la banque courante. `UNLOAD` retire uniquement
cette référence logique. La suppression SD ne touche que la source. La
suppression Flash depuis l'UI est refusée (`FLASH PROTECTED`) tant qu'un index
global des références de tous les projets n'existe pas : vérifier seulement le
projet actif ne suffirait pas à protéger les projets hors ligne.

L'identifiant 64 bits du record reste interne au catalogue Flash. Le format
des documents n'évolue pas : `{kind, path}` fournit déjà une migration propre
des projets historiques et une déduplication inter-projets, tandis qu'un ID
seul ne permettrait pas de reconstruire un record absent depuis la SD.

## Bilan mémoire Release

Le pool Sampler de 5 570 560 octets reste alloué et n'est pas compté comme un
gain. Trois tables standard n'y consomment plus les 294 912 octets de pages
dynamiques qu'elles occupaient auparavant ; ces pages sont simplement rendues
aux samples.

Les valeurs ci-dessous viennent des MAP Release avant/après. D1 et D3 sont les
empreintes occupées du linker (alignements compris). La ligne SDRAM additionne
les sections réellement allouées et exclut le pool de pages Sampler, afin de ne
pas compter le grand trou d'adressage précédant le ring Recorder.

| Mémoire | Avant | Après | Delta hors pool Sampler |
|---|---:|---:|---:|
| DTCM | 125 696 | 125 728 | +32 |
| D1 | 431 456 | 431 520 | +64 |
| D2 | 250 528 | 250 528 | 0 |
| D3 | 54 720 | 54 176 | -544 |
| SDRAM cacheable hors pool | 5 991 424 | 6 018 784 | +27 360 |
| SDRAM non-cacheable | 0 | 0 | 0 |
| **Total hors pool Sampler** | **6 853 824** | **6 880 736** | **+26 912** |

Le delta SDRAM combine +98 304 octets de scratch de préparation, -59 488
octets de catalogue Wavetable et -11 456 octets de registre AUDIO. Le scratch
est statiquement réservé mais temporaire par usage : base 32 768 octets et
mipmaps 65 536 octets sont simultanés pendant l'import, soit un pic réel de
98 304 octets. Les réductions de catalogue et de registre sont permanentes.
Les buffers FFT existants et les buffers SD mutualisés ne sont pas attribués
comme gains, puisqu'ils restent alloués et partagés.

### Delta Release de la passe Browser

Mesure isolée avec les mêmes changements utilisateur présents dans les deux
MAP Release :

| Mémoire | Avant | Après | Delta |
|---|---:|---:|---:|
| DTCM | 125 728 | 125 728 | 0 |
| D1 | 431 488 | 431 488 | 0 |
| D2 | 251 104 | 251 104 | 0 |
| D3 | 54 176 | 54 176 | 0 |
| SDRAM hors pool Sampler | 6 018 784 | 6 018 784 | 0 |
| **Total** | **6 881 280** | **6 881 280** | **0** |

Le Browser n'ajoute donc aucun coût RAM permanent mesurable. Aucun buffer
AUDIO, DMA ou pool Sampler n'est redimensionné.

### Delta Release de la passe import manuel

| Mémoire | Avant | Après | Delta |
|---|---:|---:|---:|
| DTCM | 125 728 | 125 728 | 0 |
| D1 | 431 488 | 431 520 | +32 |
| D2 | 251 104 | 251 104 | 0 |
| D3 | 54 176 | 54 176 | 0 |
| SDRAM hors pool Sampler | 6 018 784 | 5 920 480 | -98 304 |
| **Total** | **6 881 280** | **6 783 008** | **-98 272** |

Le workspace complet mesure 268 544 octets ; l'union de préparation Wavetable
en utilise 98 304 sans agrandir cette allocation permanente. Les anciens
tableaux statiques base (32 768) et mipmap (65 536) sont supprimés. Les 32
octets D1 supplémentaires viennent des pointeurs et de l'alignement ; le gain
réel net est donc 98 272 octets.

Project/Pattern/Patch détiennent déjà ce même workspace pendant leurs opérations
coopératives et ne peuvent plus importer de Wavetable. Un import manuel échoue
explicitement avec `WORKSPACE BUSY` si un propriétaire existe, puis conserve la
réservation pendant toute sa durée asynchrone. Recorder et transport sont
refusés avant acquisition ; Multi import est également exclu. Le streaming
AUDIO peut continuer à être servi par son scheduler et n'accède pas à ce
workspace. Les buffers sont cacheables, uniquement CPU, sans DMA ni maintenance
de cache supplémentaire.

## Flash firmware et coût AUDIO

La somme des sections chargeables du firmware passe de 1 552 876 à 1 554 832
octets, soit +1 956 octets. Le span du binaire Release est 1 567 572 octets sur
la région firmware de 1 572 864 octets, laissant 5 292 octets. Le pool
Wavetable de 262 144 octets est une région séparée et vide dans l'image usine ;
son occupation utile vaut 66 112 octets par table standard, 198 336 octets pour
trois tables.

La passe d'import manuel porte le span Release de 1 567 620 à 1 567 676
octets, soit +56 octets. Les régions Flash Wavetable et Groove ne changent pas.

Le calcul AUDIO, l'interpolation et le choix de mipmap sont inchangés. Les
lectures passent des pages SDRAM à la Flash mappée et cacheable ; il n'y a aucun
traitement par sample ajouté, aucune copie et aucune décompression. Un miss de
cache Flash peut coûter plus qu'un hit SDRAM, mais aucune mesure de cycles IRQ
n'est revendiquée. Les écritures, suppressions et invalidations de cache restent
hors IRQ et imposent un arrêt explicite d'AUDIO.

## Limites connues

Le journal est append-only. Une suppression rend le record invisible mais ne
récupère pas immédiatement son espace physique. Quand la queue vierge ne peut
plus recevoir un record, une maintenance doit arrêter AUDIO, effacer les deux
secteurs et reconstruire le catalogue depuis les sources SD. Aucun compactage
sur place n'est annoncé comme sûr sans secteur de secours A/B. Le backend expose
l'effacement global, mais cette reconstruction n'est pas automatisée dans l'UI
produit actuelle ; l'état plein est retourné explicitement à l'import.

Une coupure pendant l'import peut perdre le nouveau record ou condamner la
queue libre jusqu'à cette reconstruction. Elle ne réécrit pas les records déjà
commités. Une source absente interdit sa reconstruction, mais un record Flash
déjà valide reste lisible sans la carte SD.
