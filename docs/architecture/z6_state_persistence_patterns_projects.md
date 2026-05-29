# Z6 - State / Persistence / Patterns / Projects

## 1. Perimetre

Addendum 2026-05-28 - formats REC START:
- `PatternSaveV1.globals.rec_start_mode` stocke le contrat REC `START` (`DEFAULT/TRIG/ROLL 1/4/ROLL 1/2/ROLL 1`) a la place de l'ancien champ REC launch/count-in.
- `PATTERN_VERSION` passe a `26` et `PROJECT_V1_FILE_VERSION` passe a `37`; les anciens fichiers sont refuses par validation d'en-tete, sans alias de compatibilite.

Addendum 2026-05-28 - REC METRO:
- `PARAM_CFG_METRO` est persiste comme parametre global REC CFG via `global_values`, pas par track.
- `PATTERN_VERSION` passe a `27` et `PROJECT_V1_FILE_VERSION` passe a `38`; les anciens fichiers restent refuses par validation d'en-tete/payload.

Perimetre operationnel de zone (appartient a Z6):
- `Src/Storage/pattern_live_ram.c`
- `Src/Storage/pattern_sd_bank.c`
- `Src/Storage/project_v1.c`
- `Src/Storage/project_sd_bank.c`
- `Src/Storage/boot_context_flash.c`
- `Inc/Storage/pattern_live_ram.h`
- `Inc/Storage/pattern_sd_bank.h`
- `Inc/Storage/project_v1.h`
- `Inc/Storage/project_sd_bank.h`
- `Inc/Storage/boot_context_flash.h`

Elargissements necessaires (preuve de contrats et frontieres):
- `Src/Storage/sd_access_gate.c` + `Inc/Storage/sd_access_gate.h`: arbitrage d'acces SD entre clients PATTERN/PROJECT/PREVIEW.
- `Src/Storage/sd_preview.c` + `Inc/Storage/sd_preview.h`: facade preview SD dediee, separee de l'import projet.
- `Src/Storage/wav_audio_codec.c` + `Inc/Storage/wav_audio_codec.h`: decode PCM partage pour import et preview.
- `Src/Storage/wav_convert.c` + `Inc/Storage/wav_convert.h`: conversion destructive hors IRQ des WAV PCM incompatibles vers WAV PCM24 stereo 48 kHz pour import Sampler.
- `Src/Sampler/multi_sample_index.c` + `Inc/Sampler/multi_sample_index.h`: format durable `.brickmulti` du futur `Sampler/Multi`; lecture/ecriture d'index metadata hors IRQ, sans scan dossier, sans parsing WAV, sans page-cache et sans playback.
- `Src/Storage/multi_record_writer.c` + `Inc/Storage/multi_record_writer.h`: writer SD audio multi-client avec backend `LOOPER_RAW` actif cote Looper et backend distinct `SAMPLE_WAV` actif cote Audio Rec; rings RAM statiques, push IRQ RAM-only, drain SD hors IRQ vers le backend client.
- `Src/Storage/looper_storage.c` + `Inc/Storage/looper_storage.h`: autorite des paths Looper; validation des reservoirs RAW systeme, creation dossier durable, scan borne et reservation anti-ecrasement du nom final.
- `Src/Storage/wav_loader.c`: scan catalogue et import WAV refuses pendant record audio actif/finalizing.
- `Src/Storage/wav_loader.c`: catalogue RAM WAV persistant pour le browser Settings/Sampler; le boot charge seulement le fichier catalogue SD versionne, sans rescan. La synchronisation reelle avec `0:/Samples` passe par `REFRESH`/`REBUILD` explicites et respecte `sd_access_gate`.
- `Src/Core/brick6_app_init.c`: preuve du wiring runtime (`pattern_live_init`, `project_v1_init`, `project_v1_restore_boot_context`, `pattern_live_service`).
- `Src/UI/ui_core.c`: preuve des appels UI vers `pattern_live_capture_to_slot` et `pattern_live_queue_slot`.
- `Src/UI/pages/ui_page_settings.c`: preuve des appels UI vers `project_v1_save_slot/load_slot/delete_slot`.
- `Src/Storage/undo_v1.c`: preuve de la sous-zone undo basee sur snapshots live multi-niveaux.

Sous-roles internes identifies:
- `pattern_live_ram.c`: capture/apply snapshot live + gestion active/queued pattern + service de bascule a boundary.
- `project_v1.c`: orchestration projet (capture/apply/save/load/delete/active slot + boot context policy).
- `project_v1.c`: capture aussi un bloc projet `sample_autoload` qui liste les slots sample a restaurer au boot/load projet.

Dependances de Z6 sans appartenance:
- Z2 `track_runtime` (classification domaine param pendant capture/apply).
- Z3 `param_registry`, `param_store`, `mod_lfo_v1` (capture/apply params + modulation).
- Z4 `seq_runtime`, `seq_model`, `seq_output_guard`, `seq_param_iface` (etat seq capture/apply, transport, playhead, panic).
- Z5 `ui_core` (track config restore, active track playhead reference, commandes utilisateur).

Exclusions explicites:
- `Src/Storage/pattern_data.c`, `pattern_utils.c`: utilitaires/legacy non sur le flux snapshot-projet observe ici.
- `Src/Storage/sd_card.c`: couche peripherique SD hors autorite de la politique pattern/project.
- Zone audio hard-RT (`Src/Audio/*`): consommateur indirect des etats restaures, pas autorite persistence.

## 2. Autorite(s) de verite

Autorite snapshot live (pattern):
- Capture: `pattern_live_capture_current()`.
- Apply: `pattern_live_apply_snapshot()`.
- Selection pattern active/queued: `pattern_live_queue_slot()`, `pattern_live_service()`, `pattern_live_set_active_state()`.
- Restore LFO en apply live: une seule voie d'autorite (`mod_lfo_v1_set_track_param` depuis `pattern_live_apply_snapshot`).

Autorite persistence pattern bank SD:
- `pattern_sd_bank_store_slot[_nosync]()`, `pattern_sd_bank_load_slot()`, `pattern_sd_bank_delete_slot()`, `pattern_sd_bank_get_slot_checksum()`.

Autorite orchestration projet:
- `project_v1_capture_current()`.
- `project_v1_apply_snapshot()`.
- `project_v1_save_slot()`, `project_v1_load_slot()`, `project_v1_store_snapshot_to_slot()`, `project_v1_delete_slot()`.
- `project_v1_restore_boot_context()`.
- `project_v1_get_autoload_progress()` expose a Z5/Z0 la progression boot des slots `sample_autoload` sans devenir un service UI.

Autorite persistence projet SD:
- `project_sd_bank_store_slot()`, `project_sd_bank_load_slot()`, `project_sd_bank_delete_slot()`, `project_sd_bank_is_slot_equivalent_to_live()`.

Compat prototype:
- quand `PARAM_COUNT` change et modifie `PatternSaveV1` / `ProjectSaveV1`, Z6 peut bumper les versions fichier sans migration.
- pour Wave, les anciens patterns/projets sont explicitement consideres jetables; la charge minimale consiste a refuser proprement les anciens `version/payload_size`.
- pour le projet v28, les anciens projets v27 ne sont pas migres: `project_sd_bank` exige `PROJECT_V1_FILE_VERSION` exact et `sizeof(ProjectSaveV1)` avant de lire le payload.

Autorite preview SD:
- `sd_preview_begin()`, `sd_preview_process()`, `sd_preview_render_main()`, `sd_preview_stop()`.
- Le service conserve une session SD exclusive et alimente un ring buffer audio pre-rendu hors IRQ.
- Placement memoire courant: `g_sd_preview_ring` et `g_sd_preview_io` sont en `AUDIO_COLD_SDRAM` avec alignement 32; le contexte froid `g_sd_preview` est en `STORAGE_STATE_SDRAM`. Gain D1 attendu: 16 KiB pour le ring, 4 KiB pour l'I/O et ~760 B pour le contexte. Le risque accepte cote ring est limite au cout SDRAM en IRQ pendant la preview UI; le risque restant cote I/O est SDMMC/FatFs/cache a valider par preview WAV/SD.
- `sd_access_gate` expose un diagnostic minimal de contention: owner courant, dernier owner, max hold en ticks et compteurs de refus par client. Le browser Settings/Sampler l'utilise seulement pour qualifier `SD BUSY`; il ne masque pas un refus ni ne bloque l'UI.

Autorite conversion WAV import Sampler:
- `wav_convert_*`.
- Conversion hors IRQ uniquement, demarree depuis le browser Settings/Samples quand un load slot detecte un WAV PCM convertible mais non compatible avec le cache Sampler 48 kHz.
- Format cible unique: WAV PCM 24-bit, stereo, 48 kHz, data alignee apres un header 512 octets avec chunk `JUNK`, comme le chemin SAVE Looper durable.
- Pipeline: source WAV -> `wav_audio_stream` decode/SRC lineaire 48 kHz -> fichier temporaire meme path avec extension `.B6T` -> verification parse/taille -> original renomme `.B6B` -> temp renomme original -> suppression `.B6B`.
- `sd_access_gate` utilise le client exclusif `SD_ACCESS_CLIENT_WAV_CONVERT` tenu pendant toute la conversion; preview, sample cache et autres clients SD sont exclus pendant cette fenetre.
- Refus de demarrage si record/finalize/export Looper actif, si `sample_cache` a du travail SD pending, ou si le gate SD n'est pas disponible.
- La conversion ne modifie pas `sample_cache`, `sample_page_cache`, le streamer ou le runtime audio; apres conversion, l'import relance `sample_pool_load()` sur le path original.
- Placement memoire: le contexte `g_wav_convert` et le pack buffer `g_wav_convert_pack` sont en SDRAM dediee `STORAGE_SCRATCH_SDRAM`; ils sont froids, FatFs/SD uniquement, hors IRQ audio et non DMA-owned.

Autorite catalogue WAV Settings/Sampler:
- `wav_loader_catalog_init_load()` valide au boot le fichier persistant `0:/BRICK/SAMPLE.CAT` (magic/version/entry_size/count/checksum) et ne charge plus la table complete en RAM; aucun scan de `0:/Samples` n'est fait au boot.
- `wav_loader_catalog_refresh()` et `wav_loader_catalog_rebuild()` sont les seuls chemins qui scannent reellement `0:/Samples`. Ils reconstruisent `SAMPLE.CAT` sequentiellement, avec `parent_id` global stable, sans garder le catalogue complet en SDRAM apres ecriture.
- La navigation Sampler charge des pages de dossier depuis `SAMPLE.CAT` uniquement. Le cache RAM garde deux pages LRU de `WAV_LOADER_CATALOG_VIEW_MAX=256` entrees; le scroll/page dans une page chargee est RAM-only.
- Lire une page de dossier non cachee ouvre seulement `SAMPLE.CAT` et filtre les entrees par `parent_id` + offset local de page; aucune navigation ne parcourt l'arborescence FAT reelle ni ne parse les WAV. Si `streaming_critical` est actif et que la page demandee n'est pas deja cachee, l'acces est refuse et l'UI expose `SD BUSY`.
- Les entrees persistantes portent toujours `path`, `name`, `parent_id`, `type`, `size`, `date`, `time`; les champs `size/date/time` restent dans `SAMPLE.CAT` pour evolution/refresh, pas requis par la vue UI courante. Le format courant du catalogue est V2: `path` vaut `WAV_LOADER_CATALOG_PATH_MAX=160` et `name` vaut `WAV_LOADER_CATALOG_NAME_MAX=48`, afin que les sous-dossiers courants ne soient plus tronques par l'ancien buffer 64 du browser plat.
- Capacite catalogue globale restante: 9999 entrees persistantes, liee au format V1 (`count/capacity/parent_id/index` en 16 bits), pas a une table RAM. La saturation globale positionne `truncated` (`LIB FULL`); les paths trop longs sont diagnostiques separement (`PATH LONG`) pour ne pas confondre capacite catalogue et taille de vue. Il n'y a plus de limite produit fixe par dossier dans la vue Sampler: les dossiers plus grands que 256 entrees sont pagines depuis `SAMPLE.CAT`.
- Les operations firmware qui peuvent modifier la SD n'essaient plus de maintenir localement la photo; elles peuvent seulement appeler `wav_loader_catalog_mark_stale()` / `wav_loader_catalog_notify_file_created()`. Ce flag se manifeste dans le browser par `REFRESH LIB`. Les refus REFRESH/REBUILD dus a `streaming_critical`, writer actif, export Looper ou gate SD occupe ne detruisent plus la vue catalogue RAM courante; si une reconstruction echoue, Z5 garde l'emplacement courant autant que le cache existant le permet.

Autorite index Sampler/Multi:
- `multi_sample_index_*`.
- Format durable courant: fichier binaire little-endian `.brickmulti`, magic `BRKMULTI`, version `2`, header 96 octets, CRC32 sur header avec champ CRC nul + tables + string table. La lecture accepte encore la version `1` sans metadonnees de loop.
- Le fichier porte uniquement des metadonnees: instrument, samples, zones et paths WAV relatifs au dossier instrument; aucun audio brut, aucun path absolu obligatoire.
- Tables bornees: 512 samples, 2048 zones, string table 65536 octets. Les WAV source restent directement sur SD dans le dossier instrument, typiquement `0:/Multi/<Instrument>/`.
- `multi_sample_index_load()` lit et valide l'index hors IRQ via `sd_access_gate`; `multi_sample_index_apply_to_pool()` peuple `multi_sample_pool` en etat `INDEXED` uniquement. Il ne charge pas les page0, ne touche pas `sample_page_cache`, ne touche pas le streamer et ne branche pas le playback.
- Le record sample `.brickmulti` v2 ajoute `has_loop`, `loop_begin` et `loop_end` en frames source absolues. Les bornes sont valides seulement si `loop_end > loop_begin` et `loop_end <= total_frames`; les records v1 sont charges avec `has_loop=0`. Le byte metadata trace toujours la source root/velocity (`smpl`, `inst`, filename ou alpha) et peut marquer une loop auto import (`MULTI_SAMPLE_INDEX_META_LOOP_AUTO`); il est propage vers le champ `flags` du `multi_sample_pool`.

Autorite import Sampler/Multi:
- `multi_sample_import_folder()`.
- Importe uniquement `instrument_dir/*.wav`, hors IRQ, via `sd_access_gate`, ignore les sous-dossiers et refuse si record/export Looper ou travail SD `sample_cache` est actif.
- Le browser Settings/Sampler/Multi expose une action maintenance `CLEAR` bornee au dossier courant: elle supprime seulement les `.brickmulti` des instruments directs visibles, jamais les WAV, et refuse si un index cible est deja charge ou si un travail SD concurrent est actif. La regeneration reste faite ensuite par l'import Multi normal.
- Le mapping import suit l'ordre `filename numerique -> smpl -> inst -> filename legacy -> alpha` pour le root MIDI: le suffixe `prefix_NNN_VVV.wav` donne directement note MIDI `0..127` et centre velocite `1..127`; les centres d'une meme note sont ensuite etendus en plages par midpoint. Le filename legacy `prefix-NoteMidi-VelLow-VelHigh.wav` ou `prefix_NoteMidi_VelLow_VelHigh.wav` reste supporte, puis le fallback alpha demarre a C2/MIDI 36 avec velocite `1..127`.
- Validation WAV import: PCM ou extensible PCM via `wav_parser`, 48 kHz obligatoire, mono/stereo, 16/24/32-bit, frames non nulles.
- L'import lit le premier loop `smpl` forward valide (`dwType=0`) et le convertit de `dwEnd` inclusif vers `loop_end` exclusif. Si la loop `smpl` est absente ou invalide apres validation contre `total_frames`, l'import force une auto-loop hors IRQ: il lit uniquement deux petites fenetres PCM autour de 40% et 55% du sample, score les couples zero-cross pour choisir le meilleur couple meme direction, puis le meilleur couple direction quelconque, et retombe sinon sur des bornes mecaniques 40%/55%. Le score classe les candidats mais ne bloque plus la creation; `has_loop/loop_begin/loop_end` sont renseignes avec le flag metadata auto sauf sample techniquement inexploitable.
- L'import genere les zones par layers de velocite, roots tries et bornes note par midpoint, refuse les doublons root+vel et les chevauchements note+velocity ambigus, puis ecrit `<instrument_dir>/<Instrument>.brickmulti`.

Autorite LOAD Sampler/Multi:
- `multi_sample_load_instrument()` + `multi_sample_service_load()`.
- LOAD lit un `.brickmulti`, applique l'index au `multi_sample_pool`, enregistre chaque WAV relatif comme stream `sample_audio_key_t {domain=MULTI, object_id=multi_sample_id}`, et queue uniquement page0 pour chaque sample reel.
- Le chargement page0 est cooperatif hors IRQ via le `sample_stream_manager` unique et le `sample_page_cache` unique; `multi_sample_service_load(byte_budget)` tient `SD_ACCESS_CLIENT_SAMPLE_CACHE` pendant le service. Page1 et les suivantes appartiennent au streaming runtime/prefetch apres trigger.
- L'instrument reste `LOADING` tant que toutes les page0 requises ne sont pas `READY`; toute page0 `ERROR`, tout manque de cache ou toute erreur d'enregistrement bascule l'instrument en `ERROR` avec diagnostic minimal. Budget maximal READY Multi: 512 * 1 page = 512 pages = 8 MiB; marge cache restante: 512 pages = 8 MiB.
- `multi_sample_load_has_pending()` expose seulement l'activite du loader cooperatif (`active` ou queue non vide) pour permettre au boot loading de distinguer un instrument encore en cours d'un etat terminal. Cette API ne modifie pas le page-cache, le streamer, ni le runtime audio.
- Placement memoire: la queue froide `g_multi_load_queue` est en SDRAM dediee `MULTI_LOAD_SDRAM`; elle n'est pas lue par l'IRQ audio et sert uniquement au LOAD cooperatif hors IRQ.
- Placement memoire STREAM: les chemins longs des readers actifs `sample_stream_manager` sont separes dans `g_sample_stream_reader_paths` en `SDRAM_SAMPLES`; les handles `FIL` et l'etat reader sont dans `g_sample_stream_readers` en `STORAGE_STATE_SDRAM`. Ces donnees servent uniquement au service SD cooperatif hors IRQ et ne sont pas lues par l'IRQ audio.

Contrat boot/autoload projet v32:
- `project_v1_load_slot()` active une fenetre de progression autoload uniquement apres chargement SD, restore du snapshot sample pool et apply projet reussis.
- Les slots `STREAM` attendus sont termines quand `sample_pool_get_state(slot)` n'est plus `PREPARING`: `LOADED`, `ERROR`, `MISSING` et `EMPTY` sont terminaux cote ecran boot.
- Les slots `MULTI` attendus sont termines quand l'instrument est `READY` ou `ERROR`, ou quand le loader Multi n'a plus de travail pending et que le slot n'est plus `LOADING`. Avant de lancer les loads Multi, Z6 prelit les headers `.brickmulti` des slots autoload pour figer un total global en unites utilisateur: 1 unite par sample Multi, 1 unite par slot STREAM. Pendant le chargement actif, `multi_sample_get_load_diag()` expose `samples_ready/total_samples`; la progression visible additionne cet avancement au total global au lieu d'afficher les pages internes ou de repartir a 0 pour chaque Multi.
- Les slots `RAM` attendus sont recharges synchroniquement hors IRQ par `sampler_ram_pool_load_wav_at(ram_slot, global_index, path)` pendant le load projet. La progression les compte comme une unite terminee apres l'appel; le cout produit est recalcule depuis les pages reelles allouees dans `SAMPLE_PAGE_SLOT_POOL`.
- Un fichier RAM absent/invalide ou un refus SLOT_POOL/budget/backend pose un slot global `kind=RAM` en `ERROR` quand le slot global sauvegarde est disponible. Les tracks RAM gardent leur `PARAM_SAMPLER_SAMPLE` global et refusent ensuite proprement/silence via les validations runtime RAM.
- En cas d'absence de boot context, de projet refuse ou d'ancien payload v27, l'etat loading se ferme proprement sans restore partiel supplementaire.

Autorite writer SD audio multi-client:
- `multi_record_writer_*`.
- Phase courante: interface produit, etats, diagnostics, rings RAM `int32_t` stereo statiques, backend `LOOPER_RAW` Looper REC et backend `SAMPLE_WAV` branche au hall mode Audio Rec minimal.
- Backend RAW Looper REC: `multi_record_writer_prepare_raw(client, raw_slot, raw_path, expected_frames)` ouvre un reservoir RAW existant, seek offset `0`, ecrit PCM24 stereo interleaved sans header, puis fixe `recorded_frames` apres STOP/drain/finalize.
- Le backend RAW n'ecrit aucun header WAV, n'appelle pas `f_expand` et ne passe pas par `f_rename`.
- Backend SAMPLE_WAV: `multi_record_writer_prepare_sample_wav(client, temp_path, final_path, frame_limit)` ouvre un fichier temporaire WAV, ecrit un header placeholder 512 octets, puis le record ecrit directement la data PCM24 stereo interleaved. STOP draine le ring, patch le header WAV, sync/close, puis renomme le temporaire vers le path final. Aucun reservoir RAW ni export massif post-STOP n'est implique.
- Le writer reste unique: pas de second writer FatFs concurrent. La separation se fait par `backend` + client, avec etat, ring, diagnostics et paths par client.
- `sample_capture_*` est le modele produit minimal Audio Rec / Rec Edit pour `SAMPLE_WAV`: il reserve le client writer `SAMPLE_CAPTURE_RECORD_CLIENT_ID = 1`, distinct du client Looper RAW `0`, et expose arm/len/quant/routage, prepare/start/push IRQ/request stop/status, trim SAVE et ASSIGN.
- `multi_record_writer_get_last_raw_take()` expose `raw_slot`, `raw_path` et `recorded_frames` de la derniere prise RAW finalisee; le controle Looper transmet ces metadonnees au runtime transient pour le playback RAW et les utilise comme source de SAVE RAW -> WAV durable.
- `multi_record_writer_get_last_sample_wav_take()` expose le path final et `recorded_frames` d'une prise `SAMPLE_WAV` finalisee pour l'auto-ouverture Rec Edit restreint.
- `sample_capture` porte maintenant le modele produit minimal Audio Rec / Rec Edit: etat arm/len/quant, matrice de routage sources, path temporaire, buckets waveform RAM min/max signes non persistants, trim START/END, loop START/END UI-session, `ZCROSS` UI-session directionnel, `VZOOM` UI-session en crans `x0.5..x8`, SAVE auto vers `0:/Samples/RECnnnn.WAV` et assignation optionnelle post-SAVE vers le premier slot libre `sample_pool`. `ARM=REC` est une autorisation UI seulement: aucune ouverture de temp WAV, aucun push IRQ et aucun demarrage writer ne se produisent avant REC global actif + transport/quant valide. `LEN=1..64` est compte en steps Audio Rec.
- La prise temporaire `SAMPLE_WAV` reste un WAV temporaire sous `0:/PROJECT/REC`; SAVE cree un nouveau WAV final trimme et ne cree aucun sidecar. L'assignation post-SAVE charge ce WAV deja sauvegarde dans `sample_pool` sans refaire de copie trimmee.
- L'audition Rec Edit utilise `sd_preview_begin_range(path, start_frame, end_frame)` pour lire la fenetre `START..END` depuis le WAV temporaire finalise/ferme, sans charger la prise dans `sample_pool` et sans cache waveform persistant.
- La waveform live/Rec Edit reste volatile: buckets RAM min/max signes `int16` issus des blocs audio captures PCM24, compression min/max en RAM quand la resolution est pleine, puis affichage frame-aware par fenetre zoom/scroll. L'overview RAM utilise un bucket initial court afin de reduire la perte visible en REC live et comme fallback Rec Edit tant qu'aucune line n'est disponible. Le renderer affiche toujours la ligne zero; en REC live il dessine les buckets en traits verticaux fins, y compris pour `min=max=0`. En Rec Edit, `sample_capture` demande un cache line separe des `edit_zoom=0`, sans sidecar ni cache persistant, derive depuis le cache audio RAM quand la fenetre est couverte. L'ancien cache line n'est plus invalide a la demande d'une nouvelle fenetre: il reste affichable pendant le remplissage/recentrage du cache audio pour stabiliser le rendu. L'echelle verticale Rec Edit est stable sur la prise courante et vise une marge d'environ 4 px haut/bas pour les pics forts. Le zoom Rec Edit est continu sur `edit_zoom=0..255`: `view_frames = total_frames * pow(min_view_frames / total_frames, edit_zoom / 255)`, borne entre la prise complete et 256 frames. Le changement de zoom conserve le centre visuel courant, puis le scroll est borne a la fenetre visible; le scroll encodeur avance proportionnellement a `view_frames`.
- Overview globale Rec Edit: la prise ouverte possede une carte complete editor-owned de 4096 points max `{min,max}` `int16`, soit 16 KiB, placee en `EDITOR_AUDIO_CACHE_SDRAM`. Elle couvre toujours `0..recorded_frames` une fois prete, se construit hors IRQ depuis le WAV temporaire finalise par chunks bornes via `SD_ACCESS_CLIENT_EDITOR_CACHE`, et sert uniquement au zoom global/dezoom ou fallback propre; elle n'est ni persistante, ni sample-owned, ni sidecar.
- Etape cache editor tuile: Rec Edit possede un cache audio RAM volatile editor-owned compose de 16 tuiles mono `int16` de 0,25 s / 12000 frames chacune, soit 4 s audio total, place en SDRAM dediee `EDITOR_AUDIO_CACHE_SDRAM`. Le service `sample_capture` remplit une seule tuile a la fois hors IRQ depuis le WAV temporaire finalise par chunks PCM24 stereo bornes, via le client gate `SD_ACCESS_CLIENT_EDITOR_CACHE`. Les demandes sont priorisees autour du focus/vue courante: tuile focus, tuiles visibles si possible, anticipation droite/gauche puis voisinage focus. Le cache n'est ni persistant, ni sample-owned, ni sidecar.

- Optimisation vues larges/locales: chaque tuile maintient pendant son chargement une pyramide volatile SDRAM de points `{min,max,first,last}` sur blocs 16/64/256 frames. La generation line Rec Edit selectionne le niveau selon `samples_per_point` et peut assembler une ligne depuis plusieurs tuiles pretes. Si `view_frames` depasse la capacite totale du cache tuile, aucune demande de couverture locale n'est envoyee: le renderer utilise l'overview globale. Pour une vue locale, la decision de cache demande d'abord les tuiles visibles, puis au plus une tuile gauche et une droite en prefetch, avec au plus 4 nouvelles demandes par decision; les chargements devenus hors voisinage du focus sont marques stale/drop. Si une vue locale n'est pas encore couverte, le renderer garde l'ancienne line compatible ou proche avant de retomber sur l'overview globale; le draw ne lit jamais la SD.
- Instrumentation Rec Edit optionnelle: `SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS=1` active des logs UART1 bornes pour le renderer, le cache editor tuile, la generation line RAM, le draw page/waveform et le flush OLED (`RECEDIT R=BRKWAVE_TILE`, `RECEDIT R=OLD_AUDIO_TILE`, `RECEDIT R=OLD_LINE`, `RECEDIT R=GLOBAL_OVERVIEW`, `RECEDIT R=EMPTY`, `wc=NONE|16384|4096|1024|256`, `OLD_AUDIO_TILE_HIT/MISS/REQ/DONE`, `ELINE`). Ce flag vaut `0` par defaut: aucun log UART waveform haute frequence ne doit tourner pendant draw/scroll/zoom. Les breadcrumbs HardFault RAM restent separes.
- Le cache detail waveform Rec Edit est opportuniste: il ne se construit pas pendant record/finalize, preview active, export Looper, pattern load pending ou travail SD sample-cache pending; si le gate SD est occupe par un client prioritaire, le rendu garde l'overview RAM.
- `sd_preview_begin_range()` refuse tout record/finalize writer actif et tout export Looper. Les operations Audio Rec qui doivent reprendre la main sur le fichier temporaire (`nouveau REC`, RETURN, SAVE, ASSIGN) stoppent la preview avant de continuer; un changement START/END s'applique a la prochaine audition declenchee.
- Priorite SD effective dans la superloop: `multi_record_writer_service` draine/finalise d'abord; hors export Looper, `sample_cache_service`, refill Looper, load Multi et `pattern_load_service` passent avant `sd_preview_process`. Une preview active est stoppee si un travail sample cache devient pending, et `pattern_load_request/service` stoppe la preview avant son acces SD.
- Le mode `streaming_critical` de `sd_access_gate` est active par les locks de fenetre voix Sampler: seuls les services STREAM sous `SD_ACCESS_CLIENT_SAMPLE_STREAM` peuvent demarrer une nouvelle possession SD. Preview, convert/import, editor/waveform cache, pattern, project et chargements samples non-stream sont differes. Une operation deja proprietaire du gate n'est pas preemptee dans cette passe.
- Le backend Sampler `STREAM_SAFE_CONTIGUOUS` utilise une certification physique volatile construite au load STREAM par FatFs CLMT. Cette certification n'est pas persistante: toute operation BRICK qui ecrit, convertit, renomme, supprime ou remplace un WAV doit etre consideree comme invalidante; le prochain load rescanne et retombe sur FatFs si le fichier n'est pas certifie safe.
- Les lectures secteurs directes du backend contigu ne contournent pas `sd_access_gate`: elles sont appelees uniquement par le streamer Sampler pendant une possession SD existante, et ne remplacent pas les chemins FatFs de browser/import/projets/export.
- Le writer interdit le demarrage simultane de deux clients actifs: Looper RAW actif bloque Sample Rec et Sample Rec actif bloque Looper REC. Le playback Looper peut rester source routee d'Audio Rec.
- `multi_record_writer_service(byte_budget)` acquiert le gate sans bloquer seulement si le sample cache n'a pas de travail SD pending, packe `int32_t` stereo vers PCM 24-bit interleaved par chunk borne de 1024 frames, execute au plus un `f_write` audio par passage, puis relache le gate.
- `STOP_REQUESTED -> DRAINING -> FINALIZING -> TAKE_READY` draine le ring. En RAW, la finalisation fait sync/close et fixe `recorded_frames`; en SAMPLE_WAV, elle patch le header puis sync/close/rename.
- La transition `SAMPLE_WAV TAKE_READY -> Rec Edit` est RAM-only: copie du path finalise, frames, trim, vue et demandes cache. Si le path finalise est le fichier writer temporaire `0:/PROJECT/REC/AUDIOREC_TMP.WAV`, aucune demande `.wavecache` n'est emise; les services SD Rec Edit (`overview/editor cache`) restent differes pour empecher une lecture FatFs dans la meme passe UI que l'entree Rec Edit.
- Les producteurs audio poussent uniquement vers les rings RAM; le hook Z1 appelle `multi_record_writer_push_audio_block_from_irq` depuis `mixer_process` seulement quand le client concerne est en `RECORDING` et que son autorite runtime a active la capture. Pour Audio Rec, `sample_capture_audio_hook_is_enabled()` devient vrai uniquement apres REC global + transport/quant, jamais sur le seul `ARM=REC`.
- Placement memoire: les rings producteurs restent en `SDRAM_RECORDER`; le buffer pack PCM24 `g_pcm24_pack`, utilise seulement par `multi_record_writer_service()` hors IRQ, est en SDRAM dediee `RECORDER_SCRATCH_SDRAM`.
- `multi_record_writer_any_active()` garde les operations globales incompatibles avec toute prise/finalisation audio; les guards pattern utilisent le filtre backend Looper RAW pour laisser Audio Rec `SAMPLE_WAV` cohabiter avec `pattern_load_request/service`.

Politique SD pendant record audio actif/finalizing:
- Refuse: project save/load/delete, blank load projet, refresh/list/has-data projet.
- Refuse courant: pattern load/request/service pendant Looper RAW actif ou export Looper. Le record `SAMPLE_WAV` ne bloque plus `pattern_load_request/service` par principe; le gate SD reste arbitre par tranches hors IRQ.
- Refuse temporairement: pattern save direct, car le chemin courant ecrit encore en SD synchrone; TODO `pending budgeted pattern save`.
- Refuse: preview WAV SD, scan catalogue WAV et import/load WAV vers SDRAM.
- Le cleanup/finalize du writer reste proprietaire du writer et continue via `sd_access_gate`.

Autorite paths Looper:
- `looper_storage_raw_init()`, `looper_storage_raw_validate()`, `looper_storage_raw_is_available()`.
- `looper_storage_raw_get_slot_for_track(track, &slot)` calcule le mapping deterministe depuis la projection `track_runtime` deja rafraichie par le caller.
- `looper_storage_raw_track_is_available(track)` combine mapping valide et reservoirs RAW valides.
- Les reservoirs RAW systeme obligatoires sont:
  - `0:/SYSTEM/LOOPER/LPR00.RAW`
  - `0:/SYSTEM/LOOPER/LPR01.RAW`
  - `0:/SYSTEM/LOOPER/LPR02.RAW`
  - `0:/SYSTEM/LOOPER/LPR03.RAW`
- Format logique RAW cible: PCM stereo 24-bit / 48 kHz, little-endian, interleaved L/R, sans header.
- Taille exacte par reservoir: `999999996` octets, soit `166666666` frames a 6 octets/frame.
- Les reservoirs sont crees hors firmware live par outil PC/setup SD; le firmware ne fait que valider presence et taille par `f_stat`.
- Si un reservoir est absent ou de taille invalide, `looper_storage_raw_is_available()` reste faux et le storage Looper RAW est indisponible au niveau systeme.
- La validation conserve le dernier diagnostic RAW: slot fautif, `FRESULT` FatFs et taille observee quand disponible, exposes par getters pour distinguer missing/stat/size/mount/busy sans refaire un acces SD.
- Ces reservoirs ne passent jamais par `sample_pool`, catalogue WAV, browser ou slots Sampler projet.
- Mapping RAW cible:
  - premiere track `Sampler/Looper` logique -> slot RAW `0`,
  - deuxieme -> slot RAW `1`,
  - troisieme -> slot RAW `2`,
  - quatrieme -> slot RAW `3`,
  - cinquieme et suivantes -> indisponibles/refus propre.
- Ce mapping n'est pas un allocateur: il est recalcule par scan des tracks logiques et suit les changements family/type/load projet apres refresh runtime explicite.
- `looper_storage_make_next_path(track_id, out_path, out_len)`.
- Cree `0:/PROJECT` puis `0:/PROJECT/LOOPS` a la demande via le client SD recorder.
- Scanne au plus `LOOPER_STORAGE_SAVE_PATH_TRIES` noms `0:/PROJECT/LOOPS/LPRtt_nnnn.WAV` par `f_stat`.
- Ne retourne un path final que si le fichier cible est absent; un fichier existant n'est jamais choisi silencieusement.
- Mappe l'indisponibilite du gate SD vers `LOOPER_STORAGE_PATH_BUSY`; les fautes mount/mkdir/stat/path vers `LOOPER_STORAGE_PATH_FAIL`.

Autorite boot context flash:
- `boot_context_flash_load()`, `boot_context_flash_commit()`, `boot_context_flash_clear()`.

Seconde autorite concurrente:
- Aucune seconde autorite complete equivalente n'est observee pour le couple capture/apply live + save/load project.
- Sous-zone parallele partielle: `undo_v1` reutilise `pattern_live_capture_current/apply_snapshot`, mais ne remplace pas l'autorite pattern/project.

## 3. API entrantes

Entrees depuis init/runtime global:
- `brick6_app_init.c` appelle `pattern_live_init()`, `project_v1_init()` puis arme `ui_boot_loading`; le restore du boot context est declenche ensuite par le service loading apres une premiere frame OLED.
- `brick6_app_init.c` appelle `multi_record_writer_init()`.
- `brick6_app_init.c` appelle `looper_storage_raw_init()` puis `looper_storage_raw_validate()` hors IRQ; cette validation ne cree aucun dossier/fichier RAW.
- Boucle superloop appelle `multi_record_writer_service(16384U)` puis `brick6_looper_runtime_service(8192U)` et `pattern_live_service()`.

Entrees depuis UI:
- `ui_core.c` appelle `pattern_live_capture_to_slot()` et `pattern_live_queue_slot()`.
- `ui_page_settings.c` appelle `project_v1_refresh_slots()`, `project_v1_list_slots()`, `project_v1_slot_has_data()`, `project_v1_load_slot()`, `project_v1_save_slot()`, `project_v1_delete_slot()`.

Entrees depuis undo:
- `undo_v1.c` appelle `pattern_live_capture_current()` et `pattern_live_apply_snapshot()`.

Contrats implicites observes:
- APIs `project_v1_*` mutantes et `undo_v1_*` refusent contexte ISR via `__get_IPSR()`.
- `pattern_live_service()` suppose appel periodique hors IRQ audio, avec seq runtime deja actif si queue presente.
- L'autorite stop/panic/reprise transport du restore live est centralisee dans `pattern_live_apply_snapshot()`.
- `pattern_live_queue_slot()` applique immediatement si transport stoppe, sinon differe au boundary.

Getters non-mutants:
- `pattern_live_get_active()`, `pattern_live_get_queued()`, `pattern_live_is_apply_in_progress()`.
- `project_v1_get_active_slot()`, `project_v1_get_last_error()`, `project_v1_get_last_sd_error_code()`.

## 4. API sortantes

Z6 appelle les zones suivantes:
- Z5 UI:
  - `ui_get_track_family/type/midi_*`, `ui_restore_track_config_bulk()`, `ui_get_active_track()`.
- Z2 Track runtime:
  - `track_runtime_get_param_rule()`, `track_runtime_refresh_all()`.
- Z3 Param/Mod:
  - `param_get()`, `param_set()`, `param_registry_get_track_value()`, `param_registry_apply_track_value()`, `param_registry_batch_begin/end()`, `param_registry_sync_ui_for_active_track()`.
  - `mod_lfo_v1_get_track_param()`, `mod_lfo_v1_set_track_param()`.
- Z4 Seq:
  - `seq_model_*` (capture/apply trig/plocks/pages/length).
  - `seq_runtime_*` (tempo/clock/rec/div/quant/swing/playhead/start/stop).
  - `seq_output_guard_panic()`.
- Infrastructure storage:
  - `sd_access_gate_try_acquire/release()`, `sd_access_fs_mount_if_needed()`, FatFs (`f_open/f_read/f_write/f_sync/f_unlink/f_lseek/f_mkdir`).
  - HAL flash in `boot_context_flash.c` (`HAL_FLASHEx_Erase`, `HAL_FLASH_Program`).

Contrats timing:
- SD/flash operations sont hors hard-RT et potentiellement longues.
- Apply snapshot/projet stoppe transport avant mutation d'etat globale.
- Load projet est separe en 3 phases: load+validation, live apply, puis commit pattern-bank SD.

## 5. Etats structurants possedes

### `pattern_live_ram.c`
- `g_current_pattern` (`PatternSaveV1`): dernier snapshot live capture/applique.
- `g_next_pattern` (`PatternSaveV1`): snapshot pattern queue pour prochaine bascule.
- `g_boot_pattern` (`PatternSaveV1`): fallback snapshot boot si slot absent.
- `g_pattern_slot_meta[16][16]` (`pattern_slot_meta_t {has_snapshot, dirty_pending_persist}`): meta locale des slots.
- `g_active_bank`, `g_active_pattern`: pattern actif.
- `g_queued_valid`, `g_queued_bank`, `g_queued_pattern`: pattern queue.
- `g_apply_in_progress`: garde anti re-entrance apply.
- `g_last_playhead_valid`, `g_last_playhead_step`: detection de wrap pour apply queue.

Points d'ecriture principaux:
- capture: `pattern_live_capture_current`, `pattern_live_capture_to_slot`, `pattern_live_capture_boot_snapshot`.
- apply: `pattern_live_apply_snapshot`, `pattern_live_queue_slot`, `pattern_live_service`, `pattern_live_set_active_state`, `pattern_live_init`.

Points de lecture principaux:
- UI via `pattern_live_get_active/get_queued`.
- project via `project_v1_capture_current` et `project_v1_apply_snapshot`.
- undo via `undo_v1_*`.

### `pattern_sd_bank.c`
- `g_slot_has_data[16][16]`, `g_slot_meta_cache_valid[16][16]`: cache presence pattern slots en D1.
- `g_slot_checksum_cache[16][16]`: cache checksum pattern slots en `STORAGE_STATE_SDRAM`, metadata froide hors IRQ audio et non DMA-owned.
- `g_boot_pattern` + `g_boot_pattern_valid`: fallback de chargement si fichier slot absent.
- `g_pattern_write_chunk[4096]`: tampon chunk write pour payload pattern.

### `project_v1.c`
- `g_project_work` (`ProjectSaveV1`): buffer travail pour save/load/apply, incluant le snapshot `sample_pool` du projet, le bloc `sample_autoload` et le bloc MACRO projet.
- `g_project_multi_assign[SEQ_TRACK_COUNT]`: verite durable RAM projet pour l'assignation `Sampler/Multi` par track (`path .brickmulti` borne + gain); `instrument_id` reste runtime et n'est pas persiste.
- `g_project_macro_state` (`project_v1_macro_state_t`): owner RAM canonique du chantier MACRO (scenes/pots/locks + `Mode`), distinct du payload pattern et du `undo_v1`; placement `UI_SDRAM` car etat froid projet/persistence non audio et non DMA.
- `g_project_active_slot_valid`, `g_project_active_slot`: slot projet actif logique.
- `g_project_save_counter`: compteur de version save.
- `g_project_last_error`, `g_project_last_sd_error`: etat erreur expose API.

### `wav_loader.c`
- `g_wav_pcm` reste en `AUDIO_COLD_SDRAM` pour le buffer PCM charge.
- `g_wav_catalog_views` garde deux pages LRU de catalogue Sampler en `UI_SDRAM`; le catalogue complet reste dans `0:/BRICK/SAMPLE.CAT`.
- `g_wav_fs` est place en `STORAGE_STATE_SDRAM`: handle FatFs froid, non DMA-owned, monte par service storage hors IRQ audio.
- Le catalogue scanne `0:/Samples` uniquement pendant `REFRESH` / `REBUILD`; les dossiers sont ecrits avant les fichiers dans chaque dossier pour que le browser les affiche en tete.

### `project_sd_bank.c`
- `g_project_slot_has_data[16]`: presence des slots projet.
- `g_project_slot_buffer` (`PatternSaveV1`): buffer temporaire records pattern lors save/load.
- `g_project_io_buffer` (`ProjectSaveV1`): buffer I/O projet froid en `UI_SDRAM`, utilise pour revalidation/compare sans allouer le payload projet massif sur la stack.
- `g_project_sd_last_error`: erreur SD detaillee.

### Placement SDRAM froid storage/control
- `CONTROL_STATE_SDRAM` / `.control_state_sdram`: etat control low-rate non IRQ audio; utilise pour `g_param_macro_sources`.
- `STORAGE_STATE_SDRAM` / `.storage_state_sdram`: metadata/handles storage non DMA-owned et hors IRQ audio; utilise pour `g_slot_checksum_cache`, `g_pattern_slot_meta`, `g_sd_preview`, `g_wav_fs`, `g_sd_fs`, `g_sample_pool_fs`.
- `UI_STATE_SDRAM` reste reserve aux etats UI explicites; `g_looper_save_diag` y est place car diagnostic UI froid.

### `boot_context_flash.c`
- `g_boot_ctx_cache` (`boot_context_flash_data_t {version, valid, crc, active_project_slot}`): cache dernier contexte valide.
- `g_boot_ctx_cache_valid`: validite cache RAM.

### `project_v1` macro RAM
- `project_v1_macro_state_t` porte le modele MACRO projet-level en RAM:
  - `hall_switch_mode` conserve comme tombstone de layout projet (`Scene` / `Switch`), sans autorite UI active depuis le retrait du mode Hall MACRO,
  - `macro_scene[4]` pour lier chaque macro pot a une scene,
  - `scenes[16]` porte les 16 scenes MACRO; chaque scene contient `locks[32]`.
- Les locks vides utilisent une convention sentinel explicite:
  - `track = PROJECT_V1_MACRO_LOCK_TRACK_NONE`,
  - `param = PROJECT_V1_MACRO_LOCK_PARAM_NONE`.
- Ce bloc est capture/restaure dans `ProjectSaveV1` et persiste via `project_sd_bank_*` au niveau projet.

### `undo_v1.c` (sous-zone dependante)
- `g_undo_v1` (`undo_snapshot_v1_history_t`): ring buffer de 10 snapshots undo.
- `g_undo_capture_work`: buffer de capture temporaire.
- `g_undo_capture_suspended`: garde anti-recursion pendant restore undo.

### `undo_v2.c` (sous-zone dependante)
- Historique delta UI/p-lock en RAM froide `UI_SDRAM`: runtime, transactions, deltas param, deltas p-lock, deltas step et snapshots.
- Les structs stockees en tableaux undo v2 ont un alignement minimal 4 octets et un stride multiple de 4, verifie par `_Static_assert`, pour rester compatibles avec `UNALIGN_TRP` actif.
- Les free-lists undo v2 restent bornees par capacites fixes; toute tete/index hors pool est refusee avant dereferencement.
- Le chemin UI p-lock n'applique pas une mutation seq sans entree undo v2 valide: en cas de refus/overflow undo, les locks deja appliques dans le geste courant sont rollbackes.

## 6. Flux runtime

1. Capture d'etat live:
- Source: `pattern_live_capture_current()`.
- Collecte:
  - track cfg UI (`ui_get_track_*`),
  - seq model (trigs/plocks/pages/length), avec p-locks stockes comme `set_id + param_slot + value16` ; le slot reste local et n'est pas un `param_id`,
  - params globaux + track values,
  - mod LFO,
  - tempo/clock/rec/div/quant/swing.

2. Apply snapshot / restore pattern:
- `pattern_live_apply_snapshot()`:
  - valide budget plocks,
  - stop transport + panic,
  - restore track config UI,
  - `track_runtime_refresh_all()`,
  - apply seq block (plus tot),
  - batch apply params + globals,
  - restore LFO,
  - restore tempo/clock/rec/div/quant/swing,
  - reset playheads, sync UI param,
  - restart transport si autorise.

3. Save pattern:
- `pattern_live_capture_to_slot()` capture `g_current_pattern` puis `pattern_sd_bank_store_slot()`.
- Maj meta locale `has_snapshot/dirty_pending_persist`.

4. Load/queue pattern:
- `pattern_live_queue_slot()` lit presence slot via `pattern_sd_bank_slot_has_data()`.
- Charge `g_next_pattern` depuis SD ou fallback `g_boot_pattern`.
- Si transport stop: apply immediate + maj active.
- Si transport run: queue (`g_queued_*`) pour apply differe.

5. Bascule queue au boundary:
- `pattern_live_service()`:
  - active seulement si snapshot RAM complet arme + transport run + pas d'apply en cours,
  - lit playhead track active,
  - applique sur transition non-zero -> zero.

6. Save project:
- `project_v1_save_slot()`:
  - capture current project (`project_v1_capture_current`),
  - capture aussi le snapshot `sample_pool` courant du projet,
  - capture la liste durable `sample_autoload`: slots `STREAM` issus du `sample_pool`, slots `MULTI` issus du `multi_sample_pool`, slots `RAM` READY/ERROR issus du `sampler_ram_pool`,
  - capture les assignations `Sampler/Multi` par track: path `.brickmulti` et gain Multi; aucun `instrument_id` runtime n'est sauvegarde,
  - capture le bloc MACRO projet (`Mode`, scenes liees aux pots, scenes/locks),
  - force active slot dans snapshot,
  - stocke via `project_v1_store_snapshot_to_slot` -> `project_sd_bank_store_slot`,
  - incremente save_counter,
  - commit boot context si slot actif valide.

7. Load project:
- `project_v1_load_slot()`:
  - charge depuis SD via `project_sd_bank_load_slot` (lecture + validation header/checksum + records, sans commit pattern-bank),
  - restaure le `sample_pool` du projet avant l'apply live,
  - restaure les slots globaux `STREAM` depuis `sample_autoload`, puis recharge les slots `RAM` declares via `sampler_ram_pool_load_wav_at(slot_index, global_index, path)` avant l'apply live,
  - restaure les slots `MULTI` declares dans `sample_autoload` apres l'apply live via `multi_sample_load_instrument(path, slot_index)`; les refus restent non fatals et posent le diagnostic restore existant,
  - restaure le bloc MACRO projet depuis `ProjectSaveV1`,
  - applique snapshot (`project_v1_apply_snapshot` -> `pattern_live_apply_snapshot`),
  - restaure ensuite les assignations `Sampler/Multi`: gain par track, allocation runtime d'un `instrument_id`, appel `multi_sample_load_instrument(path, instrument_id)` hors IRQ, et track non jouable tant que `multi_sample_is_ready()` reste faux,
  - commit ensuite le pattern-bank SD via `project_sd_bank_commit_slot_patterns`,
  - met a jour slot actif/counter,
  - commit boot context.

8. Restore boot context:
- `project_v1_restore_boot_context()`:
  - lit flash (`boot_context_flash_load`),
  - valide slot, puis appelle `project_v1_load_slot(slot)`.

9. Flux interne project SD (important):
- `project_sd_bank_load_slot`: phase de lecture/validation uniquement (payload projet + records + checksum global).
- `project_sd_bank_commit_slot_patterns`: phase de commit pattern-bank (revalidation puis application des deltas vers `pattern_sd_bank_*`).

Effets aval:
- Z5: UI track config et sync param active track sont mis a jour.
- Z3: runtime params/LFO sont remutes via registry/mod.
- Z4: transport/clock/seq state est restaure et repositionne.
- Z2: binding runtime rafraichi avant apply param track.
- Z1/Sampler Multi: le restore projet ne bloque pas sur les pages audio; le chargement page0 reste cooperatif via `multi_sample_service_load(byte_budget)` en superloop. Plusieurs tracks pointant le meme `.brickmulti` partagent le meme `instrument_id` runtime restaure; des paths differents sont queues par le loader Multi.

## 7. Contraintes RT/CPU/memoire

- Z6 n'est pas hard-RT: SD et flash bloquants autorises, mais exclus du chemin IRQ audio.
- APIs projet mutantes protegent contre ISR (`__get_IPSR`).
- `pattern_live_apply_snapshot` fait des boucles `PARAM_COUNT x SEQ_TRACK_COUNT` + seq full copy, cout variable mais hors IRQ audio.
- Pas de malloc observe dans les fichiers Z6; buffers statiques (`UI_SDRAM`, `DMA_BUFFER`, globals).
- Coordination SD via `sd_access_gate` evite collisions clients heterogenes (pattern/project/preview vs autres clients).
- La preview SD lit les formats source WAV et ne touche jamais le pool projet Stream.
- La preview SD ne participe pas au sample streaming principal: son ring SDRAM est seulement une audition temporaire vers MAIN, et son buffer I/O SDRAM reste limite au flux FatFs/decode WAV preview.

## 8. Invariants a ne pas casser

- Autorite unique de capture/apply live: `pattern_live_capture_current` / `pattern_live_apply_snapshot`.
- Separation explicite:
  - live RAM (`pattern_live_*`) vs persistence pattern SD (`pattern_sd_bank_*`) vs persistence project SD (`project_sd_bank_*`) vs boot pointer flash (`boot_context_flash_*`).
- Apply snapshot/projet impose stop transport avant mutation et reset playhead apres apply.
- Ordre durci de restore live: stop/panic -> track config -> `track_runtime_refresh_all` -> seq block -> params/LFO/runtime globals.
- Queue pattern appliquee uniquement sur boundary detecte (wrap playhead) quand transport tourne.
- `project_v1_load_slot` ne commit le pattern-bank SD qu'apres succes du live apply.
- Getters de statut actif/queued et erreurs ne mutent pas l'etat metier (hors eventual set error pour API invalides cote project_v1).

## 9. Dependances inter-zones

- Depend de Z5 pour source et restauration config track UI, et pour les commandes utilisateur save/load/capture/queue.
- Depend de Z2 pour classification param domains et refresh binding runtime avant apply.
- Depend de Z3 pour application/lecture parametres et modulation.
- Depend de Z4 pour transport/tempo/clock/playhead et donnees sequenceur.
- Expose a Z5/Z7 (settings/ops) les APIs projet/pattern de persistence.
- `undo_v1` ne couvre pas les mutes, ROUT, navigation, copy, settings, load/save ou restore globaux; les resets d'historique sont poses sur `load project` et `load pattern`.

## 10. Dette technique observee

- Couplage inter-zone eleve dans `pattern_live_apply_snapshot` (UI + Z2 + Z3 + Z4 dans une seule routine).
- Dependance implicite a l'ordre d'appel superloop pour `pattern_live_service` (si non appele, queue pattern ne commute pas).
- `dirty_pending_persist` est ecrit mais non exploite dans le flux observe (meta partiellement orpheline).
- Parametres mix legacy `PARAM_MIX_TRACK0..3_*` traites comme tombstones: non captures en globals normaux, et migres load-only vers les params MIX track-aware quand un ancien snapshot contient gain/pan/mute/send.
- `project_v1_apply_snapshot` est un orchestrateur mince: delegation du live apply a `pattern_live_apply_snapshot` + restauration etat actif/queued/slot projet.
- Couplage UI implicite dans la condition de boundary (`seq_runtime_get_playhead_step(ui_get_active_track(), ...)`) au lieu d'une reference transport neutre.

Aucune double autorite complete concurrente de save/load projet n'est observee.

## 11. Impact eventuel sur la cartographie globale

- Z6 est confirmee comme zone composite avec 4 sous-domaines stables: live snapshot, pattern SD bank, project orchestration, boot context flash.
- `undo_v1` doit etre rattache en sous-zone dependante de Z6 (pas une zone principale separee).
- Frontiere Z6/Z5 est plus forte que prevu: la mutation persistence est pilotee par UI, mais l'autorite d'etat reste dans Z6.

## 12. Conclusion stricte

`cause trouvee`

## 16. Chantier MACRO - RAM owner de reference
- Le chantier MACRO commence par l'owner RAM canonique dans `project_v1`.
- La persistence projet-only et le branchement UI/runtime suivront sur des seams distincts.

## 15. Contrat "Blank Project"
- Autorite unique: `project_v1_load_blank()`.
- Comportement:
  - reset pool Sampler (`sample_pool_init()`),
  - apply snapshot boot vierge via `pattern_live_apply_boot_snapshot(0)`,
  - clear historique undo,
  - projet actif logique sans slot (`active_project_slot_valid=0`),
  - clear boot context flash (aucun slot force au prochain boot).
- Aucun slot projet SD n'est ecrit/modifie par cette operation.

## 13. Chantier Z6 cible - `pattern_live_apply_snapshot()`

Carte courte du flux reel observe dans `Src/Storage/pattern_live_ram.c`:
- Stop/panic/reprise transport:
  - `pattern_live_apply_snapshot()` capture `was_running`, execute `seq_runtime_stop()` puis `seq_output_guard_panic(1U)` avant toute mutation.
  - reprise uniquement en fin d'apply si `resume_transport != 0` et si transport etait running avant stop.
- Restore params / modulation:
  - `param_registry_batch_begin/end` encadre l'apply des blocs `sound` + `mix` par track puis des globals via `param_set`.
  - modulation LFO restauree ensuite, uniquement via `mod_lfo_v1_set_track_param` (autorite unique).
- Restore seq/model/playhead:
  - validation budget plocks, puis `pattern_live_apply_seq_block()` (reset model + trigs/plocks/pages/length).
  - reset playhead de toutes les tracks a 0 en fin d'apply.
- Restore config UI / track runtime:
  - restore bulk track config UI en premier (`ui_restore_track_config_bulk`), puis `track_runtime_refresh_all()` avant restore seq/params.
- Boundary / apply queue:
  - `pattern_live_queue_slot()` applique immediatement si transport stoppe, sinon queue.
  - `pattern_live_service()` declenche l'apply queue sur wrap playhead `!=0 -> 0` lu via `seq_runtime_get_playhead_step(ui_get_active_track(), ...)`.

Verdict couplage:
- Couplage inter-zones eleve mais borne et coherent avec l'autorite Z6: une seule routine de restore live, ordre de restauration explicite, garde anti re-entrance (`g_apply_in_progress`), et point unique stop/panic/reprise.
- Fragilite residuelle connue et circonscrite: le boundary de queue depend de `ui_get_active_track()` (dependance Z5 dans un trigger temporel Z4).

Plus petite prochaine passe utile:
- Pas de refonte ni de deplacement d'autorite.
- Passe documentaire uniquement: contrat explicite conserve dans ce canonique; aucun micro-patch code requis tant que la politique boundary reste voulue.

## 14. Contrat Sampler v1

- Le snapshot projet/pattern transporte les params Sampler via le flux param track-aware existant:
  - `Sample`, `Mode`, `Start`, `End`, `Gain`, `Tune`, `Loop Start`, `Slice Count`.
- Compat restore:
  - les payloads pattern/projet gardent les memes champs family/type,
  - un ancien couple `family=Synth` + `type=Sampler` est remappe au restore vers `family=Sampler` + `type=RAM`,
  - un ancien mode `Slice` / `RevSlice` est rabattu vers `Shot` au restore/apply runtime pour eviter toute exposition produit `RAM`,
  - aucun bump de format snapshot n'est requis pour cette seule sortie de family.
- La grille Slice n'est jamais persistÃ©e:
  - elle est reconstruite au restore depuis `sample_id` et `Slice Count`.
- `Slice Count` reste hors p-lock; `Slicer` n'est plus un type Track CFG visible et les configs legacy `Sampler/Slicer` sont normalisees en `Sampler/RAM` sans changer `PARAM_COUNT`.
- `PROJECT_V1_FILE_VERSION` a ete incremente pour reflï¿½ter le payload Sampler v1 et le bloc MACRO projet.
- `PATTERN_VERSION=6` et `PROJECT_V1_FILE_VERSION=10` marquaient une ancienne rupture prototype Synth historique; les anciens payloads incompatibles restent refuses via `version/payload_size`.
- Le `sample_pool` du projet est persiste comme references de slots (paths WAV), pas comme audio brut.
- Au restore projet, le pool est reconstruit avant l'apply live pour que les params `Sample` retrouvent les slots residents quand c'est possible.

## 15. Contrat send2 delay global

- Les params delay globaux produit (`PARAM_MIX_DELAY_TYPE`, `PARAM_MIX_DELAY_TIME`, `PARAM_MIX_DELAY_PINGPONG`, `PARAM_MIX_DELAY_MODE`, `PARAM_MIX_DELAY_TIME_R`, `PARAM_MIX_DELAY_WIDTH`, `PARAM_MIX_DELAY_FEEDBACK`, `PARAM_MIX_DELAY_HPF`, `PARAM_MIX_DELAY_LPF`, `PARAM_MIX_DELAY_FBW`, `PARAM_MIX_DELAY_MOD`, `PARAM_MIX_DELAY_MOD_RATE`, `PARAM_MIX_DELAY_REV`, `PARAM_MIX_DELAY_VOL`) sont captures dans `PatternSaveV1.globals`.
- Les anciens IDs `PARAM_MIX_DELAY_SWING` et `PARAM_MIX_DELAY_ACCENT` restent dans le layout `PARAM_COUNT` comme tombstones reserves; ils ne sont plus reappliques comme globals utiles.
- `PARAM_MIX_DELAY_TIME` persiste la division musicale sync BPM, pas la duree calculee en ms/secondes.
- `PARAM_MIX_DELAY_TIME_R` persiste aussi une division musicale sync BPM; en DUAL/Tap elle sert de temps principal.
- `PARAM_MIX_DELAY_TYPE` persiste le choix `CLASSIC`/`DUAL`; le default est `CLASSIC`.
- Le restore pattern/projet les reapplique via `param_set()`, comme les autres globals utiles.
- `PARAM_COUNT` change avec ces nouveaux params; la version pattern et la version project sont incrementees pour refuser proprement les anciens payloads prototype de taille incompatible.
- `PATTERN_VERSION=10` et `PROJECT_V1_FILE_VERSION=13` marquent la rupture prototype ou le delay passe au contrat 8 params `TIME/X/WID/FDBK/HPF/LPF/REV/VOL`.
- Les params reverb globaux `PARAM_MIX_REVERB_HPF` et `PARAM_MIX_REVERB_LPF` sont captures dans `PatternSaveV1.globals`.
- `PARAM_COUNT` change avec ces deux globals; `PATTERN_VERSION=11` et `PROJECT_V1_FILE_VERSION=14` marquent la rupture prototype reverb HPF/LPF pre-reverb.
- `PATTERN_VERSION=12` et `PROJECT_V1_FILE_VERSION=15` marquent la rupture prototype DUAL send2 delay.
- Le retrait produit V1 de `SWING`/`ACCENT` ne change pas `PARAM_COUNT`; aucun bump pattern/projet supplementaire n'est requis.
- `PROJECT_V1_FILE_VERSION=16` marque la rupture prototype MACRO Scene/Switch 32 locks par scene: `ProjectSaveV1.macro` grossit, les anciens projets sont refuses proprement par version/payload_size, sans migration legacy.
- `PATTERN_VERSION=13` et `PROJECT_V1_FILE_VERSION=18` marquent le retrait des IDs/type/params `TB3`; aucun remap ni preservation des anciens projets/configs `TB3` ou `DX7` n'est conserve.
- `PatternSaveV1` ne change pas pour cette passe MACRO: les scenes/locks restent projet-only.



## Addendum 2026-04-29 - pattern_load deux temps

Contrat runtime pattern load:
- `pattern_load_request()` enregistre la demande bank/pattern sans apply live.
- `pattern_load_service()` est le seul point de progression SD du load pattern et produit un `PatternSaveV1` complet en RAM.
- `pattern_load_is_ready()` est une query pure.
- `pattern_load_take_ready()` transfere le snapshot RAM complet au seam musical.
- `pattern_live_service()` ne lit plus la SD pour appliquer un pattern; il arme `g_next_pattern` uniquement depuis RAM puis applique au boundary.
- Si le snapshot demande n'est pas pret au boundary, le pattern courant reste actif et la demande pending reste conservee.
- Le backend de lecture reste encore synchrone via `pattern_sd_bank_load_slot()` dans `pattern_load_service()`; le decoupage chunked futur doit rester derriere cette API.
Contrat Sampler persistence/cache:
- `sample_pool` est catalogue/projet/metadata: slot, path, metadata et etat de restauration.
- La memoire audio runtime appartient a `sample_page_cache`; `sample_cache` reste la facade/orchestration prepare-service-compat, et `sample_desc->data` est une compat legacy qui ne doit pas redevenir owner.
- Les snapshots projet persistent les paths WAV, pas l'audio brut.
- Les slots Sampler acceptent les WAV PCM ou extensible PCM compatible, 48 kHz uniquement, mono/stereo, 16/24/32-bit; les autres sample rates, WAV float/compresses et fichiers >2 canaux restent refuses avant prepare cache.
- `sample_pool_load()` ne publie plus de path/metadata de slot avant validation WAV compatible et preparation cache reussie; un WAV incompatible ou une conversion annulee/echouee ne doit donc pas creer de slot fantome.

TODO policy SD/projet:
- Project save/load pendant playback reste a refuser ou differer explicitement.
- Project save/load ne doit pas preempter `sample_cache_service()` quand un stream sample a besoin de refill.
- Project load ne doit jamais appliquer un etat partiel.
- Pendant Looper RAW record/finalize ou `looper_storage_raw_export_is_active()`, les entrees SD lourdes sont refusees: project save/load/delete/refresh, pattern load, pattern save direct, preview, scan catalogue et import WAV. Audio Rec `SAMPLE_WAV` ne bloque pas `pattern_load_request/service` par principe.
- Reste a faire: pattern save pending budgete et pattern load avec stop/finalize musical avant load/apply.
- Politique finale SD attendue: SAMPLE_CACHE prioritaire, PATTERN_LOAD entre refills, PATTERN_SAVE differe, PROJECT hors playback, PREVIEW exclusif, sans scheduler SD generique.

## Addendum 2026-05-06 - Master/FX UI-only

- Les params `PARAM_MASTER_FX1_*` a `PARAM_MASTER_FX4_*` sont ajoutes en fin d'enum et entrent dans les tableaux `PARAM_COUNT` existants.
- Les nouveaux snapshots/projets peuvent stocker ces valeurs via les flux parametres existants, mais le layout binaire `PARAM_COUNT` augmente.
- L'etat ROUT Master/FX reste UI-only local dans cette passe; il n'est pas encore persiste en pattern/projet.

## Addendum 2026-05-08 - contrat SD audio recording multi-client

Ce contrat documente la cible produit pour l'enregistrement audio vers SD. La phase courante branche le REC `Sampler/Looper` sur reservoir RAW; le writer Looper ne conserve plus de flux temporaire.

### Autorites

- Z1 reste l'autorite des producteurs audio hard-RT:
  - taps/captures depuis le pipeline audio,
  - copie vers ring RAM uniquement,
  - aucun acces FatFs, SDMMC, allocation dynamique ou lock bloquant.
- Z0 reste l'autorite de cadence des services hors IRQ:
  - ordre de service superloop,
  - budgets cooperatifs,
  - aucun scheduler SD parallele implicite.
- Z4 reste l'autorite transport/REC/boundaries:
  - stop de record a frontiere musicale quand demande produit,
  - `pattern load` pendant Looper RAW record/export passe par stop/finalize avant load/apply; Audio Rec `SAMPLE_WAV` le laisse cadence et s'en remet au gate SD.
- Z6 reste l'autorite SD/persistence:
  - `sd_access_gate` est l'arbitre SD effectif,
  - FatFs n'est pas reentrant (`_FS_REENTRANT=0`), donc un seul owner SD reel a la fois,
  - pattern/project/preview/import doivent cohabiter avec le futur writer audio global.
- Z2/Z5 restent les autorites track-aware/UI pour une future track looper:
  - choix family/type/capacite exposee via les contrats runtime/UI existants,
  - le looper n'est pas une architecture SD speciale, seulement un client du writer multi-record.

### Modele cible

- Format durable produit: WAV PCM stereo 24-bit / 48 kHz; format REC Looper courant: RAW systeme PCM24 stereo interleaved sans header.
- Modele cible:
  - `N producteurs audio -> rings RAM alignes int32 stereo par client -> multi_record_writer_service global -> fichiers WAV SD`.
- Le writer packe en PCM 24-bit hors IRQ juste avant `f_write`.
- Un record client peut representer:
  - une prise looper,
  - une track stem,
  - un bus/session recorder futur.
- Il ne doit pas exister un writer FatFs par track qui arbitre directement via `sd_access_gate`; l'arbitrage des fichiers record appartient au writer global multi-client.

### Ordre de service SD

Ordre produit attendu dans la superloop, sous budgets explicites:

1. `sample_cache_service(...)`
   - priorite absolue pour playback streaming.
   - Charge les pages RAM du Sampler; l'audio ne lit que RAM.
2. `multi_record_writer_service(...)`
   - budget courant 16384 octets par appel apres `sample_cache_service(32768U)`.
   - draine les rings record, par client le plus rempli, en tranche courte pour ne pas monopoliser la superloop/UI.
3. Operations interdites ou differees pendant Looper RAW active recording/finalizing:
   - project save/load,
   - pattern load/save direct,
   - preview SD,
   - scan library/catalogue,
   - imports/loads WAV non musicaux.

### Ring writer

- Chaque client record possede un ring RAM dedie.
- Format interne ring: stereo `int32_t` aligne, 48 kHz.
- Le push audio est borne:
  - copie bloc vers ring,
  - avance index atomique/volatile,
  - update counters simples.
- Aucun acces SD cote audio IRQ:
  - pas de FatFs,
  - pas de malloc,
  - pas de lock bloquant,
  - pas de formatage WAV,
  - pas de `f_open/f_write/f_sync/f_lseek/f_rename/f_unlink/f_expand`.
- Diagnostics obligatoires par client:
  - high watermark,
  - overflow count,
  - dropped frames,
  - failed/degraded take comme garde critique interne seulement,
  - bytes written,
  - last FatFs error.

### Debits et RAM

Debit WAV PCM stereo 24-bit / 48 kHz:
- 1 record: 288000 octets/s.
- 4 records: environ 1.15 MB/s write.
- 8 records: environ 2.30 MB/s write.
- 16 records: environ 4.61 MB/s write.

Pression RAM ring interne `int32_t stereo`:
- 1 record: 384000 octets/s.
- 250 ms par client: 96000 octets.
- 0.5 s par client: 192000 octets.
- 1 s par client: 384000 octets.
- 2 s par client: 768000 octets.
- 4 s utiles par client: 1536000 octets utiles, 1536008 octets alloues avec frame sentinel.

Exemples de RAM ring brute:
- 4 clients: environ 768 KB pour 0.5 s, 1.54 MB pour 1 s, 3.07 MB pour 2 s.
- 4 clients produit courant: `12001` frames allouees/client, environ 384 KB pour 250 ms utiles.
- 8 clients: environ 1.54 MB pour 0.5 s, 3.07 MB pour 1 s, 6.14 MB pour 2 s.
- 16 clients: environ 3.07 MB pour 0.5 s, 6.14 MB pour 1 s, 12.29 MB pour 2 s.

Ces chiffres n'incluent pas:
- buffers de pack PCM24,
- structures de fichiers,
- cache sample,
- fragmentation/latence SD,
- pression read `sample_cache_service` en parallele.

Aucun nombre de records simultanes ne doit etre promis sans benchmark sur carte cible avec:
- playback sample_cache actif,
- carte fragmentee et carte fraiche,
- pattern save opportuniste,
- cas de finalisation WAV,
- mesures high watermark/drop par client.

### Politique writer

- L'ecriture disque se fait par chunks alignes secteur quand possible.
- Le choix du prochain client prend le ring le plus rempli; ce point reste suffisant pour le client Looper unique courant.
- Si un client overflow:
  - incrementer les diagnostics critiques `overflow_count` / `dropped_frames`,
  - ne jamais bloquer l'audio,
  - traiter l'evenement comme bug d'architecture en usage supporte, pas comme workflow produit normal.
- Si plusieurs clients overflow:
  - escalader vers stop/finalize global ou fail des clients les plus critiques selon mode produit,
  - exposer l'etat a l'UI/diagnostics.
- Une carte lente ou bloquee ne doit jamais remonter en attente bloquante vers Z1.
- Pour LEN fixe Looper, la taille attendue est transmise au writer avant start pour borne dure d'ecriture; aucune preallocation monolithique n'est faite avant record, et le ring 4 s reste la garde contre une pause SD initiale. `f_sync` reste limite a la finalisation apres drainage.
- La finalisation WAV reste hors IRQ mais ne chaine plus toutes les operations metadata dans un seul appel service; chaque passage `multi_record_writer_service` execute au plus une phase de finalisation pour limiter le freeze UI/superloop.

### Contrat looper

- Le looper est un client du writer multi-record.
- `REC` utilise le slot RAW deterministe de la track Looper quand le storage RAW systeme est disponible; sinon le demarrage REC est refuse proprement.
- Si la validation boot des reservoirs RAW n'a pas encore abouti, le controle Looper retente `looper_storage_raw_validate()` hors IRQ au moment du REC avant de refuser avec une cause courte (`RAW MISS`, `RAW SIZE`, `RAW SLOT`, `RAW MOUNT`, `RAW BUSY`, `RAW STAT` ou `RAW INIT`).
- `STOP` draine le ring; en RAW il sync/close et fixe `recorded_frames`.
- `SAVE` RAW exporte `recorded_frames` vers un WAV durable STOP-only; il ne commit/rename aucun fichier temporaire Looper.
- `DELETE` futur retire immediatement la prise cote UI/audio; `f_unlink` SD est differe par service.
- Spam `REC/DELETE/SAVE`:
  - replace/cancel controle,
  - pas de creation infinie de fichiers durables,
  - un seul etat courant explicite par client looper.
- Boot/projet:
  - aucune restauration de reservoir RAW comme prise durable sans SAVE explicite.

### Contrat stem recorder

- Le stem recorder est aussi un ensemble de clients du writer multi-record.
- Start/stop global.
- Plusieurs clients record simultanes.
- Fichiers durables de session.
- Finalisation globale apres stop.
- Overflow marque par client:
  - un stem peut etre failed/degraded sans invalider automatiquement tous les autres,
  - overflow multiple ou ring global critique peut forcer stop/finalize session.

### Pattern save/load pendant record

- `SAVE PATTERN` pendant record est autorise.
- Capture RAM immediate:
  - le snapshot doit etre complet en RAM avant toute ecriture SD,
  - l'action musicale ne doit pas attendre l'ecriture disque.
- Ecriture SD:
  - temp file,
  - chunks budgetes,
  - sync/commit/rename opportunistes,
  - queue limitee (par exemple une demande pending, remplacement ou refus explicite).
- Etats UI possibles:
  - queued,
  - saving,
  - saved,
  - failed.
- `LOAD PATTERN` pendant record:
  - queue intention,
  - stop/finalize des records actifs a frontiere musicale si possible,
  - ensuite seulement load/apply pattern.

### Risques connus

- Latence non bornee de `f_open`, `f_write`, `f_sync`, `f_lseek`, `f_rename`, `f_unlink`.
- Carte lente ou fragmentee.
- Starvation de `sample_cache_service` si `sd_access_gate` ou l'ordre de service est mal arbitre.
- Overflow rings si la SD reste bloquee trop longtemps.
- Usure et latence en cas de spam `REC/STOP/SAVE/DELETE`.
- Sans preallocation monolithique, une carte lente ou fragmentee peut encore faire monter le watermark ring; tout overflow doit etre traite comme defaut critique de debit/ring, pas masque par une allocation FAT bloquante.

## Addendum 2026-05-08 - Sampler/Looper persistence skeleton

- `Sampler/Looper` est un client de `multi_record_writer`, pas une architecture SD speciale et pas un writer par track.
- Le controle Looper prepare le writer hors IRQ depuis le seam transport/control Z5: slot/path RAW via `looper_storage_raw_get_slot_for_track` et `multi_record_writer_prepare_raw` quand `transport running + REC global arme` et une unique Looper est eligible. START/STOP effectifs (`multi_record_writer_start` / `multi_record_writer_request_stop`) sont armes comme intentions et consommes par `brick6_looper_runtime` au boundary audio.
- Le bouton `REC` ne demarre pas directement le writer Looper; il conserve l'armement REC global du sequenceur, et le focus UI n'est pas une condition pour enregistrer une Looper armee.
- Si plusieurs Loopers sont eligibles, Z5 refuse de demarrer le client Looper unique courant plutot que de choisir silencieusement plusieurs sources de prise.
- Le SAVE Looper RAW est l'unique chemin actif: `SHIFT+SETTINGS` en Z5 refuse transport running, puis demarre `looper_storage_raw_export_start()` quand la prise courante est RAW `TAKE_READY`.
- Le path durable courant est fourni par `looper_storage_make_next_path`: `0:/PROJECT/LOOPS/LPRtt_nnnn.WAV`, avec `tt` = track Looper active et `nnnn` = compteur local scanne par `f_stat`.
- Le dossier `0:/PROJECT/LOOPS` est cree a la demande par `looper_storage_make_next_path` via le meme client SD recorder avant l'export.
- Apres un SAVE Looper reussi, Z5 notifie le catalogue WAV via `wav_loader_catalog_notify_file_created()` avec le path final ferme/verifie; si le catalogue est deja en cache, l'entree `LOOPS/LPRtt_nnnn.WAV` est ajoutee sans scan SD global, sinon le scan lazy de `Settings > SAMPLER > SD` la retrouvera.
- Les etats `RECORDING`, `STOP_REQUESTED`, `DRAINING` et `FINALIZING` restent des refus SAVE; `IDLE`, `FAILED`, une prise vide ou une prise finalisee appartenant a une autre track Looper sont traites comme absence de boucle sauvegardable.
- `LEN=Free` ne declenche aucun auto-stop.
- `LEN=1/2/4/8/16` est branche cote controle Z5 comme intention; le writer reste le client Z6 unique, et l'arret automatique passe par `brick6_looper_runtime` au boundary audio, sans SAVE, commit, rename ou acces FatFs supplementaire.
- Pour `PLAY=Auto` apres LEN fixe, le controle Z5 calcule la longueur attendue et arme le runtime Looper des le `request_stop` musical pour un depart START_RAM a playhead 0; `TAKE_READY` reste reserve a la finalisation RAW/backing storage et au SAVE, pas au premier depart live.
- Apres auto-stop LEN, Z5 inhibe un redemarrage automatique tant que REC global et transport restent actifs, afin de conserver la prise RAW finalisee pour SAVE et de ne pas relancer/effacer une prise sans transition utilisateur.
- La precision de duree depend du marker boundary audio consomme par Z1; Z6 ne porte pas l'autorite musicale et ne calcule pas les mesures.
- `ARM=Overd` reste borne/no-op: aucun overdub audio reel n'est branche.
- `PATTERN_VERSION=15` et `PROJECT_V1_FILE_VERSION=20` marquent le changement de layout `PARAM_COUNT`, l'ajout du type `Looper` et l'ajout de `PatternSaveV1.track_cfg.looper_route_enabled`.
- `PATTERN_VERSION=16` et `PROJECT_V1_FILE_VERSION=21` marquent la rupture prototype du contrat TONE Looper: `MODE` est retire, `ARM` devient `Off/Rec/Overd`, `PLAY Off/Auto` remplace l'ancien slot; les anciens fichiers sont refuses par version/payload stricts.

## 34. Versioning LFO final

- `PATTERN_VERSION=25` et `PROJECT_V1_FILE_VERSION=36` marquent la rupture prototype du contrat LFO final.
- `PatternSaveV1.mod` persiste maintenant, par LFO, `dest`, `rate`, `depth`, `shape`, `delay`, `trig`, `fade`, `phase_slew`.
- Les anciennes sauvegardes prototype sont refusees par version/payload stricts; aucune migration produit complexe n'est requise.
- La restauration LFO reste mono-autorite: `pattern_live_apply_snapshot` appelle uniquement `mod_lfo_v1_set_track_param`.
- `PATTERN_VERSION=17` et `PROJECT_V1_FILE_VERSION=22` marquent l'ajout de `PARAM_WAVE_PHASE_RESET` et le changement de layout `PARAM_COUNT`; les anciens fichiers prototype sont refuses par version/payload stricts.
- La selection ROUT `Sampler/Looper` est capturee/restauree par matrice `looper track -> source track` dans le snapshot pattern; les projets la portent via leur snapshot live embarque.
- Les etats internes writer (`TAKE_READY`, `FINALIZING`, etc.) restent caches; aucun etat `Temp/Saved/Finalizing` n'est expose comme param utilisateur Looper.
- Aucun parametre SAVE/STAT Looper n'est expose en TONE.

## Addendum 2026-05-09 - Sampler/Looper runtime transient

- Le REC Looper courant produit une prise RAW avec `raw_slot`, `raw_path` et `recorded_frames`; apres finalisation RAW OK, le controle notifie `brick6_looper_runtime_notify_raw_take_ready()`.
- `brick6_looper_runtime` ne garde plus de chemin WAV transient Looper actif: le REC RAW alimente le chemin RAW dedie sans parsing WAV.
- Les ids transients Looper commencent a `SAMPLE_PAGE_CACHE_LOOPER_ID_BASE` et ne sont pas des slots projet ni des entrees catalogue. La base est decouplee de la capacite projet; elle suit la plage hot/page-cache reservee au Sampler.
- `TAKE_READY` RAW envoie au runtime le path du reservoir et `recorded_frames`; si le playback START_RAM post-REC est deja arme ou en cours, cette notification attache seulement le backing RAW/page-cache sans reinitialiser le playhead ni redemarrer la prise.
- Un nouveau `ARM=Rec` reste un replace destructif cote controle: l'ancien reader transient est detache avant le start writer RAW. La nouvelle longueur utile est `recorded_frames`, consommee par le playback et utilisee pour le wrap.
- SAVE RAW export lit uniquement `recorded_frames` depuis le reservoir RAW, ecrit un WAV final PCM24 stereo 48 kHz avec `fmt `, chunk `JUNK` de padding et chunk `data` aligne a l'offset 512, copie par chunks bornes dans `looper_storage_raw_export_service()` et expose une progression UI par phase (`SAVE WAIT`, `SAVE OPEN`, `SAVE n%`, `SAVE VERIFY`, puis done/fail). Il ne renomme ni ne supprime le RAW et ne passe pas par sample_pool/catalogue/slot projet.
- La finalisation SAVE RAW verifie hors IRQ la taille data ecrite, l'offset data WAV fixe a 512 octets, puis compare les 16 premieres et 16 dernieres frames RAW contre la zone data WAV; un mismatch bascule l'export en erreur `VERIFY_FAIL` et conserve un snapshot diagnostic.
- Le SAVE RAW exporte directement le reservoir RAW vers le path final et laisse le playback transient RAW courant attache au reservoir.
- La notification catalogue apres SAVE reste seulement pour l'affichage `Settings > SAMPLER > SD`; elle ne devient pas l'autorite du playback Looper.
- Le service SD reste ordonne par `brick6_app_process`: le writer record est servi avant tout pour terminer un drain/finalize deja actif; quand un SAVE RAW -> WAV est actif, l'export devient prioritaire avec budget `516096U` et suspend sample cache, refill Looper, pattern load et preview SD. Hors export actif, l'ordre normal redevient sample cache, refill Looper, export opportuniste si aucun refill Looper n'est pending, puis pattern load. `sample_cache_service` ne service que la plage hot `0..SAMPLE_CACHE_HOT_SAMPLE_CAPACITY-1`; le Looper service la plage transient `SAMPLE_PAGE_CACHE_LOOPER_ID_BASE..SAMPLE_PAGE_CACHE_LOOPER_ID_BASE+SEQ_TRACK_COUNT-1`.
- Pendant la phase COPY, l'export garde le gate SD pour une tranche budgetee complete et peut copier huit blocs de `64512` octets maximum par appel service. Le contexte `g_looper_raw_export`, le buffer I/O SAVE `g_looper_raw_export_io` et le diagnostic `g_looper_raw_export_diag` sont froids, hors IRQ, places en SDRAM dediee `RECORDER_SCRATCH_SDRAM` et alignes 32 pour preserver RAM_D1 aux etats audio hot. Le chunk est multiple de 512 octets et de 6 octets/frame PCM24 stereo, et le premier write audio commence a l'offset WAV 512, ce qui conserve les offsets secteur alignes. Le diagnostic export expose les compteurs `chunks_copied`, `bytes_copied`, `service_calls`, `gate_acquire_count`, les temps cumules open/read/write/copy/sync/verify/close/total et le dernier `FRESULT`.
- Les logs UART SAVE Looper sont des traces de debug compile-time desactivees par defaut (`LOOPER_STORAGE_EXPORT_UART_LOG=0`); le comportement produit garde uniquement le diagnostic froid `g_looper_raw_export_diag`.
- Le demarrage SAVE refuse un writer recording/finalizing, refuse transport running cote UI, stoppe une preview SD active avant reservation du path final, puis bloque les entrees project/pattern/import/scan via `looper_storage_raw_export_is_active()`. L'export ne cede plus a `sample_cache_has_pending_sd_work()`; un refus prolonge avant ouverture bascule en erreur `WAIT_TIMEOUT` pour ne pas laisser l'etat actif indefiniment.
- Limite restante: le refill Looper partage le budget global du `sample_page_cache`; une page manquante produit un silence local jusqu'au prochain refill.
- Le silence de page manquante ne bloque pas la position de lecture Looper: le playhead continue, et la demande de pages couvre une fenetre ahead modulo pour anticiper les pages suivantes et le wrap de boucle.
- Le premier playback `PLAY=Auto` post-REC ne rattrape plus un retard par avance de playhead et ne depend plus de `TAKE_READY` quand START_RAM est disponible. Un preroll RAM statique de 1 s stereo `int32_t`, capture pendant le REC depuis le flux deja envoye au writer, sert de source de depart pendant que le RAW/page-cache se prepare. Le RAW systeme reste l'autorite complete/backing storage; si le backing RAW n'est pas encore attache a la sortie du preroll, l'audio ne bloque pas et le playhead attend. Une fois le backing RAW attache, les miss page-cache relevent du chemin normal Looper: silence local, playhead avance, refill hors IRQ.
- Le preroll RAM n'est pas une source de boucle permanente: apres le premier bloc lu depuis le RAW/page-cache, le runtime marque le relais RAW comme effectue et interdit toute reutilisation du preroll aux wraps suivants. La frame 0 apres wrap doit donc venir du RAW/page-cache.

## Addendum 2026-05-13 - rupture stockage retrait buffer master

- `PATTERN_VERSION=18` et `PROJECT_V1_FILE_VERSION=23` marquent le retrait du type UI/runtime buffer master et des anciens params buffer de `PARAM_COUNT`.
- Aucune migration legacy n'est conservee: les anciens patterns/projets prototype sont refuses par version/payload stricts.
- Le XFade Looper persiste via `PARAM_LOOPER_XFADE` dans le layout courant.

## Addendum 2026-05-13 - metadata musicale Looper transient

- La prise Looper RAW transient porte maintenant une metadata runtime minimale pour un futur stretch Looper: frames enregistrees, duree musicale Q16, cadence source au REC et samples absolus start/stop.
- Cette metadata reste runtime/transient dans cette passe: le RAW systeme n'est pas restaure comme prise durable au load projet, et SAVE RAW -> WAV continue de lire uniquement `recorded_frames` pour exporter l'audio.
- Aucun changement de layout `PARAM_COUNT`, `PatternSaveV1` ou `ProjectSaveV1` n'est introduit par cette passe; aucun bump `PATTERN_VERSION` / `PROJECT_V1_FILE_VERSION` n'est requis.

## Addendum 2026-05-13 - params Looper STRETCH UI/state

- `PATTERN_VERSION=19` et `PROJECT_V1_FILE_VERSION=24` marquent l'ajout de `PARAM_LOOPER_STRETCH`, `PARAM_LOOPER_PITCH` et `PARAM_LOOPER_GRAIN` au layout `PARAM_COUNT`.
- Les anciens patterns/projets prototype sont refuses proprement par version/payload stricts; aucune migration legacy ni tombstone n'est conserve.
- Les nouveaux params Looper persistent via les flux `PARAM_COUNT` existants, mais restent UI/state uniquement: aucun etat REC/PLAY/WRAP/SAVE/XFADE/ROUT ni metadata RAW n'est modifie par cette passe.
- `PROJECT_V1_FILE_VERSION=25` marque l'ajout du bloc projet `Sampler/Multi` par track (`path .brickmulti` + gain). Les anciens projets prototype sont refuses proprement par version/payload stricts.

## Addendum 2026-05-15 - browser Samples split

- Le browser Settings/Samples liste la bibliotheque durable sous `0:/Samples` avec navigation dossier locale a Z5.
- Les paths WAV complets restent transmis a `sample_pool_load()` et `sd_preview_begin()`; seul le label UI retire `.wav`.
- Les slots projet affiches a droite restent les slots `sample_pool` existants et continuent d'etre captures/restaures par `ProjectSaveV1.sample_pool`.
- Le clear de slot appelle seulement `sample_pool_clear()` et ne supprime jamais le fichier SD source.
- Aucun changement de layout `PatternSaveV1`, `ProjectSaveV1`, `PARAM_COUNT`, `PATTERN_VERSION` ou `PROJECT_V1_FILE_VERSION` n'est requis.

## Addendum 2026-05-15 - browser Multi-Sample Settings

- Le browser Settings/Multi-Sample expose les dossiers sous `0:/Multi/` et n'affiche pas les WAV internes; un dossier avec WAV directs est un Multi chargeable, tandis qu'un dossier sans WAV direct mais avec sous-dossiers reste un dossier de navigation. La qualification ne descend pas recursivement.
- La preparation d'un dossier non indexe appelle `multi_sample_import_folder()` et produit/met a jour `<instrument>/<instrument>.brickmulti`.
- La variante UI `multi_sample_import_folder_with_progress()` expose uniquement une progression froide hors IRQ: nombre de WAV directs traites, puis finalisation zones/index. Elle ne change pas le format `.brickmulti` ni le chemin loader.
- Le load de slot appelle `multi_sample_load_instrument()` sur le `.brickmulti` et s'appuie sur `multi_sample_service_load()` en superloop pour le chargement cooperatif page0.
- Le pool projet affiche et borne la consommation `multi_sample_pool` en samples sur `MULTI_SAMPLE_POOL_MAX_SAMPLES=512`; un index/import dont le nombre de samples depasse 512 est refuse avant load.
- Les slots instruments portent maintenant le path `.brickmulti` en RAM froide pour eviter les doublons dans la session projet courante; ce path ne modifie pas le layout fichier projet.
- L'assignation durable `track Sampler/Multi -> path .brickmulti` est mise a jour par le browser Settings/Multi-Sample lorsqu'un slot est charge ou reutilise pour la track active Multi; le restore recharge ce path puis rebranche l'id instrument runtime.
- Aucun changement `PatternSaveV1`, `ProjectSaveV1`, `PARAM_COUNT`, `PATTERN_VERSION` ou `PROJECT_V1_FILE_VERSION` n'est requis.

## Addendum - cache waveform persistant `.brkwave`

- `waveform_cache` est une autorite Z6 reconstruisible pour les apercus waveform SD; il stocke uniquement des fichiers caches sous `0:/BRICK/.wavecache/` et ne modifie jamais les WAV utilisateur.
- Le boot cree/verifie seulement `0:/BRICK` et `0:/BRICK/.wavecache`; aucun scan sample, aucune validation de cache par sample et aucune generation waveform ne sont faits au boot.
- Le format V1 `.brkwave` contient un header `BRKWAVE`, etat `BUILDING/READY/INVALID`, `sample_id[16]`, hash de path normalise, taille WAV, `data_offset`, frames, format audio, hash debut/fin 64 KiB, puis une table de niveaux et des colonnes `{min,max}` `int16`.
- Les niveaux generes par defaut sont fixes: 16384, 4096, 1024 et 256 frames/colonne. Le niveau 64 frames/colonne reste reserve dans le format mais n'est pas genere en V1.
- Politique hybride V1: un `.brkwave` persistant n'est produit que pour les WAV de duree `>= 60 s`. Le seuil public est `WAVEFORM_CACHE_PERSIST_MIN_FRAMES = 48000 * 60` par defaut, ou `sample_rate * 60` quand le sample rate est connu.
- Les demandes viennent de l'ouverture/chargement sample, de la finalisation Audio Rec et de la fin de SAVE Looper. Elles sont servies hors IRQ via `SD_ACCESS_CLIENT_WAVEFORM_CACHE`, apres sample streaming et writer/finalize critiques. Quand la duree est connue par le caller (`sample_pool_load`, SAVE/Rec Edit Audio Rec, SAVE Looper), `waveform_cache_request_for_wav_known_duration()` filtre les samples courts avant queue. Quand elle n'est pas connue, la validation WAV abandonne proprement les samples courts sans creer de fichier.
- `waveform_cache_request_for_wav()` reste une demande RAM-only: copie du path dans la queue, aucun FatFs, aucun `sd_access_gate`, aucune validation WAV. Il refuse comme no-op non fatal les chemins temporaires (`AUDIOREC_TMP.WAV`, `_TMP`, `0:/PROJECT/REC/*`). Pour la raison `POST_AUDIO_REC`, le service est differe afin que Rec Edit n'attende jamais `.brkwave`. Les samples courts utilisent les caches volatils existants (`overview` RAM, cache editor tuile, `OLD_AUDIO_TILE`/fallback renderer) et ne declenchent pas de suppression automatique d'anciens `.brkwave`.
- Un cache absent, perime, incomplet ou reste en `BUILDING` est supprime puis regenere; les projets/patterns ne dependent jamais de sa presence.
- Le service expose un LRU RAM SDRAM de 64 tuiles `.brkwave` de 512 colonnes min/max, soit 128 KiB de donnees plus metadata froide. Les requetes de tuiles visibles sont servies hors IRQ/FatFs draw par le meme client SD et peuvent etre abandonnees visuellement sans bloquer l'editeur.

## Addendum 2026-05-17 - retrait Synth historique

- PATTERN_VERSION=20 et PROJECT_V1_FILE_VERSION=26 marquent le retrait du moteur Synth prototype retire et de ses anciens params TONE de PARAM_COUNT.
- Aucune migration legacy n'est conservee: les anciens patterns/projets prototype sont refuses par version/payload stricts.

## Addendum 2026-05-18 - paths Sampler catalogue/load/preview

- Le contrat path produit du Sampler classique est aligne sur le catalogue WAV V2: `SAMPLE_POOL_PATH_MAX=160`.
- `sample_pool`, `sample_cache`, `sample_page_cache`, `sample_stream_manager` et `sd_preview` acceptent le meme path complet que `SAMPLE.CAT`; il n'y a plus de limite cachee a 64 ou 96 caracteres sur LOAD/preview apres affichage catalogue.
- Les buffers path chauds restent des metadonnees froides SDRAM cote pool/cache/page-cache/preview; les strings de readers de stream actifs sont aussi separees en `SDRAM_SAMPLES`, hors IRQ audio critique, tandis que les handles `FIL` des readers STREAM sont en `STORAGE_STATE_SDRAM` avec init explicite au boot.
- `PROJECT_V1_FILE_VERSION=27` marque la rupture prototype du snapshot projet `sample_pool` due a l'extension des paths de slots Sampler. Les anciens projets prototype sont refuses par version/payload stricts, sans migration legacy.

## Addendum 2026-05-18 - erreurs catalogue/preview non destructives

- `wav_loader_catalog_load_view()` construit une vue de dossier dans un scratch SDRAM et ne remplace une entree LRU valide qu'apres lecture complete de `SAMPLE.CAT`; un `f_open`/`f_read` en erreur conserve donc l'ancienne vue valide.
- Les diagnostics froids `preview_open_fail_count`, `load_open_fail_count`, `catalog_open_fail_count`, `catalog_view_preserved_on_error_count` et `gate_release_on_error_count` gardent le path, le client gate courant/dernier et le `FRESULT` de la derniere erreur d'ouverture.
- `sample_cache_prepare()` preflight le path avant de liberer l'ancien slot cache, afin qu'un refus gate/mount/open initial ne vide pas un slot deja charge.

## Addendum 2026-05-21 - autoload slots sample projet

- `PROJECT_V1_FILE_VERSION=28` marque l'ajout du bloc `ProjectSaveV1.sample_autoload`. Les anciens projets prototype restent refuses par validation stricte `version/payload_size`, sans migration legacy.
- Migration Sampler Stream en cours: `PARAM_SAMPLER_SAMPLE` hors Multi porte deja un slot global actif. `PATTERN_VERSION=22` et `PROJECT_V1_FILE_VERSION=31` refusent les anciens payloads prototype dont cette valeur designait encore directement un index backend `sample_pool`.
- `PROJECT_V1_SAMPLE_AUTOLOAD_VERSION=3` stocke l'identite `global_index` en plus du `slot_index` backend. Au restore Stream, le backend `sample_pool[SAMPLE_POOL_SIZE]` est restaure puis le slot global STREAM est force sur son `global_index` sauvegarde; Multi conserve son backend technique et capture aussi le `global_index`; RAM recharge son backend `sampler_ram_pool` au `slot_index` sauvegarde et force le slot global sauvegarde.
- Le bloc liste des entrees bornees `{slot_index, global_index, kind, flags, path}` avec `kind=STREAM`, `MULTI` ou `RAM`. `PROJECT_V1_FILE_VERSION=32` marque la rupture prototype qui remplace la reservation RAM future par des slots RAM autoloadables.
- Les entrees `STREAM` mirroring les slots `sample_pool` portent le path WAV complet et restent restaurees par le chemin `sample_pool_restore_project_snapshot()` existant.
- Les entrees `MULTI` portent le path `.brickmulti` et restaurent les slots `multi_sample_pool` par `multi_sample_load_instrument(path, slot_index)` apres apply projet. Le chargement page0 reste cooperatif via `multi_sample_service_load()`; un path absent/invalide met seulement le diagnostic restore en erreur et ne crashe pas.
- Les entrees `RAM` portent le path WAV complet et restaurent les slots `sampler_ram_pool` par `sampler_ram_pool_load_wav_at(slot_index, global_index, path)` hors IRQ. Aucun audio brut n'est sauvegarde; `cost_bytes` est recalcule au reload comme `pages allouees * SAMPLE_PAGE_BYTES`. Un echec d'ouverture/parse/decode/budget/SLOT_POOL/backend conserve si possible le slot global RAM en `ERROR` avec le path, sans pointeur audio stale.
- Le bloc sert de source explicite pour la phase boot/autoload UI: la lecture projet, les loads STREAM/RAM synchrones et les loads MULTI cooperatifs alimentent la progression. RAM vaut une unite utilisateur terminee a la fin de son load synchrone.

## Addendum 2026-05-23 - Sampler/Multi LOOP

- `PATTERN_VERSION=21` et `PROJECT_V1_FILE_VERSION=29` marquent l'ajout append-only de `PARAM_SAMPLER_MULTI_LOOP` dans le layout `PARAM_COUNT`; les anciens patterns/projets prototype sont refuses par version/payload stricts.
- `MULTI_SAMPLE_INDEX_VERSION=2` marque l'ajout des metadonnees de loop WAV par sample dans `.brickmulti`; la lecture v1 reste acceptee avec `has_loop=0`.

## Addendum 2026-05-24 - catalogue global sample produit

- `sample_global_pool` ajoute l'autorite catalogue/budget produit au-dessus des backends existants: catalogue final 256 slots globaux, capacite active derivee du pool page-cache produit courant (`SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS`, 240 avec la config actuelle), budget utilisateur 16 MiB, kinds `EMPTY/STREAM/MULTI/RAM`, avec `backend_index` separe du slot global.
- `STREAM` reste represente par un slot backend `sample_pool`, dimensionne a la capacite active courante pour que le backend Classic couvre les slots globaux `STREAM`; `MULTI` reste represente par un `multi_sample_pool` instrument id; `RAM` est represente par un slot interne volatile `sampler_ram_pool`, lui aussi dimensionne sur la capacite active courante. Les limites de 16 pads/voix/pages UI ne bornent pas le nombre de samples RAM residents. Aucun chemin audio Stream/Multi, page-cache, runtime Multi, RAM normal ou sliced n'est remplace par cette couche.
- Le cout permanent global compte uniquement les slots produits charges: Stream/Multi gardent le cout de presocle page-cache valide, RAM compte sa taille physique reelle en pages `SAMPLE_PAGE_SLOT_POOL` allouees. RAM est stocke en `FLOAT32_INTERLEAVED` stereo au load WAV; les anciens slots residents sont volatils et sont donc simplement recharges depuis leur path projet/autoload. Les fenetres voix actives, Multi LOOP, window locks, pages queued/loading et marges runtime restent hors cout permanent.
- `Settings > Sample` lit maintenant l'en-tete budget depuis `sample_global_pool` (`used_slots/capacite active`, `used_bytes/16 MiB`). Le format projet courant est v35: le restore reset le catalogue global puis restaure explicitement les slots globaux Stream sauvegardes et les slots RAM autoloades.

- `PATTERN_VERSION=23` et `PROJECT_V1_FILE_VERSION=33` marquent le retrait prototype de `PARAM_SAMPLER_FADE_IN` / `PARAM_SAMPLER_FADE_OUT` de `PARAM_COUNT`; aucune migration ancienne valeur fade n'est conservee.
- `PATTERN_VERSION=24` et `PROJECT_V1_FILE_VERSION=34` marquent l'ajout append-only de `PARAM_SAMPLER_LOOP_START` dans le layout `PARAM_COUNT`; les anciens patterns/projets prototype sont refuses par version/payload stricts.
- `PROJECT_V1_FILE_VERSION=35` marque l'alignement du backend volatile `sampler_ram_pool` sur la capacite active du catalogue global sample; les anciens projets prototype sont refuses par version/payload stricts.

## Addendum 2026-05-27 - contrat runtime du projet blank

- `project_v1_load_blank()` neutralise maintenant hors IRQ les etats transitoires non persistables avant de recharger le boot snapshot: preview SD, voix/readers Sampler, runtime Looper, runtime Wave/Braids, voix Drum, mixer lanes/sends, buffers reverb/delay, XFade Looper, MacroFX et bases track sound/tone.
- Le reset lourd precede `pattern_live_apply_boot_snapshot()`; le boot snapshot reste l'autorite des defaults persistables et reprojette ensuite UI/runtime/params.
- Depuis `Settings > Project > Load > Blank`, le retour UI force `CFG` pour eviter de rendre une page template devenue invalide apres remise a `Off` des tracks.

## Addendum 2026-05-27 - Patch V1

- `patch_v1` ajoute une persistence Z6 separee de Project/Pattern: un Patch est la photo canonique d'une seule track.
- Les slots Patch sont des fichiers indexes sous `0:/BRICK/PATCH/P0000.B6P`, format magic `B6PT`, version `1`, header metadata et checksum du payload.
- Payload V1: family/type/source/name, `track_sound_state_t`, `track_tone_sound_state_t`, deux lanes LFO capturees via `mod_lfo_v1_get_track_param`, et une reference asset Sampler optionnelle issue de `sample_global_pool`.
- Aucun audio brut, sequence, pattern, p-lock, playhead, voice, reader SD, page-cache, buffer audio, pointeur runtime ou etat IRQ n'est capture.
- `patch_sd_bank` utilise `SD_ACCESS_CLIENT_PATCH`; les acces FatFs restent hors IRQ et exclusifs vis-a-vis des clients SD critiques.
- L'apply minimal `patch_v1_apply_slot_to_track` charge un slot, valide header/checksum/payload, neutralise les notes/voix de la target, applique family/type via les autorites UI/track_state existantes, rafraichit `track_runtime`, restaure les etats canoniques sound/tone, restaure les LFO via `mod_lfo_v1_set_track_param`, puis reapplique les params autorises via `param_registry_apply_track_value`.
- La banque Patch V1 expose un slot state minimal `EMPTY/VALID/INVALID`, `patch_sd_bank_rename_slot` et `patch_sd_bank_delete_slot`. Rename recharge le payload, modifie `meta.name` et reecrit le meme fichier avec checksum recalcule; delete supprime le fichier de slot et met le slot en `EMPTY`.
- Le slot courant reste l'index choisi par le browser/save/apply. Save direct ecrit le slot courant s'il est utilisable, sinon le premier slot vide; apply positionne le slot courant sur le slot applique; delete choisit le prochain slot valide ou conserve le slot devenu vide.
- Les filtres/tri Patch Assign utilisent uniquement le cache metadata/header de `patch_sd_bank`; aucun payload lourd n'est charge pour construire la liste. Les filtres precis Family/Type listent uniquement les slots `VALID` compatibles; `BAD PATCH` et `EMPTY` restent visibles seulement dans la vue `ALL/ALL`, apres les patches valides.
- Le multi-target Patch Assign ne change pas le format Patch: un fichier `.B6P` reste mono-track. L'UI applique le meme slot sequentiellement vers les targets cochees via `patch_v1_apply_slot_to_track`, dans l'ordre croissant des track id, et continue apres un echec partiel sans rollback.
- La reference asset Sampler est resolue uniquement si l'asset est deja present et `READY` dans `sample_global_pool` avec kind/path compatible; aucun reload FatFs, page-cache, reader ou voice n'est cree par l'apply Patch.
- Preview, rollback, reload asset Sampler complet, filtres avances et Kit restent hors perimetre V1.
- Le niveau produit `Set` est supprime du contrat: l'extension future se limite au `Kit` comme groupe de patches/tracks.

## Addendum 2026-05-29 - Kit V1 étape 2

- `kit_v1` ajoute une persistence Z6 séparée de Project/Pattern/Patch: un Kit est un snapshot sonore complet de la machine, pas un Set partiel et pas une collection de targets sélectionnables.
- Les slots Kit sont des fichiers indexés sous `0:/BRICK/KIT/K0000.B6K`, format magic `B6KT`, version `1`, header metadata (`name[32]`, `track_count`, summary compact 16 tracks max), `payload_size` et checksum du payload.
- Payload V1: pour les tracks `0..UI_TRACK_COUNT-1`, capture family/type, `track_sound_state_t`, `track_tone_sound_state_t`, deux lanes LFO via `mod_lfo_v1_get_track_param`, référence asset Sampler optionnelle issue de `sample_global_pool`, et summary family/type/label/off pour miniature future.
- Aucun pattern, séquence, p-lock, playhead, transport, voice, reader SD, page-cache, buffer audio, état IRQ ni état UI temporaire n'est capturé.
- `kit_sd_bank` utilise `SD_ACCESS_CLIENT_KIT`; les accès FatFs restent hors IRQ et exclusifs vis-à-vis des clients SD critiques.
- Étape 2 expose capture + save direct + browser metadata, miniature summary, rename et delete. Apply complet, apply partiel, preview, rollback et asset reload restent hors périmètre. Rename recharge le payload, met à jour `meta.name`, puis réécrit header/payload avec checksum recalculé; delete supprime le fichier et invalide le cache metadata en `EMPTY`.
