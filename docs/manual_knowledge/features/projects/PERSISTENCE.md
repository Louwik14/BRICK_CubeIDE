# PERSIST — Pattern, Project et Patch

## Statut

CONFIRME — validation SD/coupure alimentation requise.

Le codec courant `B6CP` version 18 (`PERSIST_CODEC_VERSION`) encode explicitement cles, sections, tailles et CRC. Pattern, Project et Patch n'utilisent pas de dump de structures. Les noms ont 32 caracteres visibles max.

Pattern : 256 slots par projet, un seul live. Resolution Recall = override Working, fichier Saved, default. Le Pattern quitte est capture avant commit cible puis reconcilie dans `BRICK/WORKING/PATTERNS`. Recall prepare CONTROL/SEQ/AUDIO avant boundary. Pattern Save ne reecrit que son document.

Project : 16 slots. `PROJECT.B6C` porte metadata, manifeste assets et 14 Scenes ; les Patterns sont fichiers separes. Save global utilise journal `BRICK/TRANSACTIONS/PROJECT`; Save As publie un dossier candidat sans reload musical ; New emploie Project Load/Blank. Load valide avant `T_FORWARD`, puis devient forward-only. Boot restaure le dernier projet via `0:/BRICK/BOOT.B6C`, sinon defaults.

Patch : une entite, parametres, jusqu'a deux references assets et etat FM type. Save choisit premier slot libre pour NEW ; overwrite explicite sur fichier. Rename relit le DTO et remplace uniquement metadata. Delete supprime aussi un document corrompu. Preview est un runtime separe et momentane.

Succes Save signifie TMP ecrit, sync, close et commit avec BAK ; les mailboxes de resultat sont consommees une fois. Restore global invalide Undo/Redo et gestes UI.

Sources : `Src/Storage/{persistent_control_codec,project_product,pattern_*,patch_product,persistence_workspace}.c`, `Inc/Storage/persistent_control_model.h`, `docs/architecture/z6_state_persistence_patterns_projects.md`.
