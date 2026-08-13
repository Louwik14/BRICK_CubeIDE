# Z2 — Autorité des pistes

`track_state` est l'autorité canonique des seize configurations indexées par `brick_entity_id_t`; chaque GROUP child conserve donc sa propre famille, son propre type et sa propre configuration MIDI. `entity_topology` absorbe l'ancienne description `track_topology`/`seq_lane` et constitue l'unique autorité logique pour l'activité, le rôle `MAIN`/`GROUP_MASTER`/`GROUP_CHILD` et la relation parent/membre. Les capacités comme `can_emit_notes` sont dérivées du descriptor et ne constituent pas un second état. `track_runtime` projette ensuite cette identité et sa configuration, sans reconstruire un child en `Sampler / RAM`, vers le moteur et les ressources physiques.

Les anciennes API `track_topology_*` et `seq_lane_*` ont été supprimées après migration directe de leurs consommateurs vers `entity_topology`. Le vocabulaire `seq_track_id_t`/`seq_lane_id_t` reste un alias de domaine sans conversion ni identité concurrente.

La réalisation AUDIO est portée par l'unique `track_audio_binding_t` inclus dans chaque contexte runtime : `entity_id`, moteur, instance, cible mixer, état/reason et génération. L'ancienne vue anonyme des champs du contexte est supprimée. `track_runtime` incrémente `generation` uniquement lorsque la réalisation publiée change; un simple refresh identique la conserve.

Les consommateurs AUDIO accèdent désormais explicitement à `ctx->audio_binding` pour le moteur, l'instance et la cible mixer. Ils conservent `entity_id` pour leurs états logiques propres; la table persistante de reverse mapping mixer vers entité est supprimée.

Une mutation suit `validation -> commit canonique -> refresh runtime`. Les opérations en masse valident intégralement familles, types, quotas et ownership avant le commit. `Off` libère ses ressources. MIDI garde un chemin notes sans audio local.

Looper est le moteur `Sampler / Looper`, avec état et stockage attachés au même index. External conserve l'identité exacte de son entrée physique; `track_input_ownership` refuse tout second propriétaire. L'allocation mixer reste séparée de l'entrée physique.

Les révisions et la configuration MIDI suivent les seize identités. Les formats bulk, snapshots et persistence restent volontairement top-level tant que leur migration GROUP complète n'est pas réalisée au chantier 5; ils préservent la configuration canonique déjà portée par les children.
