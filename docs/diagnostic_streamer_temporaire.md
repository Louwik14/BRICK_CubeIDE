# Diagnostic temporaire du premier défaut Streamer

Le chemin observé est : `sample_voice_reader_publish_lease` (AUDIO, position et fenêtre)
→ `sample_stream_manager_candidate_for_slot` (STORAGE, round robin)
→ `sample_page_cache_reserve_page_key_alloc` (`RESERVED`)
→ `sample_page_cache_begin_loading` (`LOADING`)
→ `sample_stream_transport_submit` → `sample_stream_io_begin_to`
→ `sample_stream_backend_physical_begin` → ordonnanceur SD
→ `sd_block_device_async_read_submit` → DMA SD → completion
→ `sample_stream_publish_result` → `sample_page_cache_finish_loading` (`READY` ou `FAILED`)
→ `sample_page_cache_audio_resolve_page_key` → curseur ou rendu direct.
Le backend écrit le FLOAT32 directement dans la page finale. Une page stéréo vaut
64 KiB et 8192 frames.

Le run matériel initial a montré que le diagnostic AUDIO restait vide. Le mode
Stream de clip résout sa source par `sample_classic_audio_projection_resolve`
(key CLASSIC, ou REC si la source est un enregistrement), puis appelle
`sample_voice_reader_bind_play_plan` avec un reader physique `2 + track_id`.
Le rendu stéréo 1x Release passe par
`brick6_sampler_runtime_render_stream_fwd_1x_fast` et
`sample_voice_reader_render_fwd_1x_ready_simple`. Ce kernel lit le
`audio_cursor.current_base` déjà acquis et ne traverse pas
`sample_voice_reader_prepare_multi_fwd_1x`, où se trouvait la vérification
précédente. Le filtrage diagnostic limité à `SAMPLE_AUDIO_DOMAIN_MULTI`
rejetait aussi tous les readers de clip. Le diagnostic mappe désormais les
readers de clip 2..9 sur `reader[0..7]` et contrôle le curseur directement
avant la lecture FLOAT32 dans le kernel rapide. À la frontière de page,
`sample_voice_reader_acquire_audio_page` appelle
`sample_page_cache_audio_resolve_page_key` ; son échec est capturé dans ce
kernel. Le chemin segment et son voisin de page sont également capturés.

Pour Multi Sample, `sample_voice_reader_bind_play_plan` reçoit
`BRICK6_SAMPLER_CACHE_VOICE_NONE` (255) ; l'identité physique 0..7 n'arrive
qu'à `sample_voice_reader_bind_loop_cache_incarnation`. Le diagnostic se lie
donc aussi à cette fonction pour ce chemin.

## RAM et lecture GDB

`g_sample_stream_diag` est un symbole global conservé avec LTO. Retrouver son adresse
avec `info address g_sample_stream_diag`, jamais avec une adresse fixe. `magic`
vaut `0x53444731`, `version` vaut 1. Tous les temps sont des cycles DWT modulo
2^32 ; diviser une différence non signée par `cycle_hz` pour obtenir des secondes.
Le tour du compteur arrive après quelques secondes : les différences ne sont
fiables que pour un refill court. Zéro signifie « événement non observé ».

Layout ARM 32 bits, sans pointeurs dans les structures publiées :

| Structure | Taille | Champs et offsets |
| --- | ---: | --- |
| `sample_audio_key_t` | 8 | `domain` 0 (1), `reserved` 1 (1), `object_id` 2 (2), `generation` 4 (4) |
| `sample_stream_diag_t` | 7896 | `magic` 0, `version` 4, `cycle_hz` 8, `frozen` 12, `trace_next` 16, `trace_count` 20, `active_readers` 24, `requests` 28, `reserved` 32, `loading` 36, `ready` 40, `failed` 44, `dma_starts` 48, `dma_completions` 52, `dma_errors` 56, `busy` 60, `queue_full` 64, `reserve_fail` 68, `delayed` 72, `audio_miss` 76, `audio_not_ready` 80, `audio_bad_key` 84, `audio_bad_epoch` 88, `underruns` 92, `max_need_dma` 96, `max_dma_complete` 100, `max_complete_ready` 104, `max_need_ready` 108, `scheduler_owner` 112, `scheduler_pending` 116, `sd_pending` 120, `sd_state` 124, `reader[8]` 128, `first` 1120, `trace[64]` 1380, `boundary[8][2]` 4964, `gate_polls` 7844, `gate_pending` 7848, `service_calls` 7852, `gate_deferred_load` 7856, `gate_acquire_fail` 7860, `first_gate_polls` 7864, `first_service_calls` 7868, `first_gate_pending` 7872, `first_gate_deferred_load` 7876, `first_gate_acquire_fail` 7880, `boundary_slot_mask_lo` 7884, `boundary_slot_mask_hi` 7888, `first_gate_owner` 7892 |
| `sample_stream_diag_reader_t` | 124 | `key` 0, puis mots de 4 octets : `active` 8, `slot` 12, `frame` 16, `current_page` 20, `next_page` 24, `pages_used` 28, `refills` 32, `misses` 36, `not_ready` 40, `underruns` 44, `min_ready_distance` 48, `need_page` 52, `t_need` 56, `t_reserved` 60, `t_dma_start` 64, `t_dma_complete` 68, `t_ready` 72, `t_first_use` 76, `registration_epoch` 80, `page_generation` 84, `dma_owner` 88, `prev_need_page` 92, `prev_t_need` 96, `prev_t_reserved` 100, `prev_t_dma_start` 104, `prev_t_dma_complete` 108, `prev_t_ready` 112, `prev_t_first_use` 116, `prev_dma_owner` 120 |
| `sample_stream_diag_snapshot_t` | 260 | `valid` 0, `cycles` 4, `event` 8, `reader_slot` 12, `key` 16, `page` 24, `frame` 28, `page_state` 32, `prev_state` 36, `next_state` 40, `active_readers` 44, `scheduler_pending` 48, `sd_pending` 52, `sd_state` 56, `sd_owner` 60, `sd_operation` 64, `sd_fault` 68, `sd_irq_error` 72, `refill` 76 (124 octets), `reserved` 200, `loading` 204, `ready` 208, `failed` 212, `dma_starts` 216, `dma_completions` 220, `dma_errors` 224, `busy` 228, `queue_full` 232, `t_need` 236, `t_reserved` 240, `t_dma_start` 244, `t_dma_complete` 248, `t_ready` 252, `t_first_use` 256 |
| `sample_stream_diag_trace_t` | 56 | `sequence` 0, `cycles` 4, `event` 8, `reader_slot` 12, `key` 16, `page` 24, `frame` 28, `page_state` 32, `extra` 36, `active_readers` 40, `scheduler_pending` 44, `sd_pending` 48, `sd_state` 52 |
| `sample_stream_diag_boundary_t` | 180 | `watch_page` 0, `audio_page` 4, `lease_slot` 8, `event` 12, `reason` 16, `key` 20, `storage_key` 28, `audio_cycles` 36, `audio_seq` 40, `audio_result` 44, `audio_r0_first` 48, `audio_r0_count` 52, `audio_r1_first` 56, `audio_r1_count` 60, `storage_cycles` 64, `storage_seq` 68, `storage_ok` 72, `storage_contains` 76, `storage_r0_first` 80, `storage_r0_count` 84, `storage_r1_first` 88, `storage_r1_count` 92, `examined_page` 96, `examined_state` 100, `candidate_cycles` 104, `reserve_cycles` 108, `reserve_result` 112, `storage_reads` 116, `storage_rejects` 120, `candidate_found` 124, `candidate_none` 128, `pending_seen` 132, `reserve_attempts` 136, `storage_contains_reads` 140, `storage_missing_reads` 144, `fault_cycles` 148, `fault_seq` 152, `fault_key` 156, `fault_r0_first` 164, `fault_r0_count` 168, `fault_r1_first` 172, `fault_r1_count` 176 |

`trace_next` est le numéro de la prochaine écriture ; lire les `trace_count`
entrées depuis `(trace_next - trace_count) & 63`. `reader_slot` vaut `UINT32_MAX`
pour les transitions de page sans propriétaire. Pour les événements DMA, `reader_slot`
porte la génération du propriétaire physique et `page` porte le LBA. Le lien
avec une voix est `dma_owner` ou `prev_dma_owner` dans `reader`.

Événements : 1 NEED (réservé, non tracé), 2 RESERVED (non tracé), 3 LOADING
(non tracé), 4 DMA_START, 5 DMA_COMPLETE, 6 DMA_ERROR, 7 READY, 8 FAILED,
9 RESERVE_FAIL, 10 DELAYED/BUSY, 11 QUEUE_FULL, 12 AUDIO_MISS,
13 AUDIO_NOT_READY, 14 AUDIO_BAD_KEY, 15 AUDIO_BAD_EPOCH,
16 AUDIO_UNDERRUN. États de page : 0 FREE, 1 RESERVED, 2 LOADING,
3 READY, 4 EVICTING, 5 FAILED (vérifier l'enum du firmware en cas de changement).

## Bloc GDB unique

L'ELF Release/LTO doit être chargé comme symboles avant ces commandes.

```gdb
shell cls
set pagination off
set print pretty on
set print elements 0
info address g_sample_stream_diag
ptype /o g_sample_stream_diag
ptype /o g_sample_stream_diag.reader[0]
ptype /o g_sample_stream_diag.first
ptype /o g_sample_stream_diag.trace[0]
set $d = &g_sample_stream_diag
p $d->magic
p $d->cycle_hz
p $d->frozen
p $d->active_readers
p $d->requests
p $d->reserved
p $d->loading
p $d->ready
p $d->failed
p $d->dma_starts
p $d->dma_completions
p $d->dma_errors
p $d->busy
p $d->queue_full
p $d->reserve_fail
p $d->delayed
p $d->max_need_dma
p $d->max_dma_complete
p $d->max_complete_ready
p $d->max_need_ready
p $d->reader
p $d->first
set $i = $d->trace_next - $d->trace_count
while $i < $d->trace_next
  p $d->trace[$i & 63]
  set $i = $i + 1
end
```

## Audit de la frontière AUDIO → STORAGE (deuxième run)

Le `t_need` antérieur était écrit **avant** l'appel à
`sample_page_lease_audio_publish` : il prouvait le calcul du besoin, pas sa
présence dans `g_sample_page_leases`. Le nouveau `boundary[reader][0]` garde
le besoin courant et `[1]` le précédent, donc la page N+1 au moment où AUDIO
publie déjà N+2. Ces enregistrements se figent au premier défaut. Ils
conservent les ranges proposés, le résultat et la séquence de publication,
le dernier snapshot STORAGE, le nombre de lectures contenant la page, les
candidats et les tentatives de réservation. `first_gate_*` capture le worker
au premier défaut.

La publication seqlock écrit key, epoch et ranges entre les séquences impaire
et paire. `sample_page_lease_control_read` tente trois snapshots et retourne
zéro si `seq == 0`, si une écriture est en cours, si la séquence change, ou
si `ranges[0].page_count == 0`. Si le snapshot valide contient N+1 et que N
est READY, `candidate_for_slot` examine N puis N+1 ; N+1 FREE devient
immédiatement candidat. Le gate `sample_stream_manager_has_pending_sd_work`
répond alors vrai. `scheduler_pending == 0` mesure seulement les I/O manager
déjà soumises ; il ne prouve pas que le gate ait vu le lease.

Défaut statique précis du chemin Multi Sample : `sampler_multi_voice.inc`
passe `BRICK6_SAMPLER_CACHE_VOICE_NONE == 255` à
`sample_voice_reader_bind_play_plan`. `sample_page_lease_multi_slot(255)`
calcule `(uint8_t)(16 + 255) == 15`, slot encore considéré valide. Les huit
readers Multi peuvent donc publier sur **le même slot 15** ; chaque publication
d'une autre voix écrase le lease précédent. Le reader victime ne republie pas
tant que ses `ranges[2]` locaux restent identiques : le `memcmp` compare
uniquement sa copie locale, pas le contenu du slot partagé. Le besoin N+1
peut ainsi rester absent de STORAGE pendant toute la page N. L'identité
physique 0..7 n'est
affectée au diagnostic qu'ensuite, par
`sample_voice_reader_bind_loop_cache_incarnation`. Ce chemin explique le
besoin AUDIO calculé et une page FREE jamais candidate **si
`first.key.domain == 1`**. Le domaine de la key du run fourni n'étant pas
indiqué, cette cause reste à confirmer sur matériel. Aucun correctif de lease.

Événements `boundary.event` : 1 AUDIO_PUBLISH, 2 STORAGE_READ, 3 READ_REJECT,
4 READY_SKIP, 5 LOADING_BLOCK, 6 PENDING_ONLY, 7 CANDIDATE_FOUND,
8 CANDIDATE_NONE, 9 RESERVE_ATTEMPT, 10 RESERVE_FAILED, 11 RESERVED,
12 OTHER_CANDIDATE, 13 NO_WORK, 14 EARLIER_LOADING. Sur rejet de lecture,
`reason` vaut 1 (seq nul), 2 (seq impair), 3 (snapshot modifié) ou 6
(snapshot stable mais range 0 vide). Après lecture valide, `storage_contains == 0` avec
`reason == 4` signifie même key mais page absente ; `reason == 5` signifie
key différente. `storage_contains_reads` indique si STORAGE a jamais vu la
page dans ce lease ; `candidate_found` et `reserve_attempts` séparent
sélection et réservation. `first_gate_pending` croissant sans progression de
`first_service_calls` indique une admission bloquée ; vérifier
`first_gate_deferred_load` et `first_gate_acquire_fail`. Les champs `fault_*`
copient le lease brut au premier défaut, même si la lecture STORAGE a échoué.

Dump minimal du prochain run après chargement de l'ELF Release/LTO :

```gdb
shell cls
set pagination off
set print pretty on
info address g_sample_stream_diag
set $d = &g_sample_stream_diag
p $d->first
p $d->reader[$d->first.reader_slot]
p $d->boundary[$d->first.reader_slot]
p $d->first_gate_polls
p $d->first_gate_pending
p $d->first_service_calls
p $d->first_gate_deferred_load
p $d->first_gate_acquire_fail
p $d->first_gate_owner
```

## Run matériel et lecture

Reset, lancer 7 voix, halt bref et vérifier `magic`, `active_readers == 7`,
`frozen == 0`, puis relancer. Lancer la 8e voix jusqu'au bruit, halt et exécuter
le bloc GDB. Ne pas refaire `sample_stream_manager_init` entre ces étapes.

Le `first` est figé au premier défaut audio, et la trace s'arrête à ce point ;
les compteurs globaux et les `reader` continuent à évoluer. `first.valid == 1`
et `frozen == 1` prouvent la capture. Le `first.refill` et ses `t_*` identifient
le refill visé. Les compteurs `reader[i]` révèlent si une seule voix diverge.

- `first.t_need` proche du défaut et faible distance READY : demande tardive.
- Écart `t_need → t_dma_start` élevé, `scheduler_pending` ou `queue_full` élevé :
  admission/ordonnanceur ou file saturée. Comparer les `reader` pour repérer
  une voix systématiquement non servie.
- `reserve_fail` élevé, pages `reserved/loading/ready` et trace `RESERVE_FAIL` :
  pression du cache ou impossibilité de réserver.
- Écart `t_dma_start → t_dma_complete` élevé et SD active : latence/bande
  passante SD ; DMA active sans completion : blocage DMA ou block device.
- Completion présente sans `READY`, ou `FAILED` : erreur de publication,
  token, média ou lecture. `dma_errors`, `sd_fault`, `sd_irq_error` et trace
  donnent le contexte.
- Page `READY` mais événement `BAD_KEY`/`BAD_EPOCH` : curseur/pointeur sur
  page recyclée ou descripteur changé. `NOT_READY` sur curseur déjà acquis
  indique une perte de protection du lease. `page_state` et les états voisins
  montrent si la frontière de page est en cause.

Limites : `t_need` est la publication du prochain besoin dans le lease, pas la
première lecture audio ratée ; seuls le refill courant et le précédent de chaque
reader sont conservés. Une page initiale préchargée peut ne pas avoir `t_need`.
`t_dma_complete` est la dernière completion des morceaux DMA d'une page.
`min_ready_distance` mesure la distance au bord de la page courante READY, sans
inclure une éventuelle page suivante READY. Si `first.valid` reste nul malgré
le bruit, la corruption est hors des défauts actuellement détectés ; vérifier
alors le contenu FLOAT32 de la page et l'intégrité DMA à une étape suivante.
