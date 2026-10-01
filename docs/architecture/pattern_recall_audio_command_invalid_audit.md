# Audit `AUDIO_COMMAND_INVALID` du Pattern Recall

## Verdict

Le slot, sa generation et la commande FIFO observes sont coherents. Le fatal
vient du `temp_clear_mask` ajoute par `a2846bad6`: il etait dimensionne et parcouru
sur les 16 entites logiques, alors que les runtimes temporaires LFO/ENV ont
exactement 8 owners AUDIO physiques. La premiere operation invalide garantie
est donc le clear de `PARAM_LFO1_RATE` (`id=47`) sur l'entite 8.

## Decodage du fatal et commande

`0x4101` est `BRICK_FATAL_AUDIO_INVALID_COMMAND`. Il correspond ici au retour
`AUDIO_COMMAND_APPLY_INVALID`, valeur enumeree 1. Le record est construit ainsi:

| Record | Valeur | Signification |
|---|---:|---|
| `entity` | 0 | `command.entity`, index physique du slot |
| `context` | `0x00060000` | `(opcode_kind << 16) | id`: opcode/kind brut 6, id 0 |
| `requested` | 1 | `command.value`, generation AUDIO |
| `capacity` | 1 | resultat `AUDIO_COMMAND_APPLY_INVALID`, pas une capacite de slot |

La commande consommee est donc:

```text
effective_sample_time = T_asap resolu par control_rt_resolve_asap_sample()
value                 = 1
id                    = 0
entity                = 0
opcode                 = 6 = AUDIO_STATE_COMMIT
kind                   = 0 = CONTROL_AUDIO_STATE_PATTERN
opcode_kind            = 6
```

La valeur numerique de `T_asap` n'est pas conservee dans le fatal fourni; elle
est presente dans le snapshot FIFO de la capsule. Elle n'intervient pas dans la
validation du slot apres que la commande est devenue due.

## Dispatch et premiere condition invalide

`audio_command_apply()` decode `opcode_kind & 7`, entre dans
`CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT`, puis selectionne
`audio_command_apply_prepared_state_commit()` parce que `kind != PATCH`.

Les gardes d'entree passent avec le record observe:

```text
transition=0 <= PROJECT
entity=0 < PREPARED_AUDIO_SLOT_COUNT=1
value=1 != 0
slot[0].ready=1
slot[0].reserved=1
slot[0].generation=1
slot[0].transition=0
```

Le PROGRAM PRISM observe est egalement structurellement coherent:
`engine=3`, `family=1`, `type=2`, `flags=39`; `product_kind=1` designe Tone,
ce qui est attendu pour PRISM, et `muted=0` est valide.

Apres PROGRAM/payload/resources/globals/transport, le consommateur parcourait:

```c
for (entity = 0; entity < BRICK_ENTITY_CAPACITY; ++entity) // 16
    clear(temp_clear_mask[entity]);
```

Le producteur avait rempli le meme masque pour les 16 entites. Or
`param_registry_temp_is_clearable()` ne selectionne que LFO1..3 et ENV3;
`PARAM_LFO1_RATE`, id 47, est le premier bit. Son endpoint appelle
`mod_lfo_v1_clear_track_param_temp_audio()`, dont le domaine est
`track < SEQ_TRACK_COUNT`, soit 8. L'appel `(entity=8,id=47)` retourne 0,
`live_parameter_audio_runtime_apply_param()` retourne 0, puis le commit saute a
`invalid` et produit exactement `AUDIO_COMMAND_APPLY_INVALID`/`0x4101`.

## Slot et generation

Il n'existe aucun ID logique distinct de l'index physique. Le backing est un
tableau de longueur un:

```text
g_prepared_audio_slots[PREPARED_AUDIO_SLOT_COUNT]
PREPARED_AUDIO_SLOT_COUNT = 1
index valide = 0 uniquement
```

`prepared_audio_control_reserve()` reserve toujours `[0]`, incremente son
`generation`, initialise `reserved=1, ready=0`, et renvoie `slot_id=0`.
`prepared_audio_control_publish()` fait `DMB`, positionne `ready=1`, publie
`{entity=0,value=generation}`, attend la consommation FIFO, puis remet
`ready=0,reserved=0`. L'IRQ doit donc voir `reserved=1,ready=1`.

Le record fatal prouve `slot=0,generation=1`. Les arguments
`slot_id=1,generation=228` affiches dans un frame LTO ne sont pas les valeurs de
la commande consommee. De meme, la notation GDB `g_prepared_audio_slots[1]`
decrit un tableau de longueur 1, pas l'element d'index 1.

## Correctif et portee

`temp_clear_mask` est maintenant dimensionne par
`PREPARED_AUDIO_TEMP_OWNER_COUNT = BRICK_ENTITY_TOP_LEVEL_COUNT = 8`. Le
producteur et le consommateur utilisent la meme borne. Les group children n'ont
pas de runtime LFO/ENV propre: leur owner physique est le group master, deja dans
0..7. Les tracks devenues OFF restent couvertes, ce qui efface leurs temporaires
sans transformer « OFF » en « ignorer ».

Le remplissage Project du masque, historiquement duplique apres le commit
CONTROL, est supprime: Pattern, Project Load et Project Blank utilisent tous le
masque commun construit par `persistent_pattern_prepare_audio()`.

Les transitions FM/PRISM/OFF, Sampler/Wavetable/Multi vers none, FX OFF et mute
ne changent pas de contrat. Le fatal, le FIFO, timestamp, `DMB`, fence,
release-before-acquire et held-output rebind restent inchanges.

## Bugs freres

- PreparedSeq distingue deja `SEQ_TRACK_COUNT` et `SEQ_LANE_CAPACITY`; aucun
  slot/generation melange n'a ete trouve.
- Le snapshot Patch selectionne son consommateur par `kind=PATCH` et n'utilise
  pas `g_prepared_audio_slots`; il n'est pas affecte.
- Les registries de ressources utilisent leurs propres couples
  slot/generation READY; aucun melange logique/physique analogue n'a ete trouve.
- Le duplicate Project du masque etait un bug frere demontre: avec 16 entites,
  il pouvait declencher le meme INVALID. Il est retire au profit du producteur
  commun.

La taille ARM de `PreparedAudio` passe de 9 048 a 8 728 octets et celle du slot
de 9 056 a 8 736 octets, soit 320 octets de RAM economises. Aucun cout CPU n'est
ajoute; l'IRQ parcourt huit lignes de masque au lieu de seize.

Validation: build Release `BRICK6_CUBE.elf` reussi et firewall M7
`domain_dependency_check` passe sur 311 translation units. Aucun test materiel
n'est revendique.
