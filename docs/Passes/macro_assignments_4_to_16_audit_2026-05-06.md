# Passe - Audit MACRO 4 -> 16 assignations

## Verdict court

Le passage de 4 a 16 assignations par macro n'est pas un simple changement de constante isole.
La limite reelle est `PROJECT_V1_MACRO_SLOT_COUNT` dans `Inc/Storage/project_v1.h`; elle est consommee a la fois par le stockage projet, le runtime `param_macro`, l'UI d'assignation, le feedback hall/renderer et le clipboard macro.

Recommandation: faisable avec migration/bump projet minimal, sans refonte audio ni changement Hall/scenes, mais avec une passe UI explicite pour rendre 16 slots par macro editables/visibles proprement.

## Autorite de la limite 4

- Constante canonique: `PROJECT_V1_MACRO_SLOT_COUNT 4U` dans `Inc/Storage/project_v1.h`.
- Modele projet: `project_v1_macro_state_t` = `banks[16] -> macros[4] -> slots[4]`.
- Runtime: `Src/Param/param_macro.c` dimensionne `last_resolution[PROJECT_V1_MACRO_SLOT_COUNT]` et boucle sur cette constante dans release/apply.
- UI Macro Slot: `Src/UI/ui_macro_interaction.c` mappe un hall en `macro = hall / PROJECT_V1_MACRO_SLOT_COUNT` et `slot = hall % PROJECT_V1_MACRO_SLOT_COUNT`.
- Clipboard: `Src/UI/ui_core_clipboard.c` copie/colle/clear uniquement le slot actif resolu par `ui_macro_interaction`.
- Renderer: `Src/UI/ui_renderer_template.c` affiche le slot-lock et la scene value du slot actif, pas une liste de slots.
- Page `Macro CFG`: `Src/UI/pages/ui_page_template_macro.c` ne porte que `Hall Switch Mode`; aucune navigation de slots macro supplementaire n'existe.

## Structures et fichiers concernes

- `Inc/Storage/project_v1.h`: constantes, `project_v1_macro_slot_t`, `project_v1_macro_t`, `project_v1_macro_bank_t`, `project_v1_macro_state_t`, `ProjectSaveV1`, `PROJECT_V1_FILE_VERSION`.
- `Src/Storage/project_v1.c`: init triple boucle, validation bank/macro/slot, get/set slot, capture/apply projet, blank project.
- `Src/Storage/project_sd_bank.c`: validation stricte `hdr->payload_size == sizeof(ProjectSaveV1)` et `hdr->version == PROJECT_V1_FILE_VERSION`.
- `Src/Param/param_macro.c`: cache runtime par pot, release, apply, sync active bank.
- `Src/Core/brick6_master_control.c`: 4 macro pots runtime, appel superloop de `param_macro_set_amount(macro, amount)`.
- `Src/UI/ui_macro_interaction.c`: grammaire d'assignation, mapping hall -> macro/slot.
- `Src/UI/ui_core.c`: en mode Bank, hall press choisit `active_bank`; en mode Slot, les events restent consommes par le mode macro.
- `Src/UI/ui_hall_input_service.c`: press/release hall en mode MACRO arme/finalise la capture.
- `Src/UI/ui_renderer_template.c`: feedback de slot-lock et scene value pendant la capture.
- `Src/UI/ui_core_clipboard.c`: copy/paste/clear du slot macro actif.

## Memoire

Hypothese ABI courante: `param_id_t` est `uint16_t`, `float` aligne 4, enum C non force sur 1 octet.

- `project_v1_macro_slot_t`: 8 octets (`track`, padding, `param`, `scene_value`).
- Etat projet macro actuel: 16 banks * 4 macros * 4 slots * 8 = 2048 octets, plus header d'etat env. 8 octets, soit env. 2056 octets dans `g_project_macro_state` et dans `ProjectSaveV1`.
- Etat projet macro en 16 slots: 16 * 4 * 16 * 8 = 8192 octets, plus header env. 8 octets, soit env. 8200 octets.
- Delta projet/RAM par instance `project_v1_macro_state_t`: env. +6144 octets.
- `param_macro_resolution_t`: env. 24 octets.
- Cache runtime actuel `g_param_macro_pots[4]`: env. 4 * (amount + valid + padding + 4 resolutions) = env. 416 octets.
- Cache runtime avec 16 slots: env. 1568 octets, delta env. +1152 octets.

Impact global: acceptable en SDRAM/RAM statique, mais `ProjectSaveV1` grossit d'env. 6 KiB; les fichiers projet et les buffers de travail projet grossissent aussi. `PatternSaveV1` ne change pas.

## CPU / worst-case

Chemin runtime actuel:
- `brick6_master_control_process()` est appele dans `brick6_app_process()` en superloop, pas dans l'IRQ audio.
- Chaque pot macro ne declenche `param_macro_set_amount()` que si son step 0..511 change.
- `param_macro_set_amount()` appelle `param_macro_apply_pot()`.
- `param_macro_apply_pot()` fait d'abord `param_macro_release_pot()` sur tous les slots du macro, puis reparcourt tous les slots pour resolve + lerp + apply.

Worst-case actuel par pot change: jusqu'a 4 releases + 4 applies.
Worst-case 16 slots par pot change: jusqu'a 16 releases + 16 applies.
Worst-case si les 4 pots changent dans le meme tour superloop: 64 releases + 64 applies.

Ces boucles restent bornees par constantes compile-time. Aucun malloc, aucune boucle non bornee observee. Le cout peut toutefois etre sensible en superloop parce que chaque slot peut passer par resolution track-aware, lecture base, validation p-lock/runtime, puis apply backend. Si les 64 slots ciblent des params lourds ou MIDI/PLAY/MOD, le cout est borne mais plus visible en latence UI/control-rate.

## Risque audio hard-RT

Pas de parcours macro dans l'IRQ audio trouve.
Le chemin pot -> macro passe par superloop (`brick6_app_process` -> `brick6_master_control_process`), puis Z3 apply normal/preview. Les backends audio sont commandes hors IRQ; l'audio IRQ consomme ensuite les etats runtime.

Risque hard-RT direct: faible.
Risque indirect: hausse de charge superloop/control-rate si les 4 pots bougent ensemble et si 64 assignations provoquent des writes runtime nombreux. Cela peut degrader la cadence UI/service, mais ne cree pas de boucle non bornee ni de blocage SD/flash.

## Persistence / versioning / format SD

- Les macros sont projet-only: `ProjectSaveV1` contient `project_v1_macro_state_t macro`.
- Les patterns ne capturent pas le bloc MACRO: `PatternSaveV1` ne contient pas de champ macro.
- `pattern_live_ram` et `pattern_sd_bank` ne sont pas impactes par le passage 4 -> 16.
- `project_sd_bank` valide strictement `PROJECT_V1_FILE_VERSION` et `sizeof(ProjectSaveV1)`.
- Changer `PROJECT_V1_MACRO_SLOT_COUNT` change `sizeof(ProjectSaveV1)` et donc le payload projet SD.
- Sans migration ou bump, les anciens projets sont refuses par taille/version.

Conclusion persistence: il faut au minimum bumper `PROJECT_V1_FILE_VERSION`. Une migration simple 4->16 est possible: lire ancien bloc, copier les 4 premiers slots de chaque macro, initialiser les 12 nouveaux slots vides. Mais le code actuel ne contient pas cette compat pour `ProjectSaveV1`.

## Pattern live RAM / undo

- `pattern_live_capture_current()` ne capture pas les macros.
- `pattern_live_apply_snapshot()` ne restaure pas les macros.
- `undo_v2_snapshot_payload_t` contient deux `PatternSaveV1`; donc les snapshots undo ne couvrent pas les macros projet.
- Les edits macro slot via `project_v1_macro_set_slot()` ne demarrent pas de transaction undo dediee dans le code observe.
- Les sources enum incluent `UNDO_V2_SOURCE_MACRO`, mais aucun call-site macro ne l'utilise actuellement.

Impact 4->16: pas d'impact direct sur `pattern_live_ram` ni sur la taille des snapshots undo. Le comportement actuel reste: les edits de slots macro ne sont pas undoables par snapshot pattern.

## UI

Etat actuel:
- 16 halls en mode `Slot` sont decoupes en 4 macros * 4 slots.
- 16 halls en mode `Bank` selectionnent 16 banks.
- `Macro CFG` expose seulement `Hall Switch Mode`.
- Le renderer template affiche le slot-lock du slot actuellement maintenu et la valeur scene temporaire.
- Le feedback LED MACRO lit `project_v1` pour bank active et slots projet.

Passer a 16 slots par macro casse le mapping hall direct actuel: `macro = hall / slot_count` avec `slot_count=16` donnerait seulement `macro 0`, `slot 0..15` pour les 16 halls. Les macros 1..3 ne seraient plus accessibles par le meme geste.

Conclusion UI: une adaptation UI est obligatoire si on conserve 4 macro pots et 16 assignations par macro. Elle peut rester minimale, sans changer le modele Hall/scenes: ajouter une notion de page/groupe de slots, ou reutiliser le mode Bank/Slot avec une selection de macro courante, mais ce n'est pas une simple constante.

## Conflits entre macros

Aucune resolution de conflit explicite n'est observee quand plusieurs macros ciblent le meme parametre.

Ordre effectif:
- `brick6_master_control_process()` parcourt les 4 macros dans l'ordre 0..3 si leurs pots ont change.
- `param_macro_sync_active_bank()` reapplique aussi les macros 0..3.
- Le dernier apply gagne pour une meme cible runtime.
- La release d'un macro restaure les valeurs `base_value` memorisees dans son dernier cache local, ce qui peut interagir avec un autre macro ciblant le meme parametre.

Le passage a 16 augmente la probabilite de collisions sans changer la semantique. A documenter ou garder comme contrat "last writer wins"; ne pas introduire une nouvelle autorite canonique dans `param_macro`.

## Assignabilite

Le contrat `p-lockable => macro-assignable` est respecte via:
- `track_runtime_get_param_rule(param)`,
- mapping domaine vers `SEQ_PLOCK_SET_*`,
- `seq_param_iface_param_is_supported(track,set,param)`.

Compat hors p-lock conservee:
- domaines `MIX` et `BUFFER` acceptes si `track_runtime_get_effective_param_status(track,param) == ALLOWED`.

Le passage 4->16 ne change pas ce contrat. Il ne faut pas ajouter de table MACRO separee d'exclusion/autorisation.

## Reponse aux questions

- Juste un changement de constante: non.
- Changement de format projet/persistence: oui, parce que `ProjectSaveV1` grossit et `project_sd_bank` valide version + taille.
- Refonte UI: pas une refonte large, mais une adaptation UI obligatoire pour adresser 4 macros * 16 slots avec seulement 16 halls.
- Cout runtime borne et acceptable: borne oui; probablement acceptable hors IRQ si les applies restent legers, mais worst-case x4 sur slots par pot et x4 macros = 128 operations release/apply potentielles dans un tour superloop.
- Risque audio hard-RT: faible/directement non observe, car le parcours est superloop/control-rate, pas audio IRQ.
- Zones a modifier si on le fait: Z6 (`project_v1`, `project_sd_bank`, version/migration), Z3 (`param_macro` dimension/cache/boucles), Z5 (`ui_macro_interaction`, LED/renderer/clipboard/page Macro CFG selon choix UI), docs Z3/Z5/Z6 et doc format projet.

## Prochaine passe minimale

1. Decider la grammaire UI minimale pour adresser 4 macros * 16 slots sans changer Hall/scenes.
2. Bumper `PROJECT_V1_FILE_VERSION` et ajouter migration projet 4->16 ou refuser explicitement les anciens projets avec note prototype.
3. Changer `PROJECT_V1_MACRO_SLOT_COUNT` a 16 et ajuster `param_macro`/`project_v1` sans nouvelle autorite.
4. Adapter `ui_macro_interaction` pour ne plus dependre de `hall / PROJECT_V1_MACRO_SLOT_COUNT` comme selection de macro.
5. Verifier `git diff --check`, puis build cible limite sur Z3/Z5/Z6, sans build complet initial.
