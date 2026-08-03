# Audit relâchement KBD — 2026-08-03

## Verdict

La cause racine était double : les fronts Hall étaient condensés dans deux
flags par touche, et une commande Note Off pouvait être rejetée par la file
NoteFx ou par une admission aval sans être conservée. Le propriétaire clavier
était alors perdu alors que le moteur pouvait rester `HELD`.

## Contrat corrigé

- Hall publie les fronts dans une FIFO bornée par touche, dans l'ordre exact.
  En saturation, la FIFO se resynchronise par `Off`, puis `On` si la touche est
  encore pressée.
- Chaque Note On source admis réserve physiquement une place pour son Note Off.
  Les autres commandes ne peuvent pas consommer cette réserve.
- Un Note Off source refusé après dépilage reste dans une liste de fermetures
  audio bornée et est rejoué avec le même `(track, provenance, occurrence)`.
  `ACCEPTED` et `REJECTED_STALE` terminent cette fermeture ; aucun timeout ni
  panic global n'est utilisé.
- Un Note Off terminal d'une occurrence polyphonique déjà volée est traité
  comme stale lorsque son propriétaire exact n'existe plus. Il ne peut pas
  fermer la nouvelle occurrence qui a repris la voix physique.
- Après un Stop/Panic réussi, l'état d'occurrences source KBD/MIDI est purgé
  avec la transition scheduler afin qu'un ancien propriétaire ne soit jamais
  réutilisé au prochain retrigger.

## Validation

Build `Release` Low-Cost réussi (`BRICK6_CUBE.elf`). Les chemins MIDI, STEP,
ARP, voice stealing et Stop/Panic conservent leurs APIs ; la modification
porte sur l'admission/fermeture et l'identité d'occurrence.
