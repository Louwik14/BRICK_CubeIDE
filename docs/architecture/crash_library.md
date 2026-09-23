# Capsules crash sur SD

La Crash Library est une persistence diagnostique best-effort sur SD. Elle ne
possede plus de secteur de Flash interne, de generations A/B, de rotation ni de
commande GDB d'effacement.

Les capsules sont conservees sous `0:/BRICK/CRASH/` avec un nom de sequence
hexadecimal monotone compatible 8.3 (`00000001.B6C`, etc.). Le boot normal cree
le dossier et determine la prochaine sequence. Il n'existe ni quota ni garbage
collection automatique.

Chaque fichier contient un header versionne, le fatal record, les registres CPU
et fault, l'etat FIFO, les compteurs, l'extension diagnostique et un CRC32. Il
est cree avec `FA_CREATE_NEW`, puis ecrit, synchronise et ferme sans ecraser une
capsule existante.

L'ecriture fatale n'est tentee qu'en contexte thread, avec filesystem deja
monte, SD prete et gate Storage immediatement disponible. SysTick et SDMMC
restent actifs pendant cette tentative. Un fatal en ISR, pendant une transaction
SD ou sans media ne produit volontairement aucune capsule. Il n'existe aucun
fallback Flash, BKPSRAM, journal ou retry differe.

Les capsules se copient et se decodent directement depuis la SD sur PC. Les
anciennes procedures GDB liees aux secteurs Crash Flash ne s'appliquent plus.
