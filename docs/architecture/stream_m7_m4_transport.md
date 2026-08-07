# Frontière transport H743 / H747 / DMA

Sur H743, le M7 détient besoins, scheduler, métadonnées du cache et publication
READY. Le transport monocœur exécute la commande de façon synchrone sous le gate
SD ; le backend contigu est essayé avant le fallback FatFs, avec read-ahead et
décodage sous la même couture.

La commande transportable ne contient aucun pointeur privé. Elle porte seulement
la clé audio, la page, le slot, la génération de page, le registration epoch, la
géométrie/position source, le format et le token. L'adresse physique de destination
est résolue localement depuis le token LOADING immédiatement avant l'I/O.

Sur un futur H747, cette commande et sa complétion peuvent être placées dans des
rings de mémoire partagée. Le M4 exécute l'I/O, mais ne crée pas de besoin, ne
sélectionne pas de voix et ne publie pas READY. Le M7 accepte la complétion après
validation du token, du slot, de la génération et de l'epoch.

Le DMA est un backend sous cette frontière. Il peut remplacer le transfert
synchrone sans modifier besoins, rétention du cache ou scheduling. La cohérence
cache et la barrière finale READY restent dans le chemin de complétion validé.
