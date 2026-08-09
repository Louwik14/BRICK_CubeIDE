# Regroupement borne par source et taille des lectures

Le chemin FatFs possede un read-ahead borne a une seule sous-lecture. Il ne
modifie jamais l'ordre du scheduler : seule la page selectionnee est decodee
et publiee. Si la selection suivante vise une plage immediatement contigue du
meme fichier et de la meme epoque d'enregistrement, les octets deja lus sont
reutilises. Une selection d'une autre source invalide naturellement ce bloc au
prochain remplissage ; aucune affinite fichier ne peut retarder une deadline
plus proche.

La taille produit par defaut est fixee a 16 Kio. Le setter 4/8/16/32 Kio reste
disponible pour les campagnes BENCH et la mesure materielle de l'admission, mais
le fonctionnement produit normal n'en depend pas. Une valeur interne invalide
retombe sur 16 Kio. Le read-ahead est limite par la fin des donnees audio et
reste sur pour les fichiers fragmentes, FatFs conservant la traduction de
chaine de clusters.

Le backend physique commun represente un fichier contigu par un extent et un
fichier fragmente par plusieurs extents. Chaque page est resolue en demandes
`LBA + secteurs + destination`, placees dans une FIFO bornee de quatre entrees.
Le transport SD lance ensuite les lectures SDMMC DMA sans attente active dans
le service streamer. Les callbacks IRQ terminent chaque demande, puis le poll
cooperatif enchaine les extents et ne decode/publie la page `READY` qu'apres
reception complete dans le scratch SDRAM.

La generation de map et l'epoch media sont verifies pendant toute l'operation.
Une map absente ou invalide conserve le chemin FatFs ; une erreur du chemin
physique annule sa FIFO avant le fallback. Le read-ahead FatFs ne contient que
des octets source temporaires et ne change ni les etats de page, ni les besoins,
ni les generations.

Les mesures materielles 4/8/16/32 Kio ont retenu 16 Kio pour le fallback FatFs.
Le backend physique utilise les plus grandes lectures permises par chaque
extent, independamment de ce reglage.
