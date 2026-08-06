# Audit Multi streamée 3/6 pages

## Conclusion courte

Le HEAD courant applique bien le contrat 3/6 : mono = 3 pages, stéréo = 6 pages, soit 12 pages au maximum par voix avec une fenêtre current et une fenêtre loop. Je n’ai pas identifié d’accès hors tableau ni de limite active restée à 2/4 dans le chemin Multi. Le point le plus suspect est la combinaison admission partielle / pending / locks, qui retourne souvent simplement `0` et peut laisser l’incident visible seulement plus tard comme un underrun qui stoppe la voix.

## Chemin runtime audité

1. `brick6_sampler_runtime_trigger_multi_note_velocity_token()` résout instrument, zone et sample, vérifie l’état READY de la page 0, alloue une entrée dans `g_sampler_multi_voice[8]`, construit le `sample_play_plan_t`, lie le reader et prend le slot DSP.
2. Il crée un owner current `SAMPLE_STREAM_OWNER_MULTI_VOICE` avec `owner_id = index de voix` et `owner_generation = trigger_order`, puis appelle `sample_stream_manager_reserve_active_pages()`. La fenêtre contient `lookahead_pages + 1`, calculé comme `sample_audio_format_window_pages(format)`, donc 3 mono ou 6 stéréo.
3. En boucle forward, il réserve ensuite une seconde fenêtre avec `SAMPLE_STREAM_OWNER_MULTI_LOOP`, même index et même génération. Les deux réservations sont séquentielles et un échec libère l’owner concerné puis le current déjà acquis.
4. `sample_stream_manager_reserve_active_pages()` demande chaque page dans le page cache, crée ou met à jour le pending, puis prend le window lock. `queue_active_pages()` reprend ce travail après le déclenchement, libère les pages hors fenêtre, acquiert les nouvelles et les associe au pending.
5. `sample_stream_manager_service()` choisit les pending par deadline/priorité, valide format, stride, nombre de frames et `registration_epoch`, charge par backend contigu ou reader FatFs, puis fait passer la page à READY. Une erreur de chargement devient ERROR et interrompt cet appel de service.
6. `sample_voice_reader_begin_segment()` acquiert la page courante et, pour l’interpolation, un voisin. Le reader ne conserve donc directement que current + neighbor, avec `sample_page_ref_t` contenant slot, generation, format et epoch.
7. `sample_voice_reader_commit_segment()` avance la position, libère le curseur devenu obsolète et acquiert la page suivante à la frontière. Les owners de fenêtre restent contractualisés jusqu’à la libération différée.
8. EOF, underrun, état de segment invalide, vol de voix, note-off/VCA et invalidation explicite passent par `brick6_sampler_runtime_multi_stop_voice()` ou la libération différée hors IRQ. Les arrêts audio marquent d’abord l’owner à libérer ; `brick6_sampler_runtime_service()` effectue la libération hors IRQ.

## Comparaison avec l’état avant `6e5cdbbb1`

`6e5cdbbb1` est le changement qui porte le contrat de `MIN_READY_FRAMES=8192` à `12288`, donc mono 2→3 et stéréo 4→6. Il augmente aussi le cache à 1504 pages (24 MiB) et le budget de préparation Multi de 8 à 20 MiB. Le parent avait un cache de 20 MiB, soit 1280 pages.

Les limites et structures du chemin courant sont cohérentes avec 3/6 :

- `SAMPLE_PAGE_MULTI_WINDOW_PAGES = 6` stéréo ; `SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES = 5`.
- `SAMPLE_PAGE_WINDOW_LOCK_MAX = 8 * 6 * 2 = 96` locks, pour current + loop de 8 voix.
- `SAMPLE_STREAM_DYNAMIC_PENDING_MAX = 16 * 6 * 2 = 192`; plafond par owner = 6 ; tableau pending = 1504.
- readers actifs = 16 ; voix Multi globales = 8 ; réserve page-window = 96 pages ; marge produit = 128 pages ; slot pool général = 1280 pages.
- `ready_mask` est un `uint32_t` et ne reçoit ici que le nombre de pages demandé ; 6 bits restent dans la capacité du type. Aucun tableau de fenêtre existant n’est dimensionné à 4 dans ce chemin.

Les changements après `6e5cdbbb1` visibles dans ces fichiers ne modifient pas le streaming Multi ; ils concernent principalement le chemin mono/clip et le global pool.

## Points contrôlés

- Mono/stéréo : le nombre de frames/page, le stride, le format et la fenêtre dérivent de `sample_audio_format_t`. Le slot physique reste 16 KiB ; mono utilise 4096 frames × 1 float, stéréo 2048 × 2 floats.
- Current + loop : les owners sont distincts. Une page commune aux deux fenêtres consomme deux relations d’owner ; il n’y a pas de déduplication inter-owner.
- Générations : les locks sont identifiés par kind/id/generation ; les refs du reader vérifient generation, format et epoch avant réacquisition.
- Libération : la page cache refuse de recycler une page contractualisée ou référencée. `clear_desc()` efface les locks d’un slot uniquement lors de son recyclage après cette protection.
- Capacité : le cache et les réserves acceptent plusieurs fenêtres froides ; la limite la plus basse logique est le budget pending dynamique/owner et la disponibilité de slots du voice-window pool, pas le nombre de readers.
- Partial acquisition : `reserve_active_pages()` rollbacke l’owner sur échec. En revanche, `queue_active_pages()` conserve le lock de la page courante jusqu’à son déplacement hors fenêtre lorsqu’une demande pending échoue ; ce comportement mérite confirmation sur cible.
- STOP silencieux : un échec de `begin_segment`, un segment UNDERRUN/invalide ou un reader devenu inactif déclenche le stop Multi sans raison matérielle détaillée dans le runtime existant. C’est le point d’instrumentation prioritaire.
- Assertions : les `_Static_assert` couvrent surtout les tailles de pool et la géométrie. Les invariants runtime de pending/locks et les résultats de chargement sont principalement des retours `0` ; en Release ils ne fournissent pas de diagnostic exploitable.

## Suspects classés

1. Admission ou renouvellement partiel : échec de `sample_stream_manager_request_page_with_priority_key()`, `note_pending_key()` ou `sample_page_cache_acquire_window_page_key()` puis rollback/retour `0`, avec état pending/lock à corréler.
2. Pending saturé ou owner mal réattribué : plafonds 6 par owner et 192 global, déduplication par clé/page, puis transfert d’owner lors de la libération. Une page partagée current/loop peut consommer deux locks et deux relations de pending.
3. Page réellement non chargée : `sample_stream_manager_service()` passe en ERROR et quitte immédiatement sur un échec d’ouverture, lecture, décodage ou validation format/epoch ; le reader ne voit ensuite qu’un underrun et stoppe la voix.
4. Libération différée / renouvellement de génération : architecture globalement protégée, mais le moment exact du renouvellement doit être comparé au snapshot si un reader conserve encore une ref.
5. Capacité brute : moins probable avec 96 locks, 96 pages réservées et 16 readers, mais à vérifier sur cible avec `locks_used`, `pages_free` et les états des pages au premier échec.

## Instrumentation ajoutée

Sous `BRICK6_MULTI_STREAM_DIAG` uniquement :

- `Inc/Sampler/sample_multi_stream_diag.h` définit le snapshot et les codes d’échec.
- `Src/Sampler/sample_stream_manager.c` capture les échecs de request/pending/lock et expose le breakpoint différé.
- `Src/Core/brick6_sampler_runtime.c` capture admission page 0, bind reader, reserve current/loop, begin segment et statut de segment.
- `Src/Sampler/sample_page_cache.c` fournit, uniquement sous macro, l’état des pages, locks utilisés et pages EMPTY.
- `Src/Core/crash_capsule.c` ajoute CFSR/HFSR/BFAR/MMFAR et la frame empilée au même snapshot lors d’un fault.
- `CMakeLists.txt` expose `BRICK6_MULTI_STREAM_DIAG`, OFF par défaut.

Le snapshot est `volatile`, figé par `g_sample_multi_stream_diag_frozen`, et capturé une seule fois. La fonction `sample_multi_stream_diag_capture_failure()` est `noinline`; aucun `printf`, transport, allocation ou log continu n’est ajouté. Le service hors IRQ appelle ensuite `sample_multi_stream_diag_breakpoint()` une seule fois. Taille mesurée dans le ELF Debug : `0x190 = 400` octets pour `g_sample_multi_stream_diag`, plus 4 octets pour le flag et 4 octets pour l’état interne du breakpoint.

## GDB

```gdb
break sample_multi_stream_diag_breakpoint
continue

p/x g_sample_multi_stream_diag_frozen
p/x g_sample_multi_stream_diag
p/x g_sample_multi_stream_diag.code
p/x g_sample_multi_stream_diag.failure_result
p/x g_sample_multi_stream_diag.sample_id
p/x g_sample_multi_stream_diag.voice_index
p/x g_sample_multi_stream_diag.current_generation
p/x g_sample_multi_stream_diag.loop_generation
p/x g_sample_multi_stream_diag.pending_global
p/x g_sample_multi_stream_diag.pending_current_owner
p/x g_sample_multi_stream_diag.pending_loop_owner
p/x g_sample_multi_stream_diag.locks_used
p/x g_sample_multi_stream_diag.readers_active
p/x g_sample_multi_stream_diag.pages_free
p/x g_sample_multi_stream_diag.position_frame
p/x g_sample_multi_stream_diag.pc
p/x g_sample_multi_stream_diag.lr
p/x g_sample_multi_stream_diag.current_pages[0]@6
p/x g_sample_multi_stream_diag.loop_pages[0]@6

set $vi = (int)g_sample_multi_stream_diag.voice_index
p g_sampler_multi_voice[$vi]
p g_sampler_multi_voice[$vi].reader
p g_sampler_multi_voice[$vi].reader.audio_cursor.current_page_ref
p g_sampler_multi_voice[$vi].reader.audio_cursor.neighbor_page_ref
p/x g_sampler_multi_voice[$vi].stream_state
p/x g_sampler_multi_voice[$vi].loop_stream_state

set $slot = (int)g_sample_multi_stream_diag.current_pages[0].slot_index
p/x g_sample_page_desc[$slot]

bt
bt full
info registers
p/x *(volatile unsigned int *)0xE000ED28
p/x *(volatile unsigned int *)0xE000ED2C
p/x *(volatile unsigned int *)0xE000ED34
p/x *(volatile unsigned int *)0xE000ED38
```

Les quatre adresses SCB affichent respectivement CFSR, HFSR, MMFAR et BFAR. En cas de fault, `pc/lr` et `stacked_pc/stacked_lr` sont conservés dans le snapshot ; les valeurs empilées sont prioritaires pour la backtrace du fault.
