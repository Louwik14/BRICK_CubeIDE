# Audit Z2 — autorité `track_runtime`

Branche auditée : `main_doublemcu_monocore`. Audit statique exclusivement ; aucun build, test ou fichier existant n'a été modifié.

## 1. Verdict Z2

L'architecture d'autorité est globalement saine : `track_topology` porte l'identité/capacité structurelle, `track_state` la configuration mutable, `track_input_ownership` la réservation External et `synth_polyphony` les slots de voix. `track_runtime` est bien l'unique projection qui résout engine, instance et lane mixer.

Cette projection n'est toutefois pas publiable en l'état : elle est reconstruite et consommée dans la même table mutable, y compris depuis le chemin audio. La dette est P0 monocœur avant même toute question dual-core.

## 2. Statut final de `Z2-001`

**CONFIRMÉ, aggravé.** La conclusion du registre est correcte : `g_track_runtime_ctx` et sa map inverse sont publiés en place, sans snapshot, verrou, génération acquittée ni barrière de publication. Une IRQ peut donc voir un mélange de générations.

Nuance importante : le registre affirme que la garde IPSR interdit le refresh depuis IRQ. C'est vrai pour `track_runtime_refresh_if_dirty()` et pour le chemin global de `track_runtime_refresh_track()`, mais faux pour son chemin dirty local. Quand seul `g_track_runtime_track_dirty[track]` est posé, `track_runtime_refresh_track()` reconstruit, réalloue et réapplique depuis IRQ. `brick6_audio_runtime_dsp()` appelle `mod_lfo_v1_process_block()`, qui appelle ce refresh pour chaque track ayant une route Matrix configurée.

## 3. Carte : autorité canonique → projection → consommateurs

| Autorité canonique | Projection Z2 | Consommateurs directs observés |
|---|---|---|
| `track_topology` : rôle, tracks actives, capacités, entrée physique Special | rôle/capacités dans descriptor ; garde `track_runtime_has_capability()` | Z3 Param, Z4 scheduler/capture, Z5 pages/navigation/mute, Z6 restore |
| `track_state` : family, type, canal et source MIDI | `g_track_runtime_ctx[]` : family/type/flags/engine/instance/bind/lane/MIDI | Z1 rendu audio et mixer ; Z3 backends ; Z4 clavier/scheduler ; Z5 ; Z6 |
| `track_input_ownership` : sélection et owner External | lane External et `track_runtime_is_audio_routable()` ; map inverse lane → track | Z1 rendu/mixer, Z3 MIX/FILTER, Z5 mute/CFG |
| `synth_polyphony` : slots, voix, spread, note/release | `instance_id` de base et état `bind_state` synth/drum | Z1 moteurs Prism/Stack/Wave/Deluge/Drum, Z3 paramètres voix, Z4 notes |
| `track_runtime` : décision de binding | ctx, `g_track_runtime_logical_track_by_mix_track[]`, synth usage, revisions | Z1 IRQ, Z3 apply, Z4 note/scheduler, Z5 projection, Z6 restore/paste |

`track_runtime` ne double donc pas l'autorité métier précédente ; il la recalcule. En revanche il mélange actuellement **construction de projection**, **mutation des ressources Z1** et **publication aux lecteurs**, ce qui casse sa frontière.

## 4. Preuves par chemins et symboles

### Reconstruction et publication

- `Src/Core/track_runtime.c::track_runtime_refresh_all()` écrit successivement chaque entrée de `g_track_runtime_ctx`, réserve les lanes, appelle `track_runtime_bind_ctx()`, prépare les remplacements Looper, réapplique les backends tone, écrit `g_track_runtime_synth_usage`, reconstruit `g_track_runtime_logical_track_by_mix_track`, puis seulement incrémente `g_track_runtime_revision`.
- `track_runtime_bind_ctx()` appelle directement `synth_polyphony_set_track_active()` / `set_voice_count()`. Ces chemins réinitialisent les moteurs et des slots mixer : la construction n'est pas une opération pure sur une table locale.
- `track_runtime_refresh_track()` construit seulement `next_ctx`, mais le publie par affectation dans `g_track_runtime_ctx[track]` avant les resets/reapply, puis reconstruit la map inverse et la synth usage. Les autres entrées sont simultanément consultées pour les allocations.
- Ni ctx, map inverse, synth usage ni revisions ne sont `volatile`; aucune exclusion IRQ ni protocole seqlock/double-buffer n'encadre leur publication. Les revisions sont écrites à la fin et ne sont lues que par `Src/UI/pages/ui_page_template_env.c`; elles ne protègent aucun consommateur audio.

### Lecteurs IRQ

- `Src/Audio/audio.c` appelle le callback enregistré `brick6_audio_runtime_dsp`; `Src/Core/brick6_audio_runtime.c` appelle `track_runtime_refresh_if_dirty()` puis lit `track_runtime_get_ctx()` dans les sept boucles de rendu synth/sampler/looper/Prism/Stack/Wave/Deluge.
- Ces boucles combinent `ctx->engine`, `instance_id`, `type` et `mix_track_id` avec `track_runtime_is_audio_routable()`, lequel relit ctx puis `track_input_ownership`.
- `Src/Audio/mixer.c::mixer_track_is_looper()` et `mixer_snap_track_runtime_state()` lisent ctx et la map inverse dans le chemin mixer.
- `Src/Core/brick6_audio_runtime.c::brick6_audio_runtime_dsp()` appelle `mod_lfo_v1_process_block()`. `Src/Mod/mod_lfo_v1.c::mod_lfo_v1_process_block()` appelle `track_runtime_refresh_track(track)` avant de lire ctx : c'est le chemin démontré de reconstruction locale en IRQ.

### Lecteurs hors IRQ et mutations

- Z3 : `param_registry*.c`, transition family/type, destinations MOD et backends utilisent ctx/résolveurs, en général après un refresh explicite.
- Z4 : `keyboard_engine.c`, `seq_play_scheduler.c`, `seq_param_iface.c`, live-record et output guard font de même. Le scheduler peut aussi atteindre son refresh pendant la collecte audio de `Src/Audio/audio.c` via `seq_runtime_audio_collect_block_events()` → `seq_runtime_exec_collect_block_events()` → scheduling.
- Z5 : `ui_core_runtime_bridge.c` passe par `track_state_set_track_family/type()` puis le system-sync qui invalide ; navigation, pages, mute et clipboard appellent ensuite un refresh explicite.
- Z6 : `track_snapshot.c`, `patch_v1.c` et les restores Pattern/Project passent par bulk mutation, invalidation et refresh. Undo/Redo applique `pattern_live_apply_snapshot()` dans `undo_v2.c`, donc suit le même restore structurel plutôt qu'une copie directe de ctx.

## 5. Bugs confirmés

### `Z2-001` — publication en place (P0)

Un refresh global rend observables, dans cet ordre, des ctx remis à zéro et reconstruits track par track, des allocations de lanes/voix nouvelles, des moteurs réinitialisés, puis la map inverse et enfin la révision. Une IRQ préemptant le main peut sélectionner une nouvelle lane avec une ancienne map inverse, ou un engine/instance/lane d'étapes différentes. Un refresh local a le même défaut entre l'écriture de ctx, les resets/reapply et les projections dérivées.

Le pointeur renvoyé par `track_runtime_get_ctx()` a une adresse stable mais aucune durée de validité de contenu : il peut être modifié dès qu'un autre contexte lance un refresh. `const` interdit seulement l'écriture par le consommateur. Il ne peut ni être conservé au-delà d'une opération immédiate ni servir de snapshot, y compris dans une boucle IRQ.

`track_input_ownership_apply_bulk()` publie de son côté `g_external_input` puis `g_external_owner`, avant le commit `track_state` qui déclenche l'invalidation. Pendant cette fenêtre, `track_runtime_is_audio_routable()` croise ancien ctx et nouveau ownership : l'ancienne ou la nouvelle source peut être coupée avant la publication du binding. C'est une manifestation supplémentaire de `Z2-001`, pas une seconde autorité de réservation.

### `Z2-002` — refresh local exécuté en IRQ (P0, nouveau)

- **Preuve :** `mod_lfo_v1_process_block()` est appelé par `brick6_audio_runtime_dsp()` et appelle `track_runtime_refresh_track(track)`. Si `g_track_runtime_global_dirty == 0` mais `g_track_runtime_track_dirty[track] != 0`, la fonction n'inspecte pas IPSR et exécute `track_runtime_prepare_ctx_base`, allocation lane, `track_runtime_bind_ctx`, resets moteur, reapply paramètre, recompute synth usage et map inverse.
- **Impact :** travail non borné au sens du contrat audio et mutation de ressources/mappings au milieu du bloc ; le compteur `g_track_runtime_refresh_in_irq_count` ne couvre pas ce cas.
- **Action :** à corriger avec `Z2-001`, en séparant construction hors IRQ et consommation audio d'une génération publiée ; ne pas simplement ajouter un garde IPSR qui laisserait une projection indéfiniment sale sans protocole de service main.

## 6. Dette et code mort démontrés

- `track_runtime_get_ctx()` est une escape hatch largement utilisée par Z1/Z3/Z4, pas du code mort. Sa surface brute est la dette principale : elle diffuse un pointeur vers la table publiable en place.
- Les compteurs publics `g_track_runtime_refresh_all_count`, `g_track_runtime_refresh_in_irq_count` et `g_track_runtime_refresh_track_count` sont écrits mais aucun lecteur in-tree n'a été trouvé. Les retirer n'est pas encore un nettoyage automatique : ils peuvent être des sondes debugger/out-of-tree.
- `TRACK_RUNTIME_FAMILY_INPUT` et `TRACK_RUNTIME_TYPE_LOOPER` restent nécessaires à la représentation runtime des Special Input et Looper ; ce ne sont pas des reliquats supprimables. De même, les aliases capacité ARP/MIDI FX ne sont pas retirables dans cet audit.
- Aucun chemin compilé `runtime_target` ou `Hybrid` n'a été trouvé dans Z2. Aucun code mort Z2 supplémentaire n'est démontré avec une preuve suffisante.

## 7. Divergences documentaires locales

- `z2_track_runtime_authority.md` affirme que `g_track_runtime_ctx` est écrit par `refresh_all/init` seulement ; `refresh_track()` l'écrit aussi.
- Il nomme `g_track_runtime_refresh_needed`, alors que le code emploie `g_track_runtime_global_dirty` et `g_track_runtime_track_dirty[]`.
- Son contrat de refresh local décrit correctement le refus de full refresh global en IRQ, mais omet que le dirty local est exécuté en IRQ. Il est donc insuffisant et potentiellement trompeur pour le chemin P0.
- Les sections historiques présentant Looper comme type configurable doivent être lues avec les addendums Special/topologie : le code actuel représente Looper/Input dans `track_state` pour les Special fixes, sans les exposer comme choix CFG Play.

## 8. Nettoyages sûrs

- Aucun retrait code sûr n'est recommandé sans patch séparé et vérification des consommateurs debugger/out-of-tree.
- Nettoyage documentaire sûr, après correction du runtime : corriger les trois assertions locales ci-dessus et déclarer explicitement la durée de validité de `track_runtime_get_ctx()`.

## 9. Refactors nécessaires mais risqués

1. Extraire une construction pure de binding dans un snapshot local complet : ctx forward, map inverse, synth usage et décision d'allocation doivent appartenir à une même génération.
2. Isoler de cette construction les mutations des moteurs, mixer, Looper et polyphonie ; elles nécessitent une phase de transition bornée avec ownership explicite.
3. Publier une génération cohérente à une frontière block audio, avec contrat de lecture par valeur ou handle/génération ; interdire les pointeurs ctx conservés.
4. Faire entrer ownership External et binding/lane dans la même transaction visible au lecteur audio.

Ces travaux touchent Z1/Z3/Z4/Z5/Z6 ; une désactivation globale des IRQ serait un contournement risqué et ne préparerait pas le futur split.

## 10. À reporter au véritable portage dual-core

- Choix du propriétaire de la génération de binding par cœur, protocole IPC/mailbox, mémoire partagée et maintenance cache.
- Politique de synchronisation inter-cœur des allocations synth, ownership input, mutations moteur et diagnostic.
- Placement mémoire définitif des snapshots et budget de copie mesuré sur H747.

Le correctif monocœur doit seulement fournir le seam publication/consommation ; il ne doit pas introduire une façade dual-core spéculative.

## 11. Tickets Z2 nouveaux avec preuve forte

| ID | Priorité | Statut | Résumé |
|---|---|---|---|
| `Z2-002` | P0 | CONFIRMED | `track_runtime_refresh_track()` reconstruit localement depuis l'IRQ via `mod_lfo_v1_process_block()` lorsque seul le dirty local est posé. |

`Z2-001` reste le ticket de publication transactionnelle parent ; aucun autre ticket Z2 n'est créé, les incohérences ownership/reverse-map et pointeurs en sont des manifestations directes.
