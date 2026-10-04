# Audit des representations Track

## 1. Representations recensees

| Etat logique | Persist / CONTROL | Runtime / PROGRAM AUDIO | SEQ | Asset |
|---|---|---|---|---|
| OFF | `OFF/NONE` | `OFF/NONE/NONE`, inactif audio | lane top-level disponible mais sans renderer | aucun |
| SYNTH | `SYNTH/{PRISM,STACK,WAVE,FM,TB303,ACID}` | meme type, famille `SYNTH`, renderer specifique | owner top-level, capacite logique 8 | WAVETABLE pour WAVE, aucun sinon |
| STREAM | `SAMPLER/STREAM` | `SAMPLER/STREAM/SAMPLER` | owner top-level, capacite logique 8 | `SAMPLE_STREAM` |
| RAM | `SAMPLER/RAM` | `SAMPLER/RAM/SAMPLER` | owner top-level, capacite logique 8 | `SAMPLE_RAM` |
| MULTI | `SAMPLER/MULTI` | `SAMPLER/MULTI/SAMPLER` | owner top-level, capacite logique 8 | `MULTI` |
| GROUP master | `SAMPLER/GROUP` | `OTHER/GROUP/NONE` + `GROUP_MASTER` | actif, non-emetteur, capacite 0 | aucun |
| GROUP child | `SAMPLER/RAM` | `SAMPLER/RAM/SAMPLER` + `GROUP_CHILD` | owner child, mono, capacite 1 | `SAMPLE_RAM` |
| MIDI | `MIDI/MIDI` | `MIDI/MIDI/NONE`, endpoint logique | owner top-level; sortie interne desactivee | aucun |
| EXTERNAL | `EXTERNAL/EXTERNAL` | `EXTERNAL/EXTERNAL/AUDIO_TRACK` | owner top-level; sortie interne desactivee | entree physique, pas d'asset fichier |
| Preview | Patch synth/sampler seulement | enum `PATCH_PREVIEW_ENGINE_*`, instances dediees `0xFF` | hors SEQ | resolution dediee au preview |

`track_family_t`/`track_type_t` portent le choix UI et persiste;
`track_runtime_family_t`/`track_runtime_type_t`/`track_runtime_engine_t`
portent le contrat CONTROL/AUDIO; `entity_topology_descriptor_t` porte
separement activite, role et owner. Le modele persistant utilise des cles
stables explicites, jamais les valeurs numeriques des enums runtime.

## 2. Source de verite

Les conversions communes sont `track_runtime_family_from_ui_config()`,
`track_runtime_type_from_ui()`, `track_runtime_choose_engine()`,
`track_runtime_compute_flags()` et, apres cet audit,
`track_runtime_program_is_canonical()`. La derniere fonction est l'unique
grammaire des triplets PROGRAM `family/type/renderer` et des roles GROUP.

## 3. Traductions dupliquees

PREPARE, la reconstruction runtime et la finalisation AUDIO appelaient deja les
memes helpers de conversion. En revanche, CONTROL, le diagnostic AUDIO et
l'installateur AUDIO validaient chacun le PROGRAM differemment. Le switch du
diagnostic AUDIO et les validations partielles CONTROL/AUDIO ont ete remplaces
par la grammaire commune. Les mappings asset, Patch Preview et cles Persist
restent volontairement separes: ils traduisent vers des domaines distincts.

## 4. Incoherence reelle

La grammaire FIFO CONTROL acceptait tout triplet dans les bornes, sauf un
controle plus strict du child GROUP. L'installateur AUDIO appliquait la meme
validation partielle. Un triplet impossible tel que
`SYNTH/PRISM/SAMPLER`, ou un flag `GROUP_MASTER` pose sur un autre type,
pouvait donc franchir le preflight et installer une route dont la famille, le
type et le renderer divergeaient. Le diagnostic complet le detectait seulement
quand `BRICK_PATTERN_RECALL_DIAG` etait actif.

## 5. Differences legitimes

- `SAMPLER/GROUP` en UI/Persist et `OTHER/GROUP/NONE` en PROGRAM est la
  canonicalisation voulue du bus GROUP.
- MIDI est actif pour le routage sans endpoint audio physique.
- OFF peut appartenir a un slot top-level actif topologiquement tout en restant
  sans renderer.
- Un child hors GROUP conserve sa configuration persistante mais est projete
  `OFF/NONE/NONE` tant qu'il est topologiquement inactif.
- Preview emploie des instances dediees et ne fait pas partie des 16 entities.

## 6. Symptomes audites

Les chemins `AUDIO_COMMAND_INVALID`, installation PROGRAM, mapping, republish
asset, Pattern Recall, Project Load, RESUME et Preview ont ete relus. Aucun autre
producteur actif ne fabrique un triplet divergent. Les transitions listees dans
la demande convergent toutes par les memes helpers runtime; les changements de
type sampler changent aussi le kind asset attendu, et OFF/normal/GROUP nettoient
ou ignorent correctement les selectors non applicables.

## 7. Correctif

`track_runtime_program_is_canonical()` rejette maintenant toutes les
combinaisons non supportees, y compris `OTHER` hors GROUP, GROUP sans role,
master/child incoherent, famille/type incompatible et renderer different de
`track_runtime_choose_engine()`. CONTROL l'applique avant publication, le
preflight PreparedAudio avant mutation, et AUDIO avant installation.

## 8. Mutualisation

Le calcul du renderer reste dans `track_runtime_choose_engine()`; la validation
du tuple complet est mutualisee dans `track_runtime_program_is_canonical()`.
Aucune abstraction supplementaire n'a ete ajoutee aux assets ou a Preview.

## 9. GROUP

Le master actif est uniquement `OTHER/GROUP/NONE + GROUP_MASTER`; ses children
actifs sont uniquement `SAMPLER/RAM/SAMPLER + GROUP_CHILD`. Le master ne possede
ni lane de notes ni asset sampler. Les children inactifs sont projetes OFF et la
republication asset est un no-op valide. Les passages GROUP/normal reconstruisent
les 16 contextes avant la publication et la finalisation PreparedAudio compare
le PROGRAM prepare au PROGRAM effectivement installe.

## 10. STREAM, RAM et MULTI

Les trois types partagent le renderer Sampler mais gardent des types runtime et
des kinds asset distincts. STREAM resolve `SAMPLE_STREAM`, RAM
`SAMPLE_RAM`, MULTI `MULTI`; aucun fallback de type inconnu vers Sampler n'a ete
trouve. Les changements entre ces types reconstruisent PROGRAM puis
reprojettent seulement l'asset dont le kind correspond au nouveau type.

## 11. OFF, SYNTH et autres

OFF est normalise en `OFF/NONE`; chaque synth a un renderer explicite; Drum,
MIDI et External ont chacun une combinaison fermee. Un type inconnu devient
`TRACK_RUNTIME_TYPE_OTHER`, puis ne peut obtenir aucun renderer et est refuse
par la validation de structure et par la nouvelle grammaire PROGRAM.

## 12. Preview Track

Patch Preview valide uniquement les types synth et sampler supportes, les
convertit vers `PATCH_PREVIEW_ENGINE_*`, puis utilise les instances engine
dediees `0xFF`. Il n'appelle pas les helpers indexes par entity `0..15` avec
`0xFF`; aucune correction n'est requise.

## 13. Preflight et runtime

Le preflight PreparedAudio verifie desormais, pour les 16 entrees, le triplet
canonique, l'activite, le role topologique et la concordance des flags
master/child. L'installateur AUDIO reapplique la meme grammaire. Un etat
determinable invalide ne peut donc plus etre accepte au preflight puis rejete ou
installe differemment par AUDIO.

## 14. Diagnostics

Le diagnostic AUDIO derive maintenant son renderer attendu de la source commune
au lieu d'un switch local. `g_pattern_recall_diag` compare donc le PROGRAM
canonique complet. `g_audio_command_fatal_record` conserve le payload PROGRAM
demande; `g_persist_dbg` conserve type runtime et renderer courant/cible; les
fatals de republish conservent entity, type et stage. Aucune instrumentation
supplementaire n'est necessaire.

## 15. Release

Le build complet Release de `BRICK6_CUBE.elf` reussit.

## 16. Commit

Le correctif doit etre livre dans un commit local unique, sans push, en excluant
les changements hors perimetre deja presents dans le worktree.
