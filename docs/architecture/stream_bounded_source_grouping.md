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

Le backend physiquement certifie contigu conserve sa lecture multibloc directe
d'une page. Il invalide le read-ahead FatFs, puis le decodeur commun convertit
directement vers la page SDRAM finale. Le cache de read-ahead ne contient que
des octets source temporaires : il ne change ni les etats de page, ni les
besoins, ni les generations, et une page n'est publiee `READY` qu'apres
decodage complet.

Les mesures materielles 4/8/16/32 Kio ont retenu 16 Kio pour le produit. Le DMA
asynchrone n'est pas active.
