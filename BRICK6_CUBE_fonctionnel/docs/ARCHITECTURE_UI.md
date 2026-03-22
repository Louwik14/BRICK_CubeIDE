# ARCHITECTURE UI — cartographie structurelle de l’existant

Ce document complète `Audit_UI.md`.
Il décrit uniquement l’architecture logique actuelle de l’UI : pages, navigation, paramètres, rendu, widgets implicites, hypothèses de layout et couplages structurels.

Il ne redécrit pas la pipeline runtime/tasklets/SPI sauf quand un point est nécessaire pour comprendre un couplage.

---

# 1. Vue d’ensemble

## 1.1 Modules et responsabilités

L’UI actuelle est structurée autour de quelques briques simples :

- `ui_core` orchestre les entrées UI et délègue à la navigation puis à la page active.
- `ui_page_manager` maintient un registre statique des pages et l’identifiant de page active.
- `ui_navigation` applique une table de règles bouton → page.
- `ui_page_*` portent la logique spécifique de chaque vue.
- `ui_param` gère une notion de **banque active de 4 paramètres** éditables par encodeur.
- `param_registry` décrit les paramètres (nom, type, min/max, step, format d’affichage, callback d’application).
- `param_store` stocke les valeurs runtime et sert aussi de backend de lecture/écriture pour l’UI.
- `ui_renderer_oled` déclenche le `render()` de la page active.
- `drv_display` fournit les primitives de dessin bas niveau sur OLED.

## 1.2 Séparation actuelle logique / état / rendu / navigation / paramètres

La séparation existe, mais reste minimale.

- **Logique de vue** : chaque page expose un `ui_page_t` avec `enter`, `leave`, `handle_event`, `tick`, `render`.
- **État global UI** : principalement l’ID de page active dans `ui_page_manager`, plus quelques états statiques locaux dans certaines pages (`g_dx7_page_index`, drapeaux/calendriers de calibration).
- **Navigation globale** : table statique dans `ui_navigation.c`.
- **Navigation locale** : codée directement dans `handle_event()` des pages quand elle existe.
- **Édition de paramètres** : centralisée dans `ui_param.c` via une banque active de 4 `param_id_t`.
- **Rendu** : découpage en deux niveaux :
  - `ui_renderer_oled` choisit la page active et appelle `page->render()`.
  - chaque page dessine directement via `drv_display_*`.

## 1.3 Forme générale du modèle actuel

Le modèle implicite actuel est :

1. une **page active unique** ;
2. une **banque de 4 paramètres max** attachée à cette page ;
3. une **navigation globale par boutons fixes** ;
4. un **rendu direct immédiat** dans le framebuffer OLED ;
5. **pas de hiérarchie de widgets métier**, seulement des primitives de dessin appelées depuis les pages.

C’est donc une UI de type "pages fixes + callbacks + layout codé en dur".

---

# 2. Cartographie des fichiers

## 2.1 Noyau UI

### `Inc/UI/ui_page.h`

Rôle exact :
- définit l’interface commune de toutes les pages.

Principales structures :
- `ui_page_t` : table de callbacks avec `enter`, `leave`, `handle_event`, `tick`, `render`.

Dépendances :
- dépend de `ui_event.h` pour le type `ui_event_t`.

### `Inc/UI/ui_page_manager.h` / `Src/UI/ui_page_manager.c`

Rôle exact :
- registre statique des pages ;
- stockage de la page courante ;
- transition `leave()` → changement d’ID → `enter()`.

Principales fonctions / enums :
- enum des IDs : `UI_PAGE_MAIN`, `UI_PAGE_PARAM_TEST`, `UI_PAGE_HALL_KEY_DEBUG`, `UI_PAGE_CALIBRATION`, `UI_PAGE_USER_CALIBRATION`, `UI_PAGE_COUNT`.
- `ui_page_manager_init()`.
- `ui_page_manager_register()`.
- `ui_page_set()`.
- `ui_page_get()`.
- `ui_page_get_id()`.

Dépendances :
- dépend de `ui_page.h`.
- l’implémentation dépend aussi de `ui_param.h` pour remettre la banque de paramètres à `0` avant chaque changement de page.

Remarques structurelles :
- registre fixe `g_ui_pages[16]`.
- l’ordre d’enregistrement définit implicitement la correspondance entre index de registre et IDs de navigation.

### `Inc/UI/ui_core.h` / `Src/UI/ui_core.c`

Rôle exact :
- point d’entrée logique de l’UI.
- initialise le système de pages.
- traite les deltas encodeurs, les événements boutons, la navigation, puis le `tick()` de la page active.

Principales fonctions :
- `ui_core_init()`.
- `ui_core_tick()`.

Dépendances :
- `encoders.h`.
- pages concrètes (`ui_page_main.h`, `ui_page_param_test.h`, `ui_page_debug_hall.h`, `ui_page_calibration.h`).
- `ui_event.h`, `ui_navigation.h`, `ui_page_manager.h`, `ui_param.h`.
- `App/Hall/hall_calibration.h` pour choisir la page initiale.

### `Inc/UI/ui_tasklet.h` / `Src/UI/ui_tasklet.c`

Rôle exact :
- initialisation lazy de l’UI ;
- appelle `drv_display_init()` puis `ui_core_init()` au premier poll ;
- appelle ensuite `ui_core_tick()`.

Principales fonctions :
- `ui_tasklet_poll()`.
- `ui_tasklet_is_initialized()`.

Dépendances :
- `drv_display.h`.
- `ui_core.h`.

## 2.2 Événements et navigation

### `Inc/UI/ui_event.h` / `Src/UI/ui_event.c`

Rôle exact :
- définit les types d’événements UI ;
- construit une queue circulaire d’événements issus des boutons.

Principales structures / fonctions :
- `ui_event_type_t` : `UI_EVENT_NONE`, `UI_EVENT_ENCODER`, `UI_EVENT_BUTTON_PRESS`, `UI_EVENT_BUTTON_RELEASE`.
- `ui_event_t` avec `type`, `id`, `value`.
- `ui_event_from_inputs()`.
- `ui_event_pop()`.

Dépendances :
- `buttons.h`.

Remarque importante :
- dans l’état actuel, `ui_event_from_inputs()` ne pousse **que des événements boutons**. `UI_EVENT_ENCODER` est défini mais non produit.

### `Inc/UI/ui_navigation.h` / `Src/UI/ui_navigation.c`

Rôle exact :
- navigation globale pilotée par table de règles.

Principales structures / fonctions :
- `UI_NAV_ANY_PAGE`.
- `ui_nav_rule_t` : `button`, `required_page`, `target_page`.
- `ui_navigation_handle_event()`.

Table actuelle :
- `BTN_PARAM_1` → `UI_PAGE_PARAM_TEST`
- `BTN_PARAM_2` → `UI_PAGE_MAIN`
- `BTN_PARAM_3` → `UI_PAGE_HALL_KEY_DEBUG`
- `BTN_PARAM_4` → `UI_PAGE_CALIBRATION`
- `BTN_PARAM_5` → `UI_PAGE_USER_CALIBRATION`

Dépendances :
- `buttons.h`, `ui_event.h`, `ui_page_manager.h`.

## 2.3 Paramètres

### `Inc/UI/ui_param.h` / `Src/UI/ui_param.c`

Rôle exact :
- abstraction minimale pour exposer 4 paramètres à l’UI.

Principales structures / fonctions :
- `ui_param_bank_t` avec `params[4]`.
- `ui_param_set_bank()`.
- `ui_param_handle_encoder()`.

Dépendances :
- `param_store.h` dans le header.
- `param_registry.h` dans l’implémentation.

Remarque structurelle :
- le module ne stocke qu’**une banque active globale**, sans notion de focus, de sélection ou de curseur par widget.

### `Inc/Param/param_store.h` / `Src/Param/param_store.c`

Rôle exact :
- backend runtime des valeurs de paramètres.
- double stockage `staging` / `active`.

Principales structures / fonctions :
- enum des `param_id_t` (`PARAM_*` jusqu’à `PARAM_COUNT`).
- `param_store_init()`.
- `param_store_set_staging()`.
- `param_store_set_active()`.
- `param_store_commit_if_block_advanced()`.
- `param_store_get_active()`.
- compteurs de commit.

Dépendances :
- `param_registry.h`.
- `audio_float.h` pour `g_audio_block_counter`.
- `stm32h7xx_hal.h`.

### `Inc/Param/param_registry.h` / `Src/Param/param_registry.c`

Rôle exact :
- description statique de tous les paramètres UI/audio connus.

Principales structures / fonctions :
- `param_display_type_t`.
- `param_type_t`.
- `param_desc_t` : `id`, `name`, `type`, `min`, `max`, `step`, `default_value`, `display_type`, `unit`, `labels`, `apply`.
- `param_registry[PARAM_COUNT]`.
- `param_registry_init()`.
- `param_get()`.
- `param_set()`.
- `param_reset()`.

Dépendances :
- nombreuses dépendances audio/moteur dans les callbacks `apply_*` (`mixer`, `microdexed_synth`, `juno_synth`, `audio_float`, `fx_granular`, `fx_daisy_comp`, `fx_pool`, etc.).

Remarque structurelle :
- le registry mélange :
  - métadonnées d’affichage UI ;
  - domaine de validité ;
  - callback d’application moteur.

### `Inc/Param/control_router.h` / `Src/Param/control_router.c`

Rôle exact :
- couche de routage alternative vers les paramètres de contrôle.

Principales fonctions :
- `control_router_set_param()`.

Dépendances :
- `param_registry.h`, `param_store.h`.

Remarque :
- cette couche n’est pas utilisée par les pages UI actuelles, qui appellent `param_set()` directement via `ui_param_handle_encoder()`.

### `Inc/Param/control_events.h` / `Src/Param/control_events.c`

Rôle exact :
- file lock-free d’événements de contrôle entre tasklet et audio.

Pertinence pour l’UI actuelle :
- indirecte seulement ; ce n’est pas le mécanisme employé par les pages UI actuelles pour modifier les paramètres affichés/édités.

## 2.4 Renderer, affichage, "widgets"

### `Inc/UI/ui_renderer_oled.h` / `Src/UI/ui_renderer_oled.c`

Rôle exact :
- renderer global OLED.
- choisit la page active, efface le framebuffer et appelle `page->render()`.

Principales fonctions :
- `ui_renderer_oled_draw()`.
- `ui_renderer_oled_service_poll()`.
- `ui_renderer_oled_is_rendering()`.

Dépendances :
- `main.h`.
- `drv_display.h`.
- `ui_page_manager.h`.

### `Drivers/Drv_app/Inc/drv_display.h` / `Drivers/Drv_app/Src/drv_display.c`

Rôle exact :
- API bas niveau de dessin OLED ;
- encapsule buffer, U8g2 et primitives de rendu.

Principales fonctions :
- buffer / lifecycle : `drv_display_init()`, `drv_display_clear()`, `drv_display_update()`, `drv_display_get_buffer()`.
- fonts : `drv_display_set_font()`.
- texte : `drv_display_draw_char()`, `drv_display_draw_text()`, `drv_display_draw_number()`.
- primitives : `drv_display_draw_pixel()`, `drv_display_draw_rect()`, `drv_display_draw_line()`, `drv_display_fill_rect()`, `drv_display_clear_rect()`.

Dépendances :
- HAL / SPI / GPIO / SDRAM.
- `UI/font.h`.
- `u8g2.h`.

### `Inc/UI/font.h`

Rôle exact :
- expose les objets `FONT_5X7` et `FONT_4X6`.

Remarque :
- le support de police existe, mais les pages UI actuelles n’appellent pas `drv_display_set_font()`.

## 2.5 Pages concrètes

### `Inc/UI/pages/ui_page_main.h` / `Src/UI/pages/ui_page_main.c`

Rôle exact :
- page principale informative.

Principales fonctions :
- `ui_page_main_enter()` remet la banque de paramètres à `0`.
- `ui_page_main_render()` affiche titre, métriques CPU et aide de navigation.

Dépendances :
- `cpu_load.h`, `drv_display.h`, `ui_param.h`.

### `Inc/UI/pages/ui_page_param_test.h` / `Src/UI/pages/ui_page_param_test.c`

Rôle exact :
- page de test / édition de paramètres DX7 par banques de 4 paramètres.

Principaux éléments :
- `g_dx7_param_banks[]` : 4 banques de 4 `param_id_t`.
- `g_dx7_page_names[]` : `PLAY`, `MOTION`, `CTRL`, `COLOR`.
- `g_dx7_page_index`.
- `ui_page_param_test_select_page()`.
- `ui_page_param_test_format_value()`.
- `ui_page_param_test_handle_event()`.
- `ui_page_param_test_render()`.

Dépendances :
- `buttons.h`, `drv_display.h`, `param_registry.h`, `ui_param.h`.

### `Inc/UI/pages/ui_page_debug_hall.h` / `Src/UI/pages/ui_page_debug_hall.c`

Rôle exact :
- page de debug hall/velocity en lecture seule.

Principales fonctions :
- `ui_page_debug_hall_render()` appelle `hall_engine_get_velocity_debug()` puis affiche plusieurs lignes de télémétrie.

Dépendances :
- `drv_display.h`, `App/Hall/hall_engine.h`.

### `Inc/UI/pages/ui_page_calibration.h` / `Src/UI/pages/ui_page_calibration.c`

Rôle exact :
- regroupe deux pages de calibration :
  - `g_ui_page_calibration`
  - `g_ui_page_user_calibration`

Principales fonctions / états :
- calibration capteurs :
  - `ui_page_calibration_enter()` lance `hall_calibration_start()`.
  - `ui_page_calibration_tick()` traite, sauvegarde, puis redirige vers `UI_PAGE_MAIN`.
  - `ui_page_calibration_render()` dessine une grille 8 colonnes et l’état de progression des touches.
- calibration utilisateur :
  - `ui_page_user_calibration_enter()` lance `hall_user_calibration_start()`.
  - `ui_page_user_calibration_tick()` traite, sauvegarde si succès, sinon relance la calibration.
  - `ui_page_user_calibration_render()` affiche l’étape (`FORT`, `MID`, `SOFT`, `WAIT`) et les consignes.

Dépendances :
- `stm32h7xx_hal.h`.
- `App/Hall/hall_calibration.h`.
- `drv_display.h`.
- `ui_page_manager.h`.
- `UI/font.h`.

Remarque :
- `UI/font.h` est inclus, mais aucune police n’est changée dans ce fichier.

### `Inc/UI/pages/ui_page_hall_key_debug.h`

Rôle exact :
- expose `extern const ui_page_t g_ui_page_hall_key_debug;`.

État constaté :
- aucun fichier source correspondant n’a été trouvé dans `Src/UI/pages/`.
- la page réellement enregistrée dans `ui_core.c` est `g_ui_page_debug_hall`.

---

# 3. État UI actuel

## 3.1 Où est stockée la page active

La page active est stockée dans `ui_page_manager.c` via :
- `static uint8_t g_ui_current_page_id`.
- le registre `static const ui_page_t *g_ui_pages[UI_PAGE_MANAGER_MAX_PAGES]`.

La fonction `ui_page_get_id()` renvoie l’ID courant.
La fonction `ui_page_get()` renvoie le pointeur sur la page courante.

## 3.2 Comment l’état UI est représenté

L’état UI est distribué, pas centralisé dans un gros objet unique.

État global commun :
- page active : `g_ui_current_page_id`.
- registre de pages : `g_ui_pages[]`, `g_ui_page_count`.
- banque de paramètres active : `g_ui_param.bank` + `g_ui_param.valid` dans `ui_param.c`.
- queue d’événements boutons : `g_ui_evt_q`, `g_ui_evt_w`, `g_ui_evt_r` dans `ui_event.c`.

État local par page :
- `ui_page_param_test.c` : `g_dx7_page_index`.
- `ui_page_calibration.c` : `g_save_done`, `g_cal_done_tick`, `g_user_save_done`, `g_user_message_tick`.
- `ui_page_main.c` et `ui_page_debug_hall.c` n’ont pas de state métier local persistant notable.

## 3.3 Comment une vue/page devient active

Une page devient active par `ui_page_set(page_id)`.

Séquence réelle :
1. validation de l’ID ;
2. récupération page courante / page cible ;
3. `ui_param_set_bank(0)` avant la transition ;
4. `leave()` de la page courante si défini ;
5. mise à jour de `g_ui_current_page_id` ;
6. `enter()` de la nouvelle page si défini.

## 3.4 Activation initiale

Dans `ui_core_init()` :
- les pages sont enregistrées dans un ordre fixe ;
- la page initiale dépend de `hall_calibration_load()` :
  - si le chargement réussit (`!= 0U`) → `UI_PAGE_MAIN` ;
  - sinon → `UI_PAGE_CALIBRATION`.

Cela signifie que l’entrée dans certaines vues dépend déjà d’un état métier externe à l’UI.

---

# 4. Navigation

## 4.1 Comment les boutons génèrent la navigation

Le chemin actuel est :

1. `buttons_update()` met à jour les états débouncés.
2. `ui_event_from_inputs()` transforme les fronts en `UI_EVENT_BUTTON_PRESS` / `UI_EVENT_BUTTON_RELEASE`.
3. `ui_core_tick()` dépile chaque événement.
4. pour chaque événement, `ui_navigation_handle_event()` est appelé avant `active_page->handle_event()`.

La navigation globale ne consomme que les `UI_EVENT_BUTTON_PRESS`.

## 4.2 Comment on change de page

`ui_navigation_handle_event()` parcourt `g_ui_nav_rules[]`.

Une règle s’applique si :
- `event->id == rule->button` ;
- et `rule->required_page == UI_NAV_ANY_PAGE` ou bien `rule->required_page == current_page`.

Si la page cible est différente de la page courante, `ui_page_set(rule->target_page)` est appelé.

## 4.3 Comment une action ouvre une vue

Il existe actuellement trois mécanismes d’entrée dans une vue :

### A. Navigation globale par boutons

Via `g_ui_nav_rules[]`.

### B. Décision d’entrée au boot

Dans `ui_core_init()` via `hall_calibration_load()`.

### C. Redirection interne depuis une page

Certaines pages appellent elles-mêmes `ui_page_set()` depuis leur `tick()` :
- `ui_page_calibration_tick()` retourne vers `UI_PAGE_MAIN` quand la calibration est terminée et affichée assez longtemps.
- `ui_page_user_calibration_tick()` retourne vers `UI_PAGE_MAIN` si la calibration utilisateur réussit ; sinon relance la séquence.

## 4.4 Navigation globale vs navigation locale

### Navigation globale

- gérée par `ui_navigation.c` ;
- basée sur une table statique ;
- indépendante du contenu précis des pages ;
- actuellement attachée à `BTN_PARAM_1..5`.

### Navigation locale

- codée dans la page elle-même via `handle_event()` ou `tick()`.
- exemple clair : `ui_page_param_test_handle_event()` intercepte `BTN_PARAM_1` pour changer de sous-page DX7 interne.

## 4.5 Interaction entre navigation globale et locale

L’ordre d’exécution dans `ui_core_tick()` est important :

1. `ui_navigation_handle_event(&ev)` ;
2. récupération de la page active **après** la navigation globale ;
3. appel de `active_page->handle_event(&ev)`.

Conséquence observée sur `ui_page_param_test` :
- `BTN_PARAM_1` a une règle globale vers `UI_PAGE_PARAM_TEST`.
- si l’utilisateur est déjà sur `UI_PAGE_PARAM_TEST`, la navigation globale ne change pas de page.
- le même événement est ensuite vu par `ui_page_param_test_handle_event()`, qui l’utilise comme navigation locale pour faire tourner `g_dx7_page_index`.

Cette coexistence fonctionne, mais repose sur le fait que la cible globale soit identique à la page déjà active.

## 4.6 Boutons non utilisés par la navigation actuelle

La table de navigation n’emploie que `BTN_PARAM_1..5`.

Aucune règle n’utilise actuellement :
- `BTN_PARAM_6..8`
- `BTN_PLAY`, `BTN_REC`, `BTN_SHIFT`, `BTN_COPY`, `BTN_PASTE`, `BTN_SETTINGS`
- `BTN_TRANSPOSE_UP`, `BTN_TRANSPOSE_DOWN`

Le mapping physique existe pourtant dans `buttons_hw.c`.

---

# 5. Paramètres

## 5.1 Comment ils sont définis

Les paramètres sont définis en deux couches :

### Identité
- enum `PARAM_*` dans `param_store.h`.
- `param_id_t` est un `uint16_t`.

### Métadonnées et comportement
- `param_registry[PARAM_COUNT]` dans `param_registry.c`.
- chaque entrée décrit :
  - nom (`name`) ;
  - type (`type`) ;
  - bornes (`min`, `max`) ;
  - pas (`step`) ;
  - valeur par défaut (`default_value`) ;
  - format d’affichage (`display_type`) ;
  - unité (`unit`) ;
  - labels optionnels (`labels`) ;
  - callback d’application (`apply`).

## 5.2 Comment ils sont identifiés

L’UI n’utilise pas de nom symbolique local aux pages.
Elle manipule directement des `param_id_t` issus du registry global.

Exemple : `g_dx7_param_banks[]` contient directement des IDs comme `PARAM_DX7_ALGORITHM`, `PARAM_DX7_FEEDBACK`, etc.

## 5.3 Comment l’encodeur agit dessus

Le chemin actuel est :

1. `encoders_update()` accumule des deltas signés par encodeur.
2. `ui_core_tick()` boucle sur `encoder = 0..ENC_COUNT-1`.
3. pour chaque encodeur, `encoder_consume_delta(encoder)` est lu.
4. `ui_param_handle_encoder(encoder, delta)` applique le delta à la banque active.

Dans `ui_param_handle_encoder()` :
- il faut une banque valide ;
- `encoder` doit être `< 4` ;
- le paramètre ciblé est `g_ui_param.bank.params[encoder]` ;
- la nouvelle valeur est `param_get(param) + delta * desc->step` ;
- la valeur est clampée par `ui_param_clamp()` ;
- puis `param_set(param, value)` est appelé.

## 5.4 Particularité importante des encodeurs

Les encodeurs sont nommés :
- `ENC_PAGE = 0`
- `ENC_PARAM_A`
- `ENC_PARAM_B`
- `ENC_PARAM_C`

Mais `ui_core_tick()` les traite tous uniformément via `ui_param_handle_encoder()`.

Donc, dans l’état actuel :
- l’encodeur nommé `ENC_PAGE` n’a pas de logique spéciale de navigation ;
- il modifie simplement le **slot 0** de la banque de paramètres active.

C’est un couplage important entre nom matériel et usage UI réel.

## 5.5 Comment les valeurs sont clampées / écrites / lues

### Clamp
- premier clamp dans `ui_param_handle_encoder()` via `ui_param_clamp()`.
- second clamp de sécurité dans `param_set()` via `clamp_value(value, desc->min, desc->max)`.

### Écriture
- `ui_param_handle_encoder()` appelle `param_set()`.
- `param_set()` appelle `param_store_set_active(id, clamped)` puis `desc->apply(clamped)` si le callback existe.

### Lecture
- `param_get(id)` renvoie `param_store_get_active(id)`.
- les pages lisent donc les valeurs **actives**.

## 5.6 Liens avec store / registry / moteur

Le chainage actuel est très direct :

- UI → `ui_param_handle_encoder()`
- `ui_param_handle_encoder()` → `param_get()` / `param_set()`
- `param_set()` → `param_store_set_active()`
- `param_set()` → callback `apply()` du registry
- callback `apply()` → modules moteur/audio (`mixer`, synthés, FX, etc.)

L’UI est donc branchée directement sur le registry central et, indirectement, sur le moteur audio via les callbacks `apply`.

## 5.7 Lien avec staging / commit

`param_store` possède bien un double stockage `staging` / `active` et une fonction `param_store_commit_if_block_advanced()`.

Mais l’UI actuelle qui passe par `ui_param_handle_encoder()` n’utilise pas le chemin staging/commit différé :
- elle appelle `param_set()` ;
- `param_set()` écrit immédiatement la valeur active et applique immédiatement le callback moteur.

Le mécanisme `control_router_set_param()` existe, met à jour `staging` puis tente un commit, mais n’est pas le chemin pris par les pages UI actuelles.

## 5.8 Pages qui exposent des paramètres éditables

Seule `ui_page_param_test` configure explicitement une banque active de paramètres éditables.

Les autres pages :
- `ui_page_main` désactive la banque via `ui_param_set_bank(0)` ;
- `ui_page_debug_hall` n’expose pas d’édition ;
- `ui_page_calibration` et `ui_page_user_calibration` n’utilisent pas `ui_param`.

---

# 6. Renderer et layout

## 6.1 Comment une page est rendue

Le rendu structurel est :

1. `ui_renderer_oled_draw()` récupère `ui_page_get()`.
2. `drv_display_clear()` efface tout l’écran.
3. `page->render()` est appelé si présent.
4. la page dessine directement via `drv_display_*`.

Il n’y a pas d’objet intermédiaire de scène, de widget tree ou de template d’écran.

## 6.2 Ce qui est commun à toutes les pages

Commun à toutes les pages :
- contrat `ui_page_t`.
- même renderer global `ui_renderer_oled`.
- même écran OLED 128x64 via `drv_display`.
- effacement plein écran avant chaque rendu.
- même police par défaut tant qu’aucune page n’en change une.

## 6.3 Ce qui est spécifique à une page

Chaque page décide seule :
- quels textes afficher ;
- quelles coordonnées utiliser ;
- quelles primitives appeler ;
- si elle lit des métriques CPU, des états hall, des paramètres, etc.

Exemples :
- `ui_page_main_render()` utilise uniquement du texte sur des Y fixes.
- `ui_page_param_test_render()` rend 4 lignes correspondant à 4 paramètres.
- `ui_page_calibration_render()` dessine une grille 8×N avec rectangles et remplissage vertical.
- `ui_page_user_calibration_render()` affiche des consignes textuelles selon l’étape.

## 6.4 Où le code suppose un layout unique

Le code actuel suppose implicitement un layout OLED fixe dans plusieurs endroits :

- constantes écran dans `drv_display.h` : `OLED_WIDTH 128`, `OLED_HEIGHT 64`.
- coordonnées codées en dur dans toutes les pages.
- espacements verticaux implicites :
  - `16` px dans `ui_page_param_test_render()` ;
  - `10`, `14`, `24`, `34`, `48`, `58` px dans `ui_page_main_render()` ;
  - grille basée sur `CAL_CELL_W 16`, `CAL_CELL_H 22`, `CAL_GRID_COLS 8` dans `ui_page_calibration.c`.
- `drv_display_draw_text(x, y, ...)` suppose que `y` représente le haut du texte, converti en baseline via `drv_display_baseline()`.

Il n’existe pas de couche de layout abstraite séparant contenu et placement.

## 6.5 Où les widgets sont choisis

Il n’existe pas de système explicite de widgets UI réutilisables dans le code étudié.

Les "widgets" actuels sont implicites et de bas niveau :
- ligne de texte ;
- rectangle ;
- boîte remplie ;
- ligne ;
- caractère / nombre.

Le choix de ces éléments est fait directement dans le `render()` de chaque page en appelant `drv_display_draw_text()`, `drv_display_draw_rect()`, `drv_display_fill_rect()`, etc.

Autrement dit :
- **pas de `widget` métier** (`ParamRow`, `MenuList`, `Grid`, `Header`, etc.) ;
- seulement des primitives de dessin combinées manuellement par page.

## 6.6 Format d’affichage des valeurs

La logique de formatage est elle aussi locale à la page dans `ui_page_param_test_format_value()`.

Cette fonction s’appuie sur `param_desc_t` pour choisir entre :
- booléen ;
- enum ;
- pourcentage ;
- dB ;
- temps en ms ;
- ratio ;
- float générique.

Donc le registry porte une partie de la sémantique d’affichage, mais l’assemblage final des lignes reste spécifique à la page.

---

# 7. Couplages actuels

## 7.1 Ce qui est générique

Relativement générique aujourd’hui :
- l’interface `ui_page_t`.
- le registre statique de pages de `ui_page_manager`.
- la table de règles de `ui_navigation`.
- le descripteur `param_desc_t` dans `param_registry`.
- les primitives de dessin de `drv_display`.

## 7.2 Ce qui est hardcodé

Hardcodé explicitement :
- ordre d’enregistrement des pages dans `ui_core_init()`.
- table de navigation globale dans `ui_navigation.c`.
- page de démarrage selon `hall_calibration_load()` dans `ui_core.c`.
- banques DX7 de `ui_page_param_test.c`.
- sous-pages `PLAY/MOTION/CTRL/COLOR` de `ui_page_param_test.c`.
- toutes les coordonnées de rendu dans les `render()`.
- grille de calibration et messages de calibration.

## 7.3 Ce qui mélange contenu de page et forme d’affichage

Le mélange est fort dans les pages.

Exemples :
- `ui_page_param_test_render()` mélange :
  - choix des paramètres ;
  - structure de sous-page ;
  - formatage des valeurs ;
  - concaténation des lignes ;
  - coordonnées d’écran.
- `ui_page_calibration_render()` mélange :
  - progression métier `hall_calibration_*` ;
  - représentation graphique en grille 8 colonnes ;
  - géométrie précise des cellules.
- `ui_page_user_calibration_render()` mélange :
  - étape métier ;
  - textes d’instruction ;
  - placement fixe.

## 7.4 Ce qui couple fortement l’UI au moteur / domaine métier

Couplages principaux :

- `ui_core_init()` dépend du résultat de `hall_calibration_load()` pour déterminer la première page.
- pages de calibration dépendent directement des APIs `hall_calibration_*` et `hall_user_calibration_*`.
- page debug dépend directement de `hall_engine_get_velocity_debug()`.
- `ui_param` dépend du `param_registry` global.
- `param_registry` dépend directement de modules moteur/audio via les callbacks `apply_*`.

L’UI n’est donc pas un consommateur neutre d’un modèle de données indépendant ; elle manipule directement le domaine.

## 7.5 Ce qui empêcherait facilement d’avoir plusieurs modèles d’UI

Freins principaux pour supporter facilement plusieurs modèles d’UI (gabarit fixe vs libre) :

### A. Absence de séparation contenu / layout
- les pages rendent directement aux coordonnées finales ;
- il n’existe pas de représentation intermédiaire d’une page ;
- pas de modèle de sections, zones, slots ou widgets réutilisables.

### B. Banque de paramètres figée à 4 slots
- `ui_param_bank_t` impose exactement `params[4]` ;
- le mapping encodeur → slot est direct ;
- aucune notion de page avec nombre variable d’items/focus.

### C. Entrées fortement liées au layout implicite
- les encodeurs sont traités comme 4 éditeurs de slots fixes ;
- le premier encodeur (`ENC_PAGE`) n’a pas de rôle structurel distinct malgré son nom.

### D. Pages autoportées et monolithiques
- chaque page porte sa logique d’entrée, son état, son contenu et son dessin.
- pas de séparation entre "page definition" et "renderer for that page model".

### E. Navigation globale fixée par boutons statiques
- pas de graphe de navigation ou de description de transitions attachée aux vues.
- navigation locale uniquement codée dans la page.

## 7.6 Points de couplage fins mais importants

- `ui_page_set()` remet systématiquement la banque de paramètres à `0` avant `leave()` / `enter()`. Une page doit donc réinstaller explicitement sa banque dans `enter()` si elle veut de l’édition.
- `ui_core_tick()` applique toujours les deltas encodeurs **avant** de générer/dépiler les événements boutons. L’ordre d’interaction entre édition et navigation est donc implicite.
- `ui_navigation_handle_event()` s’exécute avant `active_page->handle_event()`, ce qui conditionne la coexistence entre navigation globale et locale.
- certaines entrées du registry ont `apply == NULL`, ce qui signifie que le paramètre peut exister pour l’UI/stockage sans effet moteur immédiat visible dans ce fichier.

---

# 8. Zones à confirmer

Points explicitement ambigus ou incomplets à confirmer avant refactor :

1. **Nommage page debug hall**
   - l’enum expose `UI_PAGE_HALL_KEY_DEBUG`.
   - la page concrète enregistrée est `g_ui_page_debug_hall`.
   - un header `ui_page_hall_key_debug.h` existe sans source correspondant trouvé.

2. **Statut réel de `ENC_PAGE`**
   - son nom suggère un encodeur de navigation.
   - dans le code actuel, il édite le slot paramètre 0 comme les autres.
   - à confirmer si c’est volontaire, temporaire ou régression.

3. **Rôle voulu de `control_router` / `control_events` pour l’UI**
   - ces briques existent, mais le chemin UI actuel passe directement par `param_set()`.
   - à confirmer si le routage différé devait devenir le chemin officiel UI.

4. **Paramètres registry sans callback `apply`**
   - par exemple `PARAM_BUS_COMP_DRYWET` et `PARAM_BUS_COMP_HPF_HZ` ont `apply == NULL` dans le registry actuel.
   - à confirmer si c’est volontaire, incomplet ou en attente d’intégration moteur.

5. **Usage prévu des boutons non mappés en navigation**
   - `BTN_PARAM_6..8` et plusieurs boutons système existent mais ne participent pas à la navigation actuelle.
   - à confirmer si ces entrées sont réservées à d’autres workflows UI.

6. **Portée du support multi-polices**
   - `FONT_4X6` / `FONT_5X7` existent.
   - aucune page actuelle ne change explicitement de police.
   - à confirmer si le layout cible suppose réellement une seule police.

7. **Statut du header `ui_page_hall_key_debug.h`**
   - à confirmer s’il s’agit d’un reliquat, d’une page supprimée, ou d’un renommage incomplet.

---

# 9. Résumé utile avant refactor

## 9.1 Fichiers les plus centraux

À comprendre en priorité :

- `Src/UI/ui_core.c`
- `Src/UI/ui_page_manager.c`
- `Src/UI/ui_navigation.c`
- `Src/UI/ui_param.c`
- `Src/Param/param_registry.c`
- `Src/UI/pages/ui_page_param_test.c`
- `Src/UI/pages/ui_page_calibration.c`
- `Drivers/Drv_app/Src/drv_display.c`

## 9.2 Points à comprendre impérativement avant de changer l’architecture

1. **Le contrat réel d’une page**
   - `ui_page_t` est l’unité structurelle de base.

2. **Le lien entre ordre d’enregistrement et IDs de page**
   - toute évolution de la navigation dépend de cette correspondance implicite.

3. **Le modèle d’édition actuel = 4 slots fixes**
   - c’est le cœur du couplage entre paramètres, encodeurs et structure d’écran.

4. **Le couplage du registry**
   - le registry n’est pas seulement descriptif ; il applique aussi les changements au moteur.

5. **L’absence de widget/layout abstrait**
   - les pages dessinent directement leur forme finale.

6. **L’ordre exact de traitement des entrées**
   - encodeurs d’abord ;
   - puis événements boutons ;
   - navigation globale avant logique locale de page.

7. **Les vues non purement UI**
   - calibration et debug hall sont très liées au domaine métier et pilotent elles-mêmes les transitions de page.

## 9.3 Lecture synthétique de l’existant

Aujourd’hui, l’UI est surtout un assemblage de :
- pages à callbacks ;
- navigation bouton → page ;
- banque globale de 4 paramètres ;
- rendu direct à coordonnées fixes ;
- registry paramètre central couplé au moteur.

Cette base est simple et lisible, mais elle décrit davantage **une UI concrète unique** qu’un socle capable d’héberger plusieurs modèles d’UI sans couche d’abstraction supplémentaire.
