# Project CONTROL — passe 2

Le document Project canonique porte des références d'assets logiques (`id`, `kind`, `path`) et un catalogue de Patterns streamé. Les valeurs `SAMPLE`, leurs p-locks, `OSC1_TABLE` et `OSC2_TABLE` sont des sélecteurs de slots logiques; les indices backend (`stream`, RAM, Multi, wavetable) et les slots du pool global sont des détails runtime et ne font pas partie du format.

Le chargement Project effectue désormais deux lectures bornées avec le même workspace: contrôle structurel/CRC, puis décodage et validation complète des sections, assets et records Pattern sans mutation. Une seconde lecture applique ensuite les autorités Project et consomme chaque Pattern séparément. Aucun staging des 256 Patterns ni rollback Project n'est introduit.

La reconstruction physique des banks et le branchement du provider de la banque Pattern SD restent à fermer avant la bascule disque. `ProjectSaveV1` et `PatternSaveV1` restent donc les conteneurs disque historiques; Patch n'est pas migré dans cette passe.
