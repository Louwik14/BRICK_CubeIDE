# Persistence debug block v3

`g_persist_dbg` est un bloc contigu de 156 octets, soit exactement 39 words
little-endian de 32 bits. Il est `volatile`, place dans `.data.persist_debug`,
marque `used` et reference par l'instrumentation active. Le symbole et sa
taille sont donc conserves par le build Release avec LTO.

Les identites Pattern sont encodees `bank << 16 | slot`. Une valeur candidate
nulle n'est significative que si `candidate_phase == EMPTY`; le Pattern 0/0
courant ou demande reste donc representable sans ambiguite.

| Offset | Word | Signification | Valeurs |
|---:|---|---|---|
| `0x00` | `magic` | Signature du bloc | `0x50444247` (`PDBG`) |
| `0x04` | `version` | Version du layout | `3` |
| `0x08` | `word_count` | Taille auto-decrite | `39` |
| `0x0c` | `sequence` | Numero de l'operation de premier niveau | compteur non nul modulo 32 bits |
| `0x10` | `op` | Operation observee | enum `OP` |
| `0x14` | `stage` | Dernier stage atteint | enum `STAGE` |
| `0x18` | `status` | Dernier resultat brut | `0` ou code signe |
| `0x1c` | `first_error_stage` | Premier stage en erreur, latche | enum `STAGE` |
| `0x20` | `first_error_code` | Premiere erreur, latchee | enum `ERROR` ou resultat brut signe |
| `0x24` | `requested_pattern` | Pattern/Project demande | `bank << 16 | slot`; pour Project, bank vaut 0 |
| `0x28` | `candidate_pattern` | Candidat Pattern vivant | `bank << 16 | slot`, valide si phase non vide |
| `0x2c` | `current_pattern` | Autorite Pattern courante | `bank << 16 | slot` |
| `0x30` | `candidate_phase` | Phase du recall Pattern | enum `PATTERN_PHASE` |
| `0x34` | `request_generation` | Generation logique du candidat | compteur non nul pendant recall |
| `0x38` | `io_generation` | Generation attachee a la lecture asynchrone | compteur; doit egaler request pendant `LOADING` |
| `0x3c` | `boundary_track` | Track de reference boundary | index track |
| `0x40` | `boundary_armed` | Attente boundary armee | `0` ou `1` |
| `0x44` | `boundary_generation` | Generation de boucle prise comme base | compteur sequenceur |
| `0x48` | `boundary_observed_generation` | Derniere generation observee | compteur sequenceur |
| `0x4c` | `boundary_due` | Boundary sample deja publiable | `0` ou `1` |
| `0x50` | `transport_running` | Transport au dernier poll du candidat | `0` ou `1` |
| `0x54` | `decision_reason` | Derniere decision recall | enum `DECISION` |
| `0x58` | `apply_attempted` | Nombre d'essais apply dans l'operation | compteur |
| `0x5c` | `apply_result` | Dernier resultat apply | `persist_codec_result_t`, signe |
| `0x60` | `audio_publish` | Commits de snapshot AUDIO reussis | compteur dans l'operation |
| `0x64` | `seq_publish` | Commits du nouvel etat musical SEQ reussis | compteur dans l'operation |
| `0x68` | `ui_sync` | Resynchronisations UI centrales | compteur dans l'operation |
| `0x6c` | `active_track` | Track actif apres sync UI | index track |
| `0x70` | `selected_track` | Track selectionne apres sync UI | index track |
| `0x74` | `ui_revision` | Revision associee a la sync UI | compteur UI |
| `0x78` | `workspace_owner` | Owner du workspace exclusif | `0 FREE`, `1 PROJECT_SAVE`, `2 PROJECT_RESTORE`, `3 PATTERN_IO`, `4 GROOVE_BUILD` |
| `0x7c` | `project_phase` | Phase Project Save/Load/Blank | enum `PROJECT_PHASE` |
| `0x80` | `commit_done` | Frontiere irreversible franchie | `0` pre-commit, `1` post-commit |
| `0x84` | `project_progress` | Progression courante Project | unite definie par l'operation |
| `0x88` | `detail` | Etat interne Project brut | etat Save ou Load selon `op` |
| `0x8c` | `detail0` | Detail de stage 0 | voir ci-dessous |
| `0x90` | `detail1` | Detail de stage 1 | voir ci-dessous |
| `0x94` | `detail2` | Detail de stage 2 | voir ci-dessous |
| `0x98` | `detail3` | Detail de stage 3 | voir ci-dessous |

## Enums exacts

`OP` : `0 NONE`, `1 PATTERN_SAVE`, `2 PATTERN_LOAD`, `3 PATTERN_APPLY`,
`4 PROJECT_SAVE`, `5 PROJECT_LOAD`, `6 PROJECT_BLANK`.

`STAGE` : `0 NONE`, `1 ENTER`, `2 POLICY`, `3 WORKSPACE`, `4 PATH`,
`5 MOUNT`, `6 OPEN`, `7 SIZE`, `8 READ`, `9 WRITE`, `10 ENCODE`,
`11 DECODE`, `12 VALIDATE`, `13 CANDIDATE`, `14 ASYNC`, `15 APPLY`,
`16 BANK_STAGE`, `17 BANK_COMMIT`, `18 PUBLISH`, `19 SEQ_SYNC`,
`20 UI_SYNC`, `21 CLOSE`, `22 SUCCESS`, `23 FAIL`.

`ERROR` : `0 NONE`, `1 POLICY`, `2 WORKSPACE`, `3 PATH`, `4 MOUNT`,
`5 FILESYSTEM`, `6 CODEC`, `7 VALIDATE`, `8 BANK`, `9 APPLY`, `10 MEDIA`,
`11 INTERNAL`. Un resultat codec, FatFs ou produit peut aussi etre expose
directement comme code signe; `first_error_stage` en donne alors le domaine.

`PATTERN_PHASE` : `0 EMPTY`, `1 REQUESTED`, `2 LOADING`, `3 PENDING`.

`DECISION` : `0 NONE`, `1 PREFLIGHT_BLOCKED`,
`2 TRANSPORT_STOPPED_APPLY`, `3 APPLY_FAILED`,
`4 TRANSPORT_RUNNING_PENDING`, `5 APPLY_SUCCEEDED`, `6 WAIT_BOUNDARY`.

`PROJECT_PHASE` : `0 NONE`, `1 SAVE_SNAPSHOT`, `2 SAVE_MOUNT`, `3 SAVE_OPEN`,
`4 SAVE_ENCODE`, `5 SAVE_WRITE`, `6 SAVE_SYNC`, `7 SAVE_REPLACE`,
`8 SAVE_CLEANUP`, `9 LOAD_DECODE`, `10 LOAD_STAGED`,
`11 LOAD_WAIT_QUIESCE`, `12 LOAD_ASSETS`, `13 LOAD_APPLY`,
`14 BLANK_BUILD`, `15 DONE`.

## Details de stage

Pour Pattern I/O, `detail0..3` valent normalement `{FatFs result, taille
demandee, taille transferee, capacite}`. Pour Project Load decode, ils valent
`{codec result, taille fichier, asset count, pattern count}`. Pendant la
restauration des assets : `{asset index, asset count, warning count,
commit_done}`. En erreur Project Save : `{save error, detail bas niveau,
offset fichier, taille encodee}`.

Au build Release/LTO courant, ELF et map donnent le symbole a `0x24000034`,
taille `0x9c`. Cette adresse n'est pas une ABI et peut changer au prochain
link; `info address` reste la source d'autorite.

```gdb
shell cls
info address g_persist_dbg
x/39wx &g_persist_dbg
```
