# Audit Storage Current Findings — V3

Date de l'audit : 2026-09-06

Audit relance integralement sur l'etat courant du repository. Aucun patch code et
aucun build n'ont ete executes.

## VERDICT

**NEW FINDINGS**

## NEW FINDINGS

### F-01 — Facade Storage globale active dans Settings Storage

`Inc/Storage/settings_storage_service.h` expose dans une meme API les requetes
et projections du catalogue, des loads Classic/RAM/Wavetable/Multi, de la
Preview et de la conversion. `Src/Storage/settings_storage_service.c` garde
les etats pending, une file d'evenements et une orchestration inter-owners via
`storage_settings_service_owner()` ; le dispatcher l'appelle pour STREAM,
SAMPLE_RAM, WAVETABLE, MULTI, CATALOG et WAV_CONVERT
(`Src/App/brick6_app_init.c:255-324`).

L'enum conserve bien exactement 12 owners et ce service n'est pas une 13e owner
ni une generic job queue. Il reste toutefois une facade Storage globale active,
contraire a la contrainte d'architecture figee.

### F-02 — Preview refuse la concurrence live avec Stream et Recorder

`Src/Storage/sd_preview.c:638-642` refuse directement le demarrage lorsque
Recorder est actif. `Src/Storage/sd_preview.c:789-796` place PREVIEW en
`WAIT_RESOURCE` lorsque la gate ou `streaming_critical` est active, puis
`Src/Storage/sd_preview.c:823-827` le remet en attente pendant une lecture
Stream en vol ou un owner STREAM runnable.

Le contrat courant demande que Preview reste live pendant le playback et que les
services live coexistent avec Recorder. Le comportement actuel transforme cette
concurrence en refus/attente durable ; ce n'est pas un simple cas de completion
non consommee.

### F-03 — Waveform/cache de presentation bloque par les transports live

Les chemins de construction et de lecture de tiles de
`Src/Storage/waveform_cache.c` contiennent les memes barriers :

- `:1333-1340` attend Stream, Recorder, Preview ou Pattern avant le build ;
- `:1514-1521` attend les memes conditions avant une tile de presentation.

Le cache persistant lourd peut etre differe. En revanche, le chemin tile et la
presentation waveform de Sample Capture utilisent ce owner WAVEFORM_CACHE et
restent bloques pendant Stream/Recorder, contrairement au contrat live.

### F-04 — Browsing/catalogue leger bloque par la protection playback

`Src/Storage/wav_loader.c:565-568` refuse l'ouverture de lecture du catalogue
quand `streaming_critical` est actif. Les vues et scans du catalogue passent
egalement par l'admission background (`Src/Storage/wav_loader.c:672-682` et
`Src/Storage/storage_catalog.c:108-114`), qui remet l'owner en attente lorsque
le scheduler a un provider temps reel actif
(`Src/SD/sd_scheduler_runtime.c:161-172`).

Le rebuild complet reste correctement STOPPED-only. Les lectures de vue, lookup
et browsing/cache leger ne restent toutefois pas live sous playback/Recorder.

### F-05 — Fallback FatFs Stream execute hors gate SD

Le fallback non physique de `Src/Sampler/sample_stream_io.c:284-312` execute
directement `f_open`, `f_lseek`, `f_read` et `f_close` sans acquerir
`sd_access_gate`.

Dans le dispatcher courant, `sample_stream_transport_worker_poll()` est appele
avant `sample_cache_service()` (`Src/App/brick6_app_init.c:243-253`). La gate
`SD_ACCESS_CLIENT_SAMPLE_STREAM` est tenue par `sample_cache_service()` autour
du service Stream, mais elle est relachee avant le prochain poll du worker. Le
fallback peut donc toucher FatFs sans ownership SD protege, en dehors du
scheduler physique et de la gate.

### F-06 — Evenement de readmission SD absent du chemin courant

`sd_access_media_set_present()` (`Src/Storage/sd_access_gate.c:169-195`)
reinitialiserait l'etat et reveillerait les owners, mais aucune utilisation
courante n'est presente. `Src/SD/sd_io_hooks.c:13-16` confirme l'absence de
card-detect GPIO et limite la detection a l'etat de derniere initialisation BSP.
`sd_access_storage_report_init_failure()` et `sd_access_fs_invalidate_mount()`
latched respectivement `NO_MEDIA`/`FAULT` sans publier de readmission.

Il n'y a pas de retry periodique NO_MEDIA, ce qui est conforme. Mais apres une
reinsertion physique, une nouvelle action explicite ne dispose d'aucune
transition courante pour sortir de `NO_MEDIA`/`FAULT`; le chemin documente d'une
nouvelle admission apres insertion n'est donc pas realise.

## CONTRACT-SIMPLIFIED CASES

- Les guards playback supplementaires dans Multi Load/Import
  (`Src/Sampler/multi_sample_loader.c`, `Src/Sampler/multi_sample_import.c`),
  WAV Convert (`Src/Storage/wav_convert.c`) et rebuild catalogue
  (`Src/Storage/wav_loader.c`) sont redondants avec STOPPED-only. Ils ne
  constituent pas un nouveau besoin de concurrence.
- `SD_BLOCK_DEVICE_CARD_READY_RETRY_MS` (`Src/SD/sd_block_device.c:17`) est une
  relance courte de disponibilite materielle apres validation d'un media READY;
  elle n'est pas un retry periodique NO_MEDIA. Aucun retry NO_MEDIA periodique
  n'a ete trouve.
- Les chemins block-device invalident l'epoch, abortent la transaction et
  publient une completion d'erreur sur retrait/defaut SD. Aucune reprise
  automatique d'operation n'a ete trouvee.

## OVERARCHITECTURE

La protection active de Stream/Recorder dans les operations Multi, Convert,
rebuild catalogue et cache persistant ajoute de la complexite pour une
concurrence interdite ou non requise par STOPPED-only. Elle ne justifie ni
quanta cooperatifs, ni nouveau scheduler, ni nouveaux etats, ni reprise
automatique SD.

## AUDIT SANS NOUVEAU FINDING CONFIRME

- **Dispatcher / owners :** exactement 12 owners sont declares et dispatches
  separement ; aucune 13e owner, generic job queue ou scan metier global n'a
  ete confirmee.
- **Runnable / WAIT / completion :** les completions block-device portent
  l'owner de la requete et republient cet owner ; les releases de gate/scheduler
  republient les owners en attente. Aucun mauvais owner reveille ou completion
  perdue n'a ete confirme dans les chemins audites.
- **SD absente avant admission :** les admissions STOPPED-only testees refusent
  avant creation de job/mutation ; le feedback UI utilise `SD ABSENTE` sur les
  actions de browser/loads concernees.
- **SD retiree pendant operation :** epoch, statut, abort et nettoyage convergent
  vers erreur ; aucune reprise automatique n'a ete trouvee.
- **Scheduler / Stream / Recorder :** un seul scheduler physique arbitre READ,
  WRITE et FILESYSTEM ; la coexistence Recorder/Stream est serialisee par ce
  scheduler et la gate. Le finding FatFs F-05 concerne uniquement le fallback
  Stream non physique.
- **Sample Capture :** le chemin audio live reste separe et son chemin de
  presentation waveform est couvert par F-03.

