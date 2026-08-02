# Z2 — Autorité des pistes

`track_state` est l'autorité canonique des huit configurations indexées `0..7`. `track_runtime` en dérive moteur, capacités, binding physique et révisions. `track_topology` ne décrit que les huit slots homogènes et leurs capacités communes; aucun rôle ou ordinal n'existe.

Une mutation suit `validation -> commit canonique -> refresh runtime`. Les opérations en masse valident intégralement familles, types, quotas et ownership avant le commit. `Off` libère ses ressources. MIDI garde un chemin notes sans audio local.

Looper est le moteur `Sampler / Looper`, avec état et stockage attachés au même index. External conserve l'identité exacte de son entrée physique; `track_input_ownership` refuse tout second propriétaire. L'allocation mixer reste séparée de l'entrée physique.

Mute, MIDI source/canal, clavier, scheduler, snapshots et révisions utilisent uniquement l'index `0..7`.
