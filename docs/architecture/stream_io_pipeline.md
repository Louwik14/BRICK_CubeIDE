# Pipeline I/O des pages streamées

Le manager sélectionne un besoin persistant d'une voix bornée, réserve sa page
physique puis obtient un token lors de `RESERVED -> LOADING`. Il construit une
commande transportable sans pointeur. L'exécuteur résout localement la destination
SDRAM depuis le token, tente le backend contigu puis le fallback FatFs, applique
le read-ahead et délègue la conversion PCM au décodeur commun.

Le publisher est l'unique point `LOADING -> READY/FAILED`. La clé, la page, le
slot, la génération de page et le registration epoch doivent encore correspondre.
Une annulation ou une complétion périmée ne peut donc pas publier READY, et une
page LOADING ne peut pas être recyclée avant cette complétion.

Le flux H743 est synchrone et exécuté par le tasklet stockage, jamais par l'IRQ
audio. Le bulk Multi emprunte exactement transport, backend, décodeur et publisher
sous exclusivité SD. Un futur backend DMA ou un exécuteur M4 remplace seulement
l'exécution sous cette couture ; besoins, cache et scheduler restent inchangés.
