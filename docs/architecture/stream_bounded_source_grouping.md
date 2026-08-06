# Regroupement borné par source et taille des lectures

Le chemin FatFs possède désormais un read-ahead borné à une seule sous-lecture.
Il ne modifie jamais l'ordre EDF : seule la page sélectionnée est décodée et
publiée. Si la sélection EDF suivante vise une plage immédiatement contiguë du
même fichier et de la même époque d'enregistrement, les octets déjà lus sont
réutilisés. Une sélection d'une autre source invalide naturellement ce bloc au
prochain remplissage ; aucune affinité fichier ne peut donc retarder une deadline
plus proche.

La taille de sous-lecture est configurable à 4, 8, 16 ou 32 Kio par
`sample_stream_io_set_read_chunk_kib()`. La valeur de build
`BRICK6_STREAM_READ_CHUNK_KIB` fixe le défaut et reste à 4 Kio tant que les
mesures matérielles finales n'ont pas tranché. Une valeur invalide retombe sur
4 Kio. Le read-ahead est limité par la fin des données audio et reste sûr pour
les fichiers fragmentés, FatFs conservant la traduction de chaîne de clusters.

Le backend physiquement certifié contigu conserve sa lecture multibloc directe
d'une page. Il invalide le read-ahead FatFs, puis le décodeur commun convertit
directement vers la page SDRAM finale. Le cache de read-ahead ne contient que des
octets source temporaires : il ne change ni les états de page, ni les owners, ni
les générations, et une page n'est publiée `READY` qu'après décodage complet.

Cette passe fournit le mécanisme de regroupement et la matrice 4/8/16/32 Kio sans
choisir prématurément une taille produit. Les quotas d'admission, le débit garanti
et l'éventuel DMA asynchrone restent conditionnés aux mesures ultérieures.
