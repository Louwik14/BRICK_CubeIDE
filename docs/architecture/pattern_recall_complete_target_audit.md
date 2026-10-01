# Audit Pattern Recall : cible complete CONTROL, SEQ et AUDIO

## Verdict et cause racine

Le document Pattern et le descripteur PROGRAM n'avaient pas perdu l'identite
FM/PRISM : le codec transporte `family/type`, la preparation les convertit en
`runtime family/type`, et le descripteur compare par AUDIO contient bien
`engine/family/type/flags`. FM et PRISM restent donc distincts sur ces quatre
etapes.

La rupture etait le contrat de `PreparedAudio`. Il ne representait pas une cible
complete issue d'une seule autorite : PROGRAM et quelques champs etaient figes
pendant PREPARE, une partie du payload et les ressources etaient recaptures apres
le commit CONTROL, et certains effets CONTROL dont la publication FIFO etait
volontairement inhibee n'etaient pas dans le slot. La sequence, elle, etait
compilee en entier dans un slot inactif puis remplacee atomiquement. Ce contrat
asymetrique explique le symptome « sequence B + runtime AUDIO partiellement A ».

La correction fait de `PreparedAudio` une cible complete. PREPARE reserve le
slot, prouve les identites et construit `PreparedSeq`; apres installation du
candidat CONTROL, une projection canonique reconstruit chaque entree AUDIO,
PROGRAM compris, depuis les owners CONTROL effectivement installes. L'absence
d'une ressource signifie desormais explicitement `none`, le mute effectif est
transporte dans le slot, et les overrides temporaires clearables sont effaces
pour Pattern comme pour Project.

## Pipeline et owners

| Etape | Source | Destination | Operation / mask / generation | Fonction | Owner |
|---|---|---|---|---|---|
| Lecture | fichier Pattern | octets codec | copie FatFS | banque Pattern | Storage |
| Decode | octets codec | `persist_control_pattern_t` | copie et validation de format | `persist_codec_decode_pattern*` | Storage DTO |
| Prevalidation | DTO | preuves `persistent_pattern_prepared_t` | resolution famille/type, caps, cles, p-locks | `persistent_pattern_control_prepare()` | CONTROL prepare |
| SEQ prepare | DTO + cles resolues | slot SEQ inactif | copie complete track/steps/p-locks/Note FX/mute; generation SEQ | `persistent_pattern_prepare_seq()` | SEQ |
| AUDIO prepare | DTO + preuves | slot `g_prepared_audio_slots[slot]` reserve | generation AUDIO unique, cible provisoire, `temp_clear_mask` complet | `persistent_pattern_prepare_audio()` | CONTROL publication |
| Attente | candidat PREPARED | boundary armee | reference stable au DTO/workspace | `pattern_candidate_arm_boundary()` | Pattern CONTROL |
| Commit CONTROL | DTO + preuves | Track/Product/Tone/FM/Filter/VCA/Mixer/Poly/FX/Mod/MIDI/Globals CONTROL | copie/install complete; les publications PROGRAM/PARAM ordinaires sont inhibees | `persistent_pattern_control_commit_prepared_control()` | CONTROL |
| Finalisation AUDIO | owners CONTROL installes | meme slot AUDIO reserve | reconstruction complete des 16 entites; meme `{slot,generation}` | `persistent_pattern_finalize_audio_from_control()` | CONTROL vers AUDIO |
| Commit SEQ | slot inactif | slot SEQ actif | swap generation; REPLACE ou FLUSH | `persistent_pattern_control_commit_prepared_seq()` | SEQ |
| Publication | slot AUDIO final | FIFO datee | `AUDIO_STATE_COMMIT(slot,generation)` puis `DMB`/fence | `prepared_audio_control_publish()` | CONTROL vers AUDIO |
| IRQ AUDIO | FIFO + slot generation | runtime AUDIO | comparaison tardive, close, OFF, install, payload, rebind | `audio_command_apply_prepared_state_commit()` | AUDIO |

Le slot est une copie possedee par la publication, jamais une reference vers le
DTO. Le DTO reste reference par `PreparedPattern` jusqu'au commit CONTROL. Les
seuls deltas calcules tard sont `changed_program_mask`, l'allocation/rebind des
sorties tenues et la generation runtime des ressources.

## PROGRAM FM vers PRISM

Chemin final :

1. le DTO B porte `PERSIST_FAMILY_SYNTH` et son `type` PRISM;
2. CONTROL installe `TRACK_FAMILY_SYNTH/TRACK_TYPE_PRISM`;
3. `track_runtime_type_from_ui()` donne `TRACK_RUNTIME_TYPE_PRISM` et
   `track_runtime_choose_engine()` donne `TRACK_RUNTIME_ENGINE_PRISM`;
4. la finalisation ecrit ces valeurs dans
   `PreparedAudio.entity[e].program.{engine,family,type,flags}`;
5. a son timestamp, l'IRQ compare exactement les quatre membres au contexte
   AUDIO courant FM;
6. la difference positionne le bit `changed_program_mask`, ferme FM, installe
   OFF, puis installe PRISM;
7. les etats produit et communs sont appliques, puis les sorties tenues de cette
   entite sont reinitialisees selon le contrat existant.

Le chemin PRISM vers FM est symetrique. Le choix FM du payload se fait avec le
`runtime type` final : FM capture `fm_control_state_t`, tous les autres moteurs
capturent `tone_program_control_t`. Ni une ressource identique ni son absence ne
peut rendre les deux PROGRAM egaux, car l'identite ne repose pas sur la
ressource.

## Chronologie des champs AUDIO

| Classe | Avant la correction | Contrat final |
|---|---|---|
| A - PREPARE | validation, resolutions, PROGRAM provisoire, slot SEQ complet | validation/resolutions, slot reserve, cible provisoire, `temp_clear_mask`, slot SEQ complet |
| B - apres CONTROL | globals, payload partiel, handles ressources | PROGRAM, topology, produit, Filter/VCA/Mixer/Poly/FX/Mod, MIDI, mute, ressources, globals, tempo/step/metronome/inputs |
| C - IRQ | comparaison PROGRAM, close/install, rebind | identique : `changed_program_mask`, held outputs et generations runtime restent tardifs |

Aucun champ persistant Pattern n'est volontairement traite comme un delta
AUDIO. Les champs absents du format ne signifient jamais « conserver A ».

## Matrice exhaustive

| Famille Pattern | DTO / PreparedPattern | CONTROL commit | PreparedSeq / PreparedAudio | Runtime final | Etat apres correction |
|---|---|---|---|---|---|
| Structure/type Track | famille, type et preuves topology | bulk install explicite, OFF inclus | PROGRAM recapture apres bulk | engine map remplacee | OK |
| PROGRAM/engine | famille/type resolus | runtime ctx remplace | descriptor complet `engine/family/type/flags` | close/OFF/install si difference | OK, etait hors finalisation tardive |
| Tone | etat complet par type | install complet | recapture owner Tone | PARAM base remplaces | OK |
| FM | etat FM complet | install complet | recapture owner FM si runtime FM | etat FM remplace | OK |
| Filter | etat complet | install complet | recapture | PARAM base remplaces | OK |
| VCA | etat complet | install complet | recapture | PARAM base remplaces | OK |
| Mixer | etat complet | install complet | recapture | PARAM base remplaces | OK |
| Polyphony | voix cible validee | bulk + owner install | voix effective + flags PROGRAM | allocation nouvelle | OK |
| Audio FX | slots complets, OFF compris | install complet | recapture | chaine/params remplaces | OK |
| Sequence | tracks/steps complets | aucune mutation incrementaliste | slot inactif complet | swap atomique | OK |
| p-locks | cles/adresses resolues | aucune mutation incrementaliste | liste complete par step | nouvelle sequence | OK |
| Note FX | chaine fixe complete | owner installe | slot SEQ complet | chaine remplacee | OK |
| Mute | valeur locale DTO | CONTROL installe; FIFO inhibee pendant commit | mute effectif ajoute a AUDIO + mute SEQ | sequencer et mixer coherents | FIXE : AUDIO pouvait rester stale |
| Mod matrix | routes completes/desactivees | install complet | recapture si owner | matrice finalisee | OK |
| LFO | valeurs completes | install complet | recapture | valeurs base remplacees | OK |
| ENV | enveloppe complete | install complet | recapture | valeurs base remplacees | OK |
| Sampler selection | liste logique 0 ou 1 | clear puis restore | resolution runtime, `present=0` si none | binding cible | FIXE : 0 asset etait un no-op |
| Wavetable selection | deux roles logiques | clear puis restore | capture slot/generation des deux oscillateurs | binding cible | FIXE pour retour a none/default |
| Multi selection | binding sampler logique | clear puis restore | resolution runtime Multi | binding cible | FIXE pour retour a none/default |
| MIDI | channel/source complets | install explicite | recapture | config adapter remplacee | OK |
| Globals AUDIO | 51 valeurs validees | owner global installe | projection command-domain | PARAM globaux remplaces | OK depuis `7b220c40d` |
| Tempo | valeur globale | runtime CONTROL | capture finale | transport AUDIO remplace | OK |
| Step | derive du tempo | runtime CONTROL | capture Q16 finale | transport AUDIO remplace | DERIVED |
| Metronome | valeur globale | owner CONTROL | capture finale | runtime metronome remplace | OK |
| Input ownership | input de chaque track | bulk structure | deux owners recaptures | route input remplacee | OK |
| Groove/clock/record | donnees DTO selon owner | install CONTROL/SEQ | slot SEQ ou owner dedie | runtime correspondant | OK/DERIVED |
| Temporary/p-lock override | non persiste comme base | aucun heritage voulu | clear mask complet | overrides de A effaces | FIXE pour Pattern; Project deja couvert localement |
| Macros | etat Project, pas Pattern | Project les installe; Pattern resynchronise les sources | hors `PreparedAudio` | owner Macro coherent | hors DTO Pattern, politique explicite |

`modulation_present` suit la capability canonique, pas la presence d'une route
active. Une route desactivee et une matrice vide sont donc des valeurs completes,
et non un masque sparse. OFF, zero, liste vide et binding absent sont des valeurs
cibles applicables.

## Blank, Project, ressources et notes tenues

Le Blank et le Recall utilisent `persistent_pattern_control_build_defaults()`
puis le meme prepare/commit. Il n'existe plus de famille que le Blank efface mais
que Recall laisserait implicitement vivre : le clear des bindings, le mute et le
clear des temporaires appartiennent au chemin commun.

Project Working Pattern utilise le meme finaliseur. Le PANIC Project et le bit
`changed_program_mask` force pour toutes les entites masquaient un PROGRAM stale,
mais ne rendaient pas correcte une cible partielle. La correction couvre donc
Pattern et Project; seul Project conserve son PANIC contractuel.

Pour Sampler A vers B, Wavetable A vers B et Multi vers un autre moteur, CONTROL
efface d'abord le binding logique puis installe celui du candidat. La finalisation
capture ensuite le handle/slot/generation READY. Pour resource vers none, la
cible sampler porte le runtime invalide et la cible wavetable deux selections
invalides; il ne s'agit jamais d'une omission. AUDIO conserve
release-before-acquire : PROGRAM change ferme l'ancien renderer et installe OFF
avant le nouveau; le lifecycle des assets n'est pas contourne.

Un PROGRAM identique ne positionne pas le masque et conserve les notes tenues.
Un PROGRAM different, dont FM vers PRISM, ferme l'ancien renderer puis rebinde
les held outputs apres installation complete. La suppression de
`active_mask`/`rebind_candidate_mask` n'est pas causale : active est porte par
chaque entree et le rebind est derive tardivement du PROGRAM canonique.

## Origine du bug et cout

`53bcd05a1` a introduit la preparation Pattern/SEQ. `4530f09a8` a introduit le
slot AUDIO avec la provenance mixte PREPARE/finalisation. `7b220c40d` a corrige
la projection des globals, sans generaliser l'invariant a tout le slot. Le
cleanup ulterieur des deux masks et de `persistent_sequence_changed()` n'a pas
retire d'information necessaire : ces informations sont derivees des owners et
des slots complets.

La correction ne rajoute aucun slot, aucune transaction live ni rollback. Le
champ `muted` reutilise l'octet reserve existant; la taille reste 9 048 octets
pour l'etat et 9 056 pour le slot. Le cout CPU est une recapture CONTROL de 16
entites au commit, hors IRQ. L'IRQ conserve un seul parcours borne, le meme FIFO,
le meme timestamp, le meme `DMB` et la meme fence de consommation.

## Validation statique

Les transitions suivantes ont une politique totale : FM vers PRISM et inverse
changent PROGRAM; FX ON vers OFF applique les slots OFF; Mod active vers aucune
route installe des routes desactivees; resource A vers B resout B; resource vers
none publie `present=0`; mute ON/OFF est applique a SEQ et AUDIO; une polyphonie
differente est recapturee dans PROGRAM/payload; les globals sont reprojetes dans
le domaine de commandes AUDIO.

Validation executable : build Release de `BRICK6_CUBE.elf` et cible
`domain_dependency_check` (firewall M7). Aucun test materiel n'est revendique.
