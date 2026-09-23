# Audit Project/Pattern Load -> SEQ -> AUDIO — 2026-09-23

## Cycle et owners

Project Load decode et valide d'abord le DTO et le bank Pattern inactif. A
`T_commit`, `project_load_quiesce` ferme les ingress live/MIDI, purge leurs
queues, demande le PANIC global AUDIO, attend la consommation de l'horizon et
le retrait des leases/pools, puis charge les assets du candidat. Le commit
final installe les owners CONTROL du Pattern, NoteFX, Groove, pistes et moteurs,
compile un `seq_pattern_t` immutable, publie sa generation, retire l'execution
SEQ precedente, puis publie et attend le snapshot AUDIO Project. L'ingress ne
rouvre qu'apres ce commit et la liberation du workspace.

Pattern Load arrete emploie le meme remplacement SEQ avant son snapshot AUDIO
Pattern. Pattern Load en lecture reste une transition de cycle: il conserve
l'epoch musical et laisse le passage normal d'une generation terminale a la
suivante. Le restore du dernier Project au boot emprunte Project Load; les pools
sont initialises vides avant ce chemin. Les remplacements locaux de piste
emploient le disarm par piste puis le rearm sur generation, et ne sont donc pas
des remplacements globaux.

## Premiere divergence

Avant ce correctif, `seq_engine_control_reset_note_fx_context()` avancait
l'epoch et `seq_engine_control_flush_with_workspace()` publiait bien le nouveau
Pattern avant le snapshot AUDIO. En revanche, l'adaptateur H743 conservait son
graphe mutable: `g_terminal[]` READY/READING, `g_audio_slot` et son curseur,
`g_core` avec ledgers, sources, calendriers Groove/NoteFX, ingress et IRQ TIM4
pending. `seq_ingress_panic()` ne resetait le core que lors d'un futur service
TIM4 et ne retirait aucun bloc terminal deja publie.

Un bloc produit sous la generation N pouvait donc etre acquis apres la
publication N+1. Dans le meme half-buffer, AUDIO applique d'abord le snapshot
Project — PANIC puis nouveaux PROGRAM/PARAM — et consomme ensuite le bloc SEQ
ancien. La premiere divergence est ainsi le bloc terminal stale, pas le
renderer. Un NOTE_ON ancien peut ne plus avoir de mapping dans le nouveau
moteur; un PARAM ancien peut ne plus etre applicable a la nouvelle piste.
`audio_command_executor_apply_seq_event()` retourne alors faux et l'invariant
de `audio_process_half_common_hot()` aboutit volontairement a `Error_Handler()`.

## Etats audites

- CONTROL->AUDIO FIFO: le PANIC precede le snapshot et le commit attend le tail;
  aucun snapshot/workspace n'est reutilise avant acquittement.
- Ownership AUDIO: PANIC global ferme les renderers, oublie les sorties
  physiques et efface `g_audio_seq_output` ainsi que son track mask.
- SEQ immutable: double buffer `seq_pattern_t`, generation publiee apres
  compilation Groove/NoteFX et capture des descriptors de piste.
- SEQ mutable: core, ledgers, sources, calendriers, held NoteFX, terminal blocks,
  cursors, ingress, force-stop, disarm/rearm et IRQ pending sont maintenant
  retires par une primitive unique de remplacement.
- Live-rec CONTROL: la queue effective Note On/Off est explicitement jetee au
  debut d'un remplacement global; elle ne peut plus etre drainee dans N+1.
- Restore scratch: le scratch Groove appartient a la transaction et n'est
  utilise que jusqu'a la publication; il est libere apres commit.
- Track/engine runtime: CONTROL est installe avant compilation SEQ; AUDIO est
  installe ensuite dans un snapshot unique. Les remplacements locaux restent
  proteges par disarm/rearm de piste.

## Contrat corrige

`seq_engine_control_replace_with_workspace()` centralise le remplacement
global. Il purge le live-rec CONTROL, avance l'epoch, compile le Pattern et,
dans la section critique qui publie sa generation, appelle
`seq_engine_execution_replace()`. Cette primitive retire tous les etats derives
de N et initialise l'autorite d'execution a N+1 avant de rendre les IRQ. La
frontiere AUDIO valide en plus les generations des blocs READY: un bloc doit
appartenir soit a la generation d'execution active, soit a la generation
immutable suivante publiee par une transition normale. Un bloc plus ancien ne
peut donc jamais atteindre le consumer.

L'ordre global est desormais: gel ingress; PANIC/acquittement; retrait assets;
restore CONTROL/assets; compilation immutable; publication generation et
retrait mutable atomiques; commit AUDIO acquitte; publication UI/metadata;
reouverture ingress; PLAY et production exclusive sous la nouvelle generation.
