# Audit de l'`AUDIO_COMMAND_INVALID` restant au Pattern Recall

## Verdict

Le record fourni decrit toujours une commande `AUDIO_STATE_COMMIT` Pattern
valide pour le slot physique zero et sa generation locale un. Il ne localise
pas le sous-appel interne: `entity=0` est l'index du slot PreparedAudio, pas
l'entite musicale fautive.

La passe complete trouve deux refus atteignables a partir d'un Pattern valide:

1. lors d'une transition synth avec reduction de polyphonie, la comparaison
   qui devait fermer les sorties excedentaires etait executee apres
   l'installation du nouveau PROGRAM. A cet instant le runtime exposait deja
   le nombre de voix cible. La reduction devenait donc invisible, les anciennes
   sorties restaient held, puis `audio_note_engine_adapter_apply_polyphony()`
   refusait `held_count > target_voice_count` et faisait remonter
   `AUDIO_COMMAND_APPLY_INVALID`;
2. les payloads Tone MIDI/External contiennent aussi PROGRAM/CC MIDI. Le
   consommateur PreparedAudio essayait de les appliquer a AUDIO alors que leur
   owner est CONTROL/SEQ; `PARAM_MIDI_PROGRAM` est explicitement refuse par
   `param_audio_apply_non_filter()`. Un PROGRAM MIDI actif poursuivait ensuite
   a tort vers Common/FX/Mute, qui n'a pas de route AUDIO.

Le premier chemin est celui propre a FM vers PRISM lorsqu'une cible PRISM a
moins de voix que le FM sortant et que des sorties des voix retirees sont
encore vivantes. Avec le descriptor observe `flags=39`, la cible encode cinq
voix. La valeur attendue par l'endpoint est donc un ledger held de cardinalite
inferieure ou egale a cinq avant l'application de la polyphonie.

## Decodage du fatal

| Champ | Valeur | Decodage |
|---|---:|---|
| `code` | `0x4101` | `BRICK_FATAL_AUDIO_INVALID_COMMAND` |
| `context` | `0x60000` | `(opcode_kind=6 << 16) | id=0` |
| `entity` | `0` | `command.entity`, slot PreparedAudio physique zero |
| `requested` | `1` | `command.value`, generation locale du slot |
| `capacity` | `1` | valeur enumeree `AUDIO_COMMAND_APPLY_INVALID` |

`opcode_kind=6` donne opcode 6 `AUDIO_STATE_COMMIT` et kind 0
`CONTROL_AUDIO_STATE_PATTERN`. La commande effective est donc
`{slot=0,generation=1,transition=Pattern}`. `effective_sample_time=1585091` ne
participe plus aux gardes une fois la commande due.

`slot_id=1` et `generation=144` affiches dans la frame Release/LTO ne sont pas
les arguments materiels de la commande. Il n'existe ni handle logique de slot,
ni slot d'index un: `PREPARED_AUDIO_SLOT_COUNT=1`, et le fatal ainsi que le
contenu READY prouvent `{index=0,generation=1}`. La notation GDB
`g_prepared_audio_slots[1]` decrit la longueur du tableau.

## Chemin et refus possibles

| Etape | Fonction | Condition de refus |
|---|---|---|
| Dispatch | `audio_command_apply()` | opcode/kind non supporte |
| Slot | `audio_command_apply_prepared_state_commit()` | transition, index, generation, READY/reserved incoherents |
| Project panic | `audio_command_apply_panic()` | panic invalide, Project uniquement |
| Release/install | `audio_install_prepared_program()` | descriptor ou allocation PROGRAM invalide |
| MIDI | `audio_note_engine_adapter_apply_midi_config()` | entite ou canal hors domaine |
| Tone | `audio_prepared_apply_tone()` | codec/payload ou endpoint AUDIO refuse |
| FM | `audio_prepared_apply_fm()` | moteur courant non FM ou projection impossible |
| Common | `audio_prepared_apply_common()` | Filter/VCA/Mixer/Polyphony/FX refuse |
| Mute | `live_parameter_audio_runtime_apply_param()` | route de mix absente |
| Mod | `audio_prepared_apply_mod()` | owner, enum/source ou route invalide |
| Resource | `audio_prepared_apply_resource()` | moteur/type/slot runtime incoherent |
| Globals | `audio_global_runtime_apply()` | id global sans endpoint |
| Transport/input | setters AUDIO | owner, tempo ou step invalide |
| Temp clear | clear LFO/ENV | id/owner non clearable |
| Held rebind | `initialize_held_outputs()` | rebind impossible; retourne REBIND, pas INVALID |

Pour la reduction de polyphonie, le premier sous-appel INVALID etait:

```text
audio_prepared_apply_common
 -> audio_prepared_apply_float(CONTROL_AUDIO_CONFIG_POLY_VOICES, 5)
 -> live_parameter_audio_runtime_apply_param
 -> audio_note_engine_adapter_apply_polyphony
 -> held_count > 5
 -> 0
 -> AUDIO_COMMAND_APPLY_INVALID
```

L'ancien code calculait `current_voices` dans `audio_prepared_apply_common`,
apres l'installation du descriptor PRISM a cinq voix. Il comparait donc cinq a
cinq et n'executait jamais le trim que le contrat avait prevu.

## Correction

`audio_command_prepare_synth_program_change()` compare maintenant la cible au
runtime sortant avant `audio_command_close_entity()`, ferme les sorties SEQ au
dela de la cible avec l'ancien renderer encore installe, puis laisse le chemin
existant faire OFF, installer la cible et rebind les sorties conservees. Le
meme helper couvre le commit PreparedAudio Pattern/Project et le snapshot Patch.
Project reste PANIC: aucun held output n'y est conserve.

Le decode Tone PreparedAudio ignore explicitement `PARAM_MIDI_PROGRAM` et les
CC MIDI, car ils sont deja installes par CONTROL/SEQ et n'ont aucun endpoint
AUDIO. Un PROGRAM de famille MIDI installe seulement sa configuration MIDI et
ne traverse plus Tone/Common/FX/Mute/Mod/Resource. External conserve son
`PARAM_EXT_GATE` AUDIO mais ignore ses champs PROGRAM/CC MIDI.

Le fatal n'est ni supprime, ni affaibli. Slot, FIFO, timestamp, DMB, fence,
release-before-acquire et rebind restent inchanges.

## Sweep des invariants PreparedAudio

| Famille | Verdict apres correction | Contrat |
|---|---|---|
| PROGRAM/active/topology/product | OK | descriptor complet, comparaison tardive, OFF explicite |
| FM/PRISM et Tone synth | OK | moteur installe avant payload, tag/type concordants |
| MIDI Tone | corrige | PROGRAM/CC restent CONTROL/SEQ; aucune fausse route AUDIO |
| External Tone | corrige | gate AUDIO seulement; PROGRAM/CC non projetes |
| Filter/VCA/Mixer | OK | applicabilite projetee puis endpoint audio-routable |
| Polyphony | corrige | shrink decide sur l'ancien runtime; voix cible encodees dans PROGRAM |
| Audio FX | OK | valeurs canonicales et owner physique top-level |
| Mod/LFO/ENV/routes | OK | presence complete imposee par caps; owners resolus |
| Sampler/Wavetable/Multi | OK | kind complet; `UINT16_MAX`/slot invalide signifie none |
| Mute | OK | applique seulement aux PROGRAM audio-routables |
| 51 globals | OK | projection CONTROL vers domaine commande avant publication |
| Tempo/step/metronome | OK | valeurs non nulles/bornee preparees |
| Input owners | OK | deux inputs physiques, none explicite accepte |
| Temp clears | OK | producteur/consommateur communs sur 8 owners depuis `2bf2a2daf` |
| Held outputs | OK | preserve si capacite suffisante; excedent ferme avant changement |

Les representations OFF/NONE/zero restent des valeurs cible completes. Aucun
consumer ne les transforme en « conserver l'ancien ». Le slot PreparedAudio ne
redevient pas un delta.

## Impact Pattern, Project et cout

Pattern Recall et Pattern Load stopped utilisent le trim pre-install et le
rebind. Project Load/Blank utilisent le meme payload et beneficient de la
projection MIDI corrigee; leur PANIC rend le trim held inutile. Patch partageait
le mauvais ordre PROGRAM/polyphonie et utilise desormais le meme helper.

Aucune RAM persistante n'est ajoutee. Le cout CPU n'apparait que lors d'un
PROGRAM synth modifie: une comparaison et, seulement en cas de reduction, les
NOTE OFF deja requis par le contrat de polyphonie.

Validation sans materiel: build Release `BRICK6_CUBE.elf` reussi; firewall M7
`domain_dependency_check` passe sur 311 translation units.
