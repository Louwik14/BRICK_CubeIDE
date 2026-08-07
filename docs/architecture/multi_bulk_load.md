# Chargement massif des instruments Multi

Le bulk Multi est une opération d'amorçage distincte du registre mobile. Avant
PLAY, il calcule une fois l'union des pré-socles start et loop forward, fusionne
les intervalles adjacents, réserve et épingle leurs pages physiques.

Le client `MULTI_BULK` prend l'exclusivité SD. Chaque passage traite un lot borné
à 64 Kio et chaque page utilise le transport commun : backend contigu ou fallback
FatFs, read-ahead, décodeur PCM et publication tokenisée READY/FAILED. Le loader
ne possède ni fichier FatFs ni décodeur parallèle.

La progression utilise `pages_remaining` et `samples_remaining`. L'annulation ou
l'erreur libère les lecteurs, le gate et l'instrument partiellement construit sans
toucher les besoins des voix actives. Le diagnostic expose durée, ouvertures,
seeks, lectures, volumes, transactions SD, cycles de décodage et contrôles de pages
économisés.
