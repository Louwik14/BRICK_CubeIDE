# Polyphonie

## Contrat courant

- Un step stocke au plus quatre PLAY (`V1` a `V4`). Les occurrences de steps
  successifs et les branches des MIDI FX peuvent se chevaucher au-dela de
  quatre notes.
- Les synthetiseurs polyphoniques partagent un pool physique fixe de 16 voix.
  L'allocateur AUDIO choisit une voix libre, puis la plus ancienne en release,
  puis la plus ancienne voix tenue qui peut etre reaffectee.
- MULTI partage huit lecteurs fixes. CONTROL retire l'identite tenue la plus
  ancienne a la saturation; l'allocateur AUDIO reutilise d'abord un lecteur
  libre, puis le plus ancien en release, puis le plus ancien actif.
- `SPREAD` est conserve. Il est calcule sur les voix effectivement possedees,
  sans quota configure par track.
- STREAM/Classic, Drum et ACID restent monophoniques. Patch Preview
  conserve son chemin et ses ressources independants.

Les identifiants de sortie rendent les NOTE_OFF tardifs idempotents. Lors d'un
vol synth inter-track, l'allocateur retire aussi l'ancien identifiant du miroir
AUDIO avant d'armer la nouvelle voix. Aucun malloc, verrou ou attente n'est
utilise dans l'IRQ AUDIO.

## Etat minimal conserve

Chaque voix physique conserve son etat `FREE`, `HELD` ou `RELEASE`, son age et
son identifiant de sortie. Chaque track synth conserve seulement la liste des
slots physiques qu'elle utilise et son `SPREAD`. Un slot primaire par renderer
synth actif reste indispensable dans l'architecture actuelle: il porte la
configuration DSP source que les voix nouvellement attribuees recopient. Ce
slot est une voix utilisable par sa track, mais ne peut pas etre vole par une
autre track sans perdre le preset source. Supprimer cette derniere contrainte
demanderait de separer les configurations DSP des etats de voix, ce qui sort de
la simplification presente.

## Persistence

Le codec courant est B6CP v20. Il n'ecrit plus de nombre de voix par track et
n'accepte aucun format anterieur. `SPREAD` est conserve. La largeur PLAY reste
encodee sur huit elements; seuls V1 a V4 sont appliques au modele vivant.

## Capacites sequenceur

Le dimensionnement reste fonde sur le pire cas GROUP de 35 lanes emettrices,
quatre branches VOICER par occurrence, les generations chevauchees, ROLL,
Groove, les evenements differes, le MIDI live et tous les NOTE_OFF terminaux.
La limite de stockage PLAY ne limite donc ni les MIDI FX ni la polyphonie
physique.

## Cout temps reel

Au repos, le passage de 8 a 4 PLAY divise par deux les boucles PLAY et plusieurs
pools derives. L'allocation synth reste bornee a 16 inspections et MULTI a huit;
le vol ajoute au pire un parcours lineaire du pool, sans allocation memoire.
Le cout DSP des voix actives ne change pas. Aucune mesure de cycles sur cible
n'a ete realisee dans cette passe; les gains CPU annonces sont donc
structurels, pas mesures.

## Mesure Release

Comparaison du meme build Release entre `bd3499183` (avant refonte) et la
refonte complete. Les regions DMA, D3, recorder et BKPSRAM sont inchangees.

| Region | Avant | Apres | Delta | Marge apres |
|---|---:|---:|---:|---:|
| DTCMRAM | 130144 | 130784 | +640 | 288 |
| RAM_D1 | 516000 | 505152 | -10848 | 19136 |
| SRAM1_D2_CACHEABLE | 112000 | 100000 | -12000 | 18784 |
| SRAM2_D2 | 130816 | 120832 | -9984 | 10240 |
| SRAM3_D2 | 29600 | 23104 | -6496 | 9664 |
| RAM_D3 | 54176 | 54176 | 0 | 11360 |
| SDRAM | 13936128 | 13765504 | -170624 | 19395712 |
| ITCMRAM | 61848 | 62384 | +536 | 3152 |

Le bilan RAM des regions ci-dessus est de **209312 octets recuperes nets**.
Les 35520 octets de PLAY sont inclus dans ce chiffre; le reste vient surtout
des pools SEQ redimensionnes, des snapshots/undo et de la suppression des etats
de quota. Le code Flash diminue de 8552 octets. DTCM reste la contrainte la plus
serree; aucun deplacement memoire n'a ete ajoute.
