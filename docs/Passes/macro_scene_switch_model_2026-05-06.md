# Passe - MACRO Scene / Switch

## Ancien modele

- `Macro CFG` exposait `Hall Switch Mode`.
- `Slot`: les 16 halls etaient decoupes en 4 macros * 4 slots.
- `Bank`: les 16 halls selectionnaient une bank active.

## Nouveau modele

- `Macro CFG` expose `Mode`.
- Valeurs visibles: `Scene` / `Switch`.
- Les 16 halls representent maintenant 16 scenes.
- Le modele projet MACRO expose 16 scenes et 4 macro pots.
- Chaque scene contient maintenant 32 locks.
- Les 4 macro pots pointent vers une scene via `macro_scene[4]`; ils ne possedent pas les locks.

## Mode Scene

- Maintenir un hall selectionne la scene correspondante.
- Tourner un parametre UI standard pendant le maintien cree ou met a jour un lock dans cette scene au relachement.
- Tourner un macro pot pendant le maintien lie ce pot a cette scene et ne lance pas le morph du pot pendant ce geste.

## Mode Switch

- Maintenir un hall morph momentanement vers la scene correspondante selon la position hall `0..100`.
- Relacher le hall retire la source hall.
- Les 4 macro pots restent actifs en parallele et morphent vers leurs scenes assignees.

## Pots, conflits et limites

- Les 4 macro pots restent les sources physiques stables.
- `param_macro` gere maintenant 20 sources bornees: 4 pots + 16 halls.
- Chaque source parcourt au plus 32 locks de sa scene cible.
- En conflit sur un meme parametre, la derniere source touchee gagne; a release, une source precedente encore active reprend apres recomposition.
- Le clear/copy/paste macro slot n'a plus de cible slot unique en mode scene; il est laisse inactif dans cette passe.
- Suppression retenue apres correction multi-lock: `SHIFT + scene-hold + param edit` clear le lock cible.

## Contrat LED MACRO final

- En mode MACRO, les 16 Hall LEDs representent les 16 scenes MACRO.
- Hall LED `i` affiche toujours la couleur stable de la scene `i`.
- La couleur principale n'encode ni le type de parametre, ni le nombre de locks, ni l'ancien decoupage `4 pots * 4 slots`.
- Scene vide: couleur de scene faible.
- Scene non vide: couleur de scene normale.
- Scene maintenue/editee en `Mode=Scene`: couleur de scene forte.
- Scene morph active en `Mode=Switch`: couleur de scene avec intensite augmentee selon la pression hall disponible.
- Scene assignee a un macro pot: pas de surcharge Hall LED dans cette passe.

Source de couleur scene:
- `Drivers/Drv_app/Src/led_rgb.c` centralise la table `g_led_macro_scene_colors[16]`.
- `led_macro_scene_color(scene)` est le point local de resolution couleur reutilisable par une future LED de macro pot dans le meme renderer LED.

Limite palette:
- La couche LED ne dispose pas d'une palette nommee produit a 16 entrees; cette passe utilise donc 16 variantes RGB stables.

## Fichiers touches

- `Inc/Storage/project_v1.h`
- `Src/Storage/project_v1.c`
- `Inc/Param/param_macro.h`
- `Src/Param/param_macro.c`
- `Inc/UI/ui_macro_interaction.h`
- `Src/UI/ui_macro_interaction.c`
- `Src/UI/ui_hall_input_service.c`
- `Src/UI/ui_core.c`
- `Src/UI/pages/ui_page_template_macro.c`
- `Drivers/Drv_app/Src/led_rgb.c`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/architecture/z6_state_persistence_patterns_projects.md`

## Statut check initial

- `PROJECT_V1_MACRO_SLOT_COUNT` etait inchange avant la passe 32 locks.
- `PARAM_COUNT` inchange.
- Pas de changement DSP/audio.
- Historique: pas de bump `PROJECT_V1_FILE_VERSION` pendant la premiere passe Scene/Switch, avant augmentation a 32 locks.
- Build complet non lance.

## Etat final 32 locks par scene 2026-05-06

- Capacite projet MACRO: `PROJECT_V1_MACRO_SCENE_COUNT=16`, `PROJECT_V1_MACRO_SCENE_LOCK_COUNT=32`, `PROJECT_V1_MACRO_POT_COUNT=4`.
- Stockage canonique: `project_v1_macro_state_t.scenes[16].locks[32]`.
- Compat vocabulaire legacy conservee localement: les wrappers `project_v1_macro_get_slot/set_slot` et `param_macro_resolve_slot/apply_slot` restent presents mais les call-sites actifs passent par scene/lock.
- `PROJECT_V1_FILE_VERSION` passe a `16` car `ProjectSaveV1.macro` grossit.
- Choix persistence: refus propre des anciens projets prototype via validation stricte `version/payload_size`; pas de migration legacy ajoutee.
- `PatternSaveV1` inchange: les locks MACRO restent projet-only.
- Memoire projet macro: 16 * 32 locks * taille lock, soit environ 4096 octets pour les locks sur ABI courante, delta environ +2048 octets vs ancien layout 16 locks/scene.
- Cache runtime `param_macro`: 20 sources statiques * 32 resolutions max; delta estime environ +7.5 KiB vs 16 locks/source sur ABI courante.
- Worst-case control-rate borne: recomposition par release des sources puis reapply des sources actives par ordre de dernier toucher, avec au plus 32 resolutions/applies par source.
- Aucun parcours MACRO ajoute en IRQ audio; aucun malloc.
- Scene pleine: `project_v1_macro_assign_scene_lock()` met a jour un lock existant sinon cherche un lock libre; si les 32 locks sont occupes, l'ajout est refuse proprement.
- Bind pot pendant scene-hold: `project_v1_macro_set_macro_scene_no_sync()` met a jour le projet sans recomposition runtime immediate, donc sans morph audio pendant le geste.
- LEDs MACRO inchangees: elles representent toujours les 16 scenes par couleur/intensite, jamais les 32 locks.
- Clipboard macro slot reste inactif faute de cible scene/lock unique specifiee.

Fichiers touches pour 32 locks:
- `Inc/Storage/project_v1.h`
- `Src/Storage/project_v1.c`
- `Inc/Param/param_macro.h`
- `Src/Param/param_macro.c`
- `Src/UI/ui_macro_interaction.c`
- `Drivers/Drv_app/Src/led_rgb.c`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/architecture/z6_state_persistence_patterns_projects.md`
- `docs/Passes/macro_scene_switch_model_2026-05-06.md`

Statut check 32 locks:
- `PARAM_COUNT` inchange.
- Pas de changement DSP/audio.
- Pas de changement ROUT.
- Ancien mapping hall -> 4 pots * 4 slots non reactive.
- `git diff --check`: OK.
- Inspection statique includes/prototypes/signatures: OK.

## Statut audit 2026-05-06

- Validation statique du code reel: ancien mapping utilisateur hall -> macro/slot non retrouve dans les call-sites actifs.
- Aucun ancien concept utilisateur `Bank` ne reste actif sous `Switch`; les APIs `active_bank` residuelles ne sont plus appelees hors compat stockage.
- `Mode=Scene`: le maintien hall arme une scene; un edit encodeur est consomme par `ui_macro_interaction` et n'ecrit pas le param runtime live; un mouvement de macro pot pendant le maintien relie le pot a la scene via `project_v1_macro_set_macro_scene()` et ne lance pas le morph du pot.
- `Mode=Switch`: press/maintien hall pilote `param_macro_set_scene_source_amount(scene, amount)`; release appelle `param_macro_release_scene_source(scene)`, sans latch ni promotion de scene en base.
- Les 4 macro pots restent actifs hors maintien scene et pointent vers `macro_scene[4]`.
- L'arbitrage multi-source reste borne: 20 sources statiques, release globale des previews puis reapplication par ordre de `touch_seq`; si la source gagnante disparait, une source active precedente est reappliquee, sinon retour base.
- Scene vide/OFF: aucune resolution valide, donc apply/release no-op borne.
- Clipboard macro slot: chemin inactif proprement car `ui_macro_interaction_get_active_slot_target()` ne retourne plus de cible.
- LED MACRO: `led_rgb.c` lit uniquement la projection projet `project_v1_macro_scene_has_locks(scene)` et le mode Scene/Switch; il ne reconstruit pas l'ancien mapping pot/slot.
- Docs Z3/Z5/Z6 coherentes avec le code reel observe.
- `git diff --check`: OK.
- Inspection statique includes/prototypes/signatures: OK.

## Statut check LED 2026-05-06

- 16 Hall LEDs = 16 scenes, sans pagination de locks.
- Couleur stable par scene via `led_macro_scene_color(scene)`.
- Vide/non vide visibles par intensite seulement.
- Scene hold visible par intensite forte.
- Switch morph visible via `hall_engine_get_value(hall)` quand la pression est disponible.
- Ancien feedback slot/bank absent du renderer LED MACRO.
- Fichiers touches pour cette clarification: `Drivers/Drv_app/Src/led_rgb.c`, `Inc/UI/ui_macro_interaction.h`, `Src/UI/ui_macro_interaction.c`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/macro_scene_switch_model_2026-05-06.md`.
- `git diff --check`: OK.
- Inspection statique includes/prototypes/signatures: OK.

## Statut audit LED 2026-05-06

- `led_macro_scene_color(scene)` est borne: une scene hors plage retombe sur la couleur de scene 0.
- La table couleur scene n'est pas dupliquee ailleurs: seule `g_led_macro_scene_colors[]` dans `led_rgb.c` porte les 16 couleurs.
- Les 16 scenes ont une couleur stable; vide/non vide/hold/Switch ne changent que l'intensite.
- Les Hall LEDs MACRO ne lisent jamais le contenu detaille des locks: seul `project_v1_macro_scene_has_locks(scene)` sert au statut vide/non vide.
- Le hold scene passe par le getter Z5 public `ui_macro_interaction_get_held_scene()`, qui expose uniquement l'id de scene maintenue et ne donne pas acces a l'etat interne de capture.
- La pression Switch est lue uniquement dans la branche LED `PROJECT_V1_MACRO_HALL_SWITCH_SWITCH`; elle ne fuit pas en `Mode=Scene`.
- Ancien feedback `4 pots * 4 slots` et ancien feedback `Bank` absents du renderer LED MACRO actif.
- Les autres hall modes restent sur leurs chemins LED existants avant la branche `UI_HALL_MODE_MACRO`.

Cause trouvee: non.

## Corrections eventuelles

- Aucune correction code supplementaire dans cette validation LED.

## Limites restantes

- Geste de suppression/clear stabilise dans la correction multi-lock: `SHIFT + scene-hold + param edit`.
- Clipboard scene volontairement non branche tant qu'une cible scene/lock explicite n'est pas definie.
- Compat ancien projet: depuis la passe 32 locks, les anciens projets sont refuses par bump `PROJECT_V1_FILE_VERSION=16` et taille payload; aucune migration legacy n'est fournie.
- `ui_macro_interaction_reset()` relache les 16 sources hall une par une; c'est borne mais peut recomposer plusieurs fois.
- Palette LED: 16 variantes RGB stables mais non issues d'une palette produit nommee; distinction perceptive exacte dependra du hardware LED/diffusion.
- Aucun build cible ni build complet lance dans cette passe.

## Prochain chantier recommande

- Specifier un workflow stable de gestion des locks de scene: selection visible, clear/delete, copy/paste scene ou lock, feedback de scene pleine.
- Exposer plus tard une API couleur scene partagee si une LED physique de macro pot est cablee hors `led_rgb.c`.
- Decider si une vraie migration/bump projet est necessaire pour preserver proprement les anciens projets macro.

## Correction multi-lock Scene 2026-05-06

Cause trouvee:
- Le stockage et `param_macro` parcouraient bien les 32 locks, mais le feedback template restait branche sur un seul `macro_slot_param`.
- `ui_macro_interaction` ne commitait qu'un parametre pending par maintien de scene; changer de parametre pendant le meme hold remplacait le pending local avant ecriture projet.
- Aucun chemin clear `SHIFT + scene-hold + param edit` n'etait branche.

Correction appliquee:
- Ajout de queries projet par `scene + track + param` et clear de lock par sentinel vide.
- `ui_macro_interaction` commit le lock pending avant de passer a un autre parametre, puis update un lock existant ou ajoute dans le premier lock libre.
- `SHIFT + scene-hold + param edit` clear le lock cible si present; sinon no-op propre.
- Le renderer template teste chaque parametre visible via `ui_macro_interaction_param_is_locked()` et affiche la valeur cible via `ui_macro_interaction_get_param_lock_value()`.

Confirmation multi-lock UI/reel:
- Une scene peut conserver plusieurs locks visibles sur la meme page.
- Ajouter un second parametre ne supprime pas le premier.
- Mettre a jour un parametre deja locke modifie seulement le lock `track+param`.
- Apres clear SHIFT, l'encadre inverse disparait pour le parametre cible.
- Switch morph et macro pot restent sur le parcours runtime `PROJECT_V1_MACRO_SCENE_LOCK_COUNT=32`.

Fichiers touches:
- `Inc/Storage/project_v1.h`
- `Src/Storage/project_v1.c`
- `Inc/UI/ui_macro_interaction.h`
- `Src/UI/ui_macro_interaction.c`
- `Src/UI/ui_renderer_template.c`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/Passes/macro_scene_switch_model_2026-05-06.md`

Statut check:
- `PROJECT_V1_FILE_VERSION` inchange dans cette correction.
- Pas de changement DSP/audio/ROUT/PARAM_COUNT.
- LEDs MACRO inchangees.
- `git diff --check`: OK.
- Inspection statique includes/prototypes/signatures: OK.

## Validation anti-rustine multi-lock 2026-05-06

Statut:
- Validation anti-rustine OK.
- `project_v1` reste l'autorite canonique des locks de scene via `g_project_macro_state.scenes[16].locks[32]`.
- Surface helper claire cote Z6: `project_v1_macro_get_scene_lock_for_param()`, `project_v1_macro_assign_scene_lock()`, `project_v1_macro_clear_scene_lock()`, `project_v1_macro_scene_has_locks()`, `project_v1_macro_scene_lock_is_empty()`.
- `ui_macro_interaction` ne possede qu'un pending transitoire de capture pendant le maintien; il ne persiste aucun second modele de locks.
- Le pending est commite avant changement de parametre et ne remplace pas un autre lock: l'update reste limite au meme `track+param`, sinon ajout dans le premier lock libre.
- `SHIFT + scene-hold + param edit` appelle le clear projet par `scene+track+param`; un lock absent reste un no-op.
- Le renderer template ne scanne pas les 32 locks: il consomme la projection Z5 `ui_macro_interaction_param_is_locked()` / `ui_macro_interaction_get_param_lock_value()`.
- `param_macro` et les LEDs lisent la meme verite projet: parcours `project_v1_macro_get_scene_lock()` pour le runtime, `project_v1_macro_scene_has_locks()` pour vide/non-vide LED.
- Clear conserve la convention unique sentinel vide (`PROJECT_V1_MACRO_LOCK_TRACK_NONE`, `PROJECT_V1_MACRO_LOCK_PARAM_NONE`), sans compaction.
- Aucun retour du mapping utilisateur Bank/Slot observe dans les call-sites MACRO actifs.

Cause trouvee: non.

Corrections eventuelles:
- Aucune correction code supplementaire dans cette validation.

Statut check:
- `git diff --check`: OK.
- Inspection statique includes/prototypes/signatures: OK.
- Build complet non lance.
