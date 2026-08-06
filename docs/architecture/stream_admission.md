# Admission du streaming soutenu

L'admission est effectuée au moment où une voix réserve sa première fenêtre,
avant toute nouvelle allocation ou requête SD. Une voix est identifiée par son
owner et sa génération ; les appels répétés pour la même voix sont idempotents.
Les owners de loop ne sont pas comptés une seconde fois, car ils partagent le
débit de lecture de la voix correspondante.

Le coût soutenu est calculé en octets source par seconde :

```text
48000 × block_align × step_q16 / 65536
```

Le modèle additionne les voix Classic et Multi, tient compte du format réel via
`block_align`, du ratio de lecture et du nombre de sources distinctes. Chaque
fichier distinct ajoute une réserve configurable représentant les seeks et les
changements de chaîne FAT. Une demande est refusée proprement si elle dépasse la
limite de huit voix ou la fraction exploitable du débit SD mesuré.

Les valeurs conservatrices de migration sont 6 000 000 octets/s mesurés, 75 %
d'utilisation maximale, 32 768 octets/s de réserve par fichier distinct et 384
frames audio de latence maximale par lecture. L'admission vérifie aussi que la
latence séquentielle projetée des fichiers distincts tient dans l'horizon. Elles
doivent être remplacées par les résultats matériels de la passe finale. Les
statistiques exposent le débit admis, la capacité, les voix, les fichiers, le
dernier motif et le nombre de refus.

La libération par owner/génération retire immédiatement la demande du modèle.
Une erreur pendant la réservation suit le rollback existant et libère aussi
l'admission. Aucun pool, pending, lock, présocle ou chemin IRQ n'est agrandi ou
modifié.
