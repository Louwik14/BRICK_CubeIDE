# AGENT.md

## Role documentaire

Le code courant est l'autorite finale. `docs/architecture/ARCHITECTURE_GLOBAL.md`
est l'unique porte d'entree documentaire. Les documents de domaine qu'il liste
portent les details techniques actuels de leur zone.

## Mode de travail

- Travailler silencieusement jusqu'au resultat final.
- Auditer l'autorite canonique et ses consommateurs avant toute mutation.
- Preserver les changements utilisateur et eviter les refontes hors perimetre.
- Ne pas creer de double autorite, d'allocation dynamique dans le runtime
  critique ou de cout audio non borne.
- Mettre a jour la carte globale et la documentation proprietaire lorsqu'un
  invariant change.

## Invariants essentiels

- Seize identites logiques existent: huit top-level `0..7` et huit children
  GROUP `8..15`, actives uniquement lorsque 7 est GROUP master.
- `entity_topology` derive role, parent, activite et capacites.
- `track_state` decide, `track_runtime` projette, AUDIO execute.
- Looper est un type Sampler assignable; External utilise l'autorite unique
  `track_input_ownership`.
- Ressource physique, quota et voie mixer ne sont jamais des identites logiques.
- Master est global, ouvert par `SHIFT + STEP 16`, sans piste persistante.
- Pattern, Project et Patch utilisent le codec CONTROL version 3.
- Toute mutation structurelle ou restauration est integralement validee avant
  application.
