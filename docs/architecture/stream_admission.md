# Admission du streaming soutenu

L'admission précède la publication d'un snapshot actif Classic/Multi. Une demande
est identifiée par source, voix et génération ; republier la même voix est
idempotent et une génération périmée ne peut libérer la génération courante.

Le coût soutenu est calculé en octets source par seconde :

```text
48000 x block_align x step_q16 / 65536
```

Le modèle borné additionne les voix, les sources distinctes et une réserve pour
les changements de fichier. Il refuse avant publication tout dépassement de voix,
débit SD exploitable ou latence séquentielle. La suppression d'un snapshot libère
exactement la demande correspondante.

Les valeurs conservatrices actuelles — 6 000 000 octets/s mesurés, 75 %
d'utilisation, 32 768 octets/s par source distincte et 384 frames par lecture —
doivent être remplacées par la campagne matérielle H743. Les compteurs et la
trace stable exposent acceptations, refus, libérations et génération de voix.
