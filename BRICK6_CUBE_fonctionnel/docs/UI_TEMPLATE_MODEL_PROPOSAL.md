# 1. Objectif

Objectif : ajouter un **deuxième modèle d’UI à gabarit fixe** sans supprimer ni casser le modèle actuel de **pages libres**.

La coexistence visée est la suivante :

- **pages libres existantes**
  - continuent à être représentées par `ui_page_t`
  - gardent leur `render()` direct via `drv_display_*`
  - gardent leur navigation actuelle via `ui_navigation_handle_event()` et leur logique locale éventuelle

- **pages à gabarit fixe nouvelles**
  - s’ouvrent comme une page normale depuis la navigation globale
  - encapsulent **une famille cohérente de 4 sous-pages**
  - utilisent un **layout visuel commun**
  - affichent **4 paramètres éditables** par sous-page
  - utilisent **4 boutons page** pour sélectionner directement la sous-page active
  - réemploient le même renderer/gabarit pour d’autres familles futures

Le but n’est pas un redesign global de l’UI, mais une **extension compatible** du système actuel.

---

# 2. Contraintes de l’existant

Ce qu’il faut préserver :

- `ui_page_manager` gère aujourd’hui **une seule page active**.
- `ui_navigation` repose sur un schéma simple **bouton/action -> page**.
- `ui_renderer_oled_draw()` déclenche le rendu de la page active.
- les pages libres actuelles (`g_ui_page_main`, `g_ui_page_param_test`, `g_ui_page_debug_hall`, `g_ui_page_calibration`, `g_ui_page_user_calibration`) dessinent directement via `drv_display_*`.
- `ui_param` repose sur une **banque active de 4 paramètres** (`ui_param_bank_t params[4]`).
- `ui_core_tick()` applique les deltas encodeurs via `ui_param_handle_encoder()` avant de traiter la navigation et les événements de page.

Ce qu’il faut éviter de casser :

- l’API de base `ui_page_t` et le cycle `enter / leave / handle_event / tick / render`.
- le registre de pages de `ui_page_manager`.
- la table de navigation existante dans `ui_navigation.c`.
- le fonctionnement des pages libres qui doivent continuer à marcher sans wrapper complexe.
- le couplage actuel entre `ui_param` et `param_registry` pour l’édition de 4 paramètres.

Points de couplage à prendre en compte :

- `ui_page_set()` remet la banque de paramètres à `0` avant d’appeler `enter()` de la page cible.
- `ui_navigation_handle_event()` est appelée avant `active_page->handle_event()` dans `ui_core_tick()`.
- les encodeurs sont traités uniformément ; le code actuel ne distingue pas structurellement `ENC_PAGE` des 3 encodeurs paramètres.
- les **4 boutons page physiques** existent côté hardware, mais sont aujourd’hui routés sur `BTN_UNUSED_1..4` dans `buttons_ids.h` / `buttons_hw.c`.
- `ui_renderer_oled` ne connaît aujourd’hui qu’un rendu très simple : clear puis `page->render()`.

---

# 3. Principe général proposé

Je recommande une architecture **à extension légère** fondée sur trois idées :

1. **Conserver les pages libres inchangées**.
2. **Introduire un type de page supplémentaire “template”** à côté du type libre.
3. **Modéliser une famille de 4 sous-pages comme une seule page active au sens de `ui_page_manager`**, avec un état local `active_subpage`.

Concrètement, je propose :

- **une extension légère de `ui_page_t`** pour lui ajouter un mode de rendu / de comportement et un pointeur de contexte ;
- **une couche intermédiaire de description** pour les pages à gabarit fixe ;
- **un renderer spécialisé** pour le gabarit fixe ;
- **un contrôleur générique de page template** chargé de :
  - gérer l’entrée dans la famille ;
  - gérer le changement de sous-page ;
  - pousser la bonne `ui_param_bank_t` dans `ui_param` ;
  - déléguer le rendu au renderer template.

Principe global :

- une **page libre** reste une `ui_page_t` “mode libre” ;
- une **page template** est aussi une `ui_page_t`, mais “mode template” ;
- `ui_page_manager` continue à ne connaître que des `ui_page_t` ;
- la famille de 4 sous-pages est contenue dans le **contexte** de cette page template ;
- la navigation globale continue d’ouvrir une page par ID ;
- la navigation locale du modèle template sélectionne ensuite la sous-page active à l’intérieur de cette même page.

C’est la solution la plus pragmatique parce qu’elle réutilise les points d’entrée existants (`ui_page_set`, `ui_navigation_handle_event`, `ui_param_set_bank`, `ui_renderer_oled_draw`) au lieu de les remplacer.

---

# 4. Architecture proposée

## 4.1 Extension légère de `ui_page_t`

Je recommande d’étendre `ui_page_t` dans `Inc/UI/ui_page.h` avec deux champs en fin de struct :

```c
typedef enum
{
    UI_PAGE_MODE_FREE = 0,
    UI_PAGE_MODE_TEMPLATE_FIXED,
} ui_page_mode_t;

typedef struct
{
    void (*enter)(void);
    void (*leave)(void);
    void (*handle_event)(const ui_event_t *);
    void (*tick)(void);
    void (*render)(void);

    ui_page_mode_t mode;
    const void *context;

} ui_page_t;
```

Rôle :

- `mode` permet de distinguer explicitement une page libre d’une page template.
- `context` permet d’associer une description/runtime à une page sans multiplier les variables globales cachées.

Pourquoi c’est compatible :

- les pages libres existantes continuent à fournir leurs callbacks actuels ;
- si les nouveaux champs sont ajoutés en fin de struct, les initialisations existantes restent simples à adapter ;
- `ui_page_manager` continue à stocker des `const ui_page_t *`.

## 4.2 Nouveau modèle de description template

Je recommande d’ajouter un nouveau header :

- `Inc/UI/ui_template_model.h`

Et son implémentation associée si nécessaire :

- `Src/UI/ui_template_model.c`

Structures proposées :

```c
typedef struct
{
    const char *title;
    ui_param_bank_t param_bank;
} ui_template_subpage_t;

typedef struct
{
    const char *family_title;
    const char *nav_labels[4];
    ui_template_subpage_t subpages[4];
    uint8_t default_subpage;
} ui_template_family_t;

typedef struct
{
    const ui_template_family_t *family;
    uint8_t active_subpage;
} ui_template_runtime_t;
```

Responsabilités :

- `ui_template_subpage_t`
  - décrit une sous-page du gabarit ;
  - porte ses 4 paramètres via `ui_param_bank_t` ;
  - porte un titre éventuel de sous-page.

- `ui_template_family_t`
  - décrit l’ensemble cohérent ouvert par un bouton param ;
  - porte le titre global du groupe ;
  - porte les 4 labels de navigation bas ;
  - contient exactement 4 sous-pages ;
  - définit une sous-page par défaut.

- `ui_template_runtime_t`
  - représente l’état runtime de cette famille ;
  - stocke la sous-page courante.

## 4.3 Contrôleur générique de page template

Je recommande un nouveau module :

- `Inc/UI/ui_template_page.h`
- `Src/UI/ui_template_page.c`

Rôle : représenter une famille template comme une **page UI normale**.

Fonctions proposées :

```c
void ui_template_page_enter(void);
void ui_template_page_leave(void);
void ui_template_page_handle_event(const ui_event_t *ev);
void ui_template_page_tick(void);
void ui_template_page_render(void);

void ui_template_page_select_subpage(ui_template_runtime_t *rt, uint8_t subpage);
const ui_template_subpage_t *ui_template_page_get_active_subpage(const ui_template_runtime_t *rt);
```

Responsabilités :

- `enter()`
  - lit le `context` de la page active ;
  - initialise `active_subpage = family->default_subpage` ;
  - appelle `ui_param_set_bank()` avec la banque de la sous-page courante.

- `handle_event()`
  - intercepte les 4 boutons page ;
  - appelle `ui_template_page_select_subpage()` ;
  - remet à jour `ui_param_set_bank()` à chaque changement de sous-page.

- `tick()`
  - peut rester vide au départ.

- `render()`
  - délègue au renderer commun de gabarit.

## 4.4 Renderer spécialisé pour le gabarit fixe

Je recommande un nouveau module :

- `Inc/UI/ui_renderer_template_fixed.h`
- `Src/UI/ui_renderer_template_fixed.c`

Fonction proposée :

```c
void ui_renderer_template_fixed_draw(const ui_template_runtime_t *rt);
```

Rôle :

- dessiner un layout fixe réutilisable ;
- injecter dynamiquement :
  - le titre de famille ;
  - le titre de sous-page ;
  - les 4 labels bas ;
  - les 4 paramètres de la sous-page active.

## 4.5 Nouveau type de page concret pour une famille template

Pour chaque famille future, je recommande un fichier de définition dédié, par exemple :

- `Inc/UI/pages/ui_page_template_dx7.h`
- `Src/UI/pages/ui_page_template_dx7.c`

Exemple de contenu :

```c
static ui_template_runtime_t g_ui_template_dx7_runtime;

static const ui_template_family_t g_ui_template_dx7_family = {
    .family_title = "DX7",
    .nav_labels = { "PLAY", "MOTION", "CTRL", "COLOR" },
    .subpages = {
        { .title = "PLAY",   .param_bank = { .params = { ... } } },
        { .title = "MOTION", .param_bank = { .params = { ... } } },
        { .title = "CTRL",   .param_bank = { .params = { ... } } },
        { .title = "COLOR",  .param_bank = { .params = { ... } } },
    },
    .default_subpage = 0U,
};

const ui_page_t g_ui_page_template_dx7 = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .mode = UI_PAGE_MODE_TEMPLATE_FIXED,
    .context = &g_ui_template_dx7_runtime,
};
```

Initialisation recommandée :

- dans ce fichier, affecter au boot `g_ui_template_dx7_runtime.family = &g_ui_template_dx7_family`.
- soit via une petite fonction `ui_page_template_dx7_init()` appelée depuis `ui_core_init()` ;
- soit via une initialisation statique si le compilateur/l’outillage le permet proprement.

## 4.6 Modifications minimales des modules existants

### `ui_page_manager`

Modification recommandée :

- ajouter éventuellement un helper :

```c
const void *ui_page_get_context(void);
ui_page_mode_t ui_page_get_mode(void);
```

But :
- éviter que les renderers / contrôleurs template aillent relire des globals internes.

### `ui_renderer_oled`

Deux options possibles.

#### Option recommandée : branchement explicite par mode

`ui_renderer_oled_draw()` devient :

- `UI_PAGE_MODE_FREE` -> appel `page->render()` comme aujourd’hui
- `UI_PAGE_MODE_TEMPLATE_FIXED` -> appel `ui_renderer_template_fixed_draw(page->context)`

Avantage :
- chemin explicite ;
- extensible à d’autres gabarits plus tard.

#### Option encore plus légère : garder `page->render()` pour tout

- les pages template utilisent simplement `ui_template_page_render()` ;
- `ui_renderer_oled` n’a pas besoin de connaître les modes.

Je ne recommande cette variante que si l’objectif est strictement le plus petit diff possible. Pour préparer d’autres gabarits, l’option explicite par `mode` est plus propre.

### `ui_navigation`

Modification recommandée :

- conserver la table globale actuelle ;
- ajouter de nouvelles règles `BTN_PARAM_X -> UI_PAGE_TEMPLATE_*` pour ouvrir une famille template ;
- ne pas utiliser les 4 boutons page dans cette table globale.

### `buttons_ids.h`

Je recommande **des alias sémantiques** plutôt qu’un renommage destructif :

```c
#define BTN_PAGE_1 BTN_UNUSED_1
#define BTN_PAGE_2 BTN_UNUSED_2
#define BTN_PAGE_3 BTN_UNUSED_3
#define BTN_PAGE_4 BTN_UNUSED_4
```

Avantage :
- on réemploie les 4 boutons page déjà câblés dans `buttons_hw.c` ;
- on évite une modification large des enums existantes.

---

# 5. Navigation proposée

## 5.1 Représentation d’un ensemble template

Un ensemble template est représenté comme :

- **une page globale** côté `ui_page_manager` ;
- **4 sous-pages internes** côté `ui_template_runtime_t`.

Donc :
- la navigation globale continue de manipuler des **IDs de page** ;
- la navigation interne template manipule un **index de sous-page** `0..3`.

## 5.2 Stockage de la sous-page courante

Je recommande de stocker la sous-page courante dans :

```c
typedef struct
{
    const ui_template_family_t *family;
    uint8_t active_subpage;
} ui_template_runtime_t;
```

Ce runtime est référencé via `ui_page_t.context`.

Comportement recommandé :

- à l’entrée dans la famille : `active_subpage = default_subpage` ;
- à chaque bouton page : mise à jour directe de `active_subpage` ;
- à chaque changement de sous-page : appel `ui_param_set_bank()` sur la banque de cette sous-page.

## 5.3 Ouverture d’un ensemble via un bouton param

Le comportement demandé s’intègre naturellement dans la navigation globale existante :

- un bouton param reste un **déclencheur de `ui_page_set()`** ;
- la cible n’est plus forcément une page libre ;
- la cible peut être une **page template famille**.

Exemple :

- `BTN_PARAM_6 -> UI_PAGE_TEMPLATE_DX7`

La famille s’ouvre donc comme une page normale.

## 5.4 Sélection directe des 4 sous-pages avec 4 boutons page

Je recommande d’utiliser :

- `BTN_PAGE_1`
- `BTN_PAGE_2`
- `BTN_PAGE_3`
- `BTN_PAGE_4`

Mapping proposé :

- `BTN_PAGE_1` -> sous-page `0`
- `BTN_PAGE_2` -> sous-page `1`
- `BTN_PAGE_3` -> sous-page `2`
- `BTN_PAGE_4` -> sous-page `3`

Cette logique doit vivre dans `ui_template_page_handle_event()` et non dans `ui_navigation.c`.

## 5.5 Cohabitation navigation globale actuelle / navigation locale template

Règle simple proposée :

- **navigation globale** = boutons qui changent la page active au sens `ui_page_manager`
- **navigation locale template** = boutons page qui changent la sous-page active dans la famille courante

Donc :

- `ui_navigation.c` continue à traiter les boutons param ouvrant des pages/familles ;
- `ui_template_page_handle_event()` traite les boutons page.

## 5.6 Éviter les conflits entre navigation globale et navigation locale

Je recommande les règles suivantes :

1. **Ne pas mettre `BTN_PAGE_1..4` dans `g_ui_nav_rules[]`**.
2. Réserver `BTN_PAGE_1..4` au seul modèle template.
3. Garder les boutons param pour ouvrir les familles/pages depuis la navigation globale.
4. Si un jour une page libre veut utiliser les boutons page, le faire dans son `handle_event()` local, sans passer par `ui_navigation`.

Ainsi :
- pas de conflit structurel entre les deux niveaux ;
- pas besoin de refondre l’ordre actuel `ui_navigation_handle_event()` puis `active_page->handle_event()`.

## 5.7 Cas d’usage recommandé

Séquence utilisateur :

1. appui sur un bouton param réservé à une famille template ;
2. `ui_navigation_handle_event()` ouvre `UI_PAGE_TEMPLATE_*` ;
3. `ui_template_page_enter()` sélectionne la sous-page par défaut et sa banque de 4 paramètres ;
4. l’utilisateur appuie sur `BTN_PAGE_1..4` pour changer de sous-page ;
5. la sous-page active change immédiatement ;
6. les encodeurs continuent à éditer les 4 paramètres de la banque active.

---

# 6. Rendu proposé

## 6.1 Principe général

Je recommande deux chemins de rendu :

- **pages libres**
  - continuent à dessiner elles-mêmes ;
  - continuent à appeler directement `drv_display_draw_text()`, `drv_display_draw_rect()`, etc.

- **pages template**
  - ne dessinent pas elles-mêmes le layout complet ;
  - fournissent surtout une description (`ui_template_family_t` + `ui_template_runtime_t`) ;
  - la mise en forme est déléguée à `ui_renderer_template_fixed_draw()`.

## 6.2 Comment le renderer décide quel chemin prendre

Recommandation principale :

- `ui_renderer_oled_draw()` lit `page->mode`.
- `switch (page->mode)` :
  - `UI_PAGE_MODE_FREE` -> rendu libre historique
  - `UI_PAGE_MODE_TEMPLATE_FIXED` -> rendu template partagé

C’est l’option la plus claire si plusieurs gabarits apparaissent plus tard.

## 6.3 Données fournies au renderer template

Le renderer template reçoit au minimum :

- `family_title`
- `active_subpage`
- `nav_labels[4]`
- `subpages[active_subpage].title`
- `subpages[active_subpage].param_bank`

À partir de cette banque, il peut résoudre pour chaque slot :

- `param_id_t`
- nom du paramètre via `param_registry[id].name`
- type / display_type / unité
- valeur actuelle via `param_get(id)`

## 6.4 Zones fixes du layout

Le gabarit fixe doit définir des zones stables, par exemple :

- **header**
  - titre famille
  - titre de sous-page

- **zone centrale**
  - 4 lignes paramètres
  - une ligne par slot encodeur

- **barre basse**
  - 4 labels de navigation
  - indication visuelle de la sous-page active

Le document ne présume pas ici de coordonnées définitives, mais le principe est que ces zones appartiennent au renderer template, pas aux familles concrètes.

## 6.5 Zones remplies dynamiquement

Le contenu injecté dynamiquement par chaque famille est :

- titre global de famille ;
- labels de navigation bas ;
- titre de la sous-page courante ;
- les 4 `param_id_t` de la sous-page ;
- donc indirectement leurs noms et leurs valeurs.

## 6.6 Injection des 4 paramètres

Je recommande que `ui_renderer_template_fixed_draw()` lise directement la `ui_param_bank_t` de la sous-page active.

Pour chaque slot `0..3` :

- lire `param_id_t id = subpage->param_bank.params[i]` ;
- récupérer `param_registry[id]` ;
- récupérer `param_get(id)` ;
- formatter via une fonction dédiée partagée, par exemple :

```c
void ui_param_format_value(param_id_t id, char *out, uint32_t out_len);
```

Je recommande de **sortir** la logique de formatage actuellement dupliquée dans `ui_page_param_test_format_value()` vers un module partagé, par exemple :

- `Inc/UI/ui_param_format.h`
- `Src/UI/ui_param_format.c`

Ce n’est pas obligatoire pour le prototype, mais c’est le meilleur petit refactor utile.

## 6.7 Injection des 4 labels de sous-pages

Je recommande que les labels bas viennent de :

```c
const char *nav_labels[4];
```

portés au niveau `ui_template_family_t`.

Pourquoi au niveau famille :

- ils sont stables pour tout l’ensemble ;
- ils décrivent les 4 destinations possibles ;
- ils ne dépendent pas du slot encodeur ;
- ils permettent au renderer de dessiner un footer cohérent et réutilisable.

## 6.8 Extensibilité vers d’autres gabarits

Je recommande de penser dès maintenant en deux niveaux :

- `ui_page_mode_t`
- renderer dédié par mode / gabarit

Exemple d’évolution future :

```c
typedef enum
{
    UI_PAGE_MODE_FREE = 0,
    UI_PAGE_MODE_TEMPLATE_FIXED,
    UI_PAGE_MODE_TEMPLATE_GRID,
    UI_PAGE_MODE_TEMPLATE_METER,
} ui_page_mode_t;
```

Ainsi, le premier gabarit fixe est introduit sans bloquer des gabarits supplémentaires plus tard.

---

# 7. Impact minimal sur l’existant

## 7.1 Fichiers qui n’ont probablement pas besoin d’être touchés

À priori inchangés ou quasi inchangés :

- `Src/UI/pages/ui_page_main.c`
- `Src/UI/pages/ui_page_debug_hall.c`
- `Src/UI/pages/ui_page_calibration.c`
- `Src/UI/pages/ui_page_param_test.c` (hors éventuelle réutilisation du formateur de valeur)
- `Src/Param/param_store.c`
- la plupart des callbacks `apply_*` de `param_registry.c`
- `drv_display.c`

## 7.2 Fichiers qui demandent de petites modifications

Modifications limitées recommandées :

- `Inc/UI/ui_page.h`
  - ajout de `ui_page_mode_t` et `context`

- `Src/UI/ui_page_manager.c` / `Inc/UI/ui_page_manager.h`
  - helpers `ui_page_get_mode()` / `ui_page_get_context()` si utiles

- `Src/UI/ui_renderer_oled.c`
  - switch simple sur le mode de page

- `Src/UI/ui_navigation.c`
  - ajout de nouvelles règles ouvrant les familles template

- `Drivers/Drv_app/Inc/buttons_ids.h`
  - alias `BTN_PAGE_1..4`

## 7.3 Fichiers nouveaux préférables

Je recommande de créer des modules séparés plutôt que de grossir `ui_core` ou `ui_navigation` :

- `Inc/UI/ui_template_model.h`
- `Src/UI/ui_template_model.c` (optionnel)
- `Inc/UI/ui_template_page.h`
- `Src/UI/ui_template_page.c`
- `Inc/UI/ui_renderer_template_fixed.h`
- `Src/UI/ui_renderer_template_fixed.c`
- éventuellement `Inc/UI/ui_param_format.h`
- éventuellement `Src/UI/ui_param_format.c`
- pour chaque famille réelle : `Inc/UI/pages/ui_page_template_<name>.h` + `Src/UI/pages/ui_page_template_<name>.c`

## 7.4 Ce qui peut être prototypé sans refondre toute l’UI

Prototype minimal possible :

1. ajouter le support `UI_PAGE_MODE_TEMPLATE_FIXED` ;
2. créer une seule famille template ;
3. lui donner 4 sous-pages et 4 banques de paramètres ;
4. la brancher sur un bouton param libre ;
5. gérer `BTN_PAGE_1..4` localement ;
6. garder toutes les pages libres existantes intactes.

C’est suffisant pour valider l’architecture sans toucher au cœur métier.

---

# 8. Proposition de plan d’implémentation

## Étape 1 : structures minimales

- ajouter `ui_page_mode_t` et `context` à `ui_page_t`
- ajouter les helpers de lecture de mode/contexte dans `ui_page_manager` si nécessaire
- ajouter les alias `BTN_PAGE_1..4` dans `buttons_ids.h`

## Étape 2 : modèle template

- créer `ui_template_model.h`
- définir `ui_template_subpage_t`, `ui_template_family_t`, `ui_template_runtime_t`
- décider si `default_subpage` vaut toujours `0` ou si c’est configurable

## Étape 3 : contrôleur générique de page template

- créer `ui_template_page.c`
- implémenter :
  - `enter()`
  - `handle_event()`
  - `select_subpage()`
  - réinjection de `ui_param_set_bank()`

## Étape 4 : renderer template fixe

- créer `ui_renderer_template_fixed.c`
- dessiner un premier layout simple : header / 4 lignes paramètres / footer 4 labels
- réutiliser `param_registry` et `param_get()` pour les contenus

## Étape 5 : extraction optionnelle du formatage de valeur

- extraire la logique de `ui_page_param_test_format_value()` vers `ui_param_format.c`
- faire utiliser ce helper par `ui_page_param_test` et par le renderer template

## Étape 6 : première famille prototype

- créer une première famille template réelle, par exemple basée sur le cas déjà proche de `ui_page_param_test`
- définir les 4 sous-pages et leurs 4 banques
- enregistrer cette famille dans `ui_core_init()`
- lui affecter un bouton param dans `ui_navigation.c`

## Étape 7 : validation navigation

- vérifier l’ouverture de la famille depuis un bouton param
- vérifier la sélection directe des 4 sous-pages via `BTN_PAGE_1..4`
- vérifier que `ui_param` suit bien la sous-page active

## Étape 8 : généralisation légère

- ajouter une deuxième famille template pour confirmer la réutilisabilité du renderer
- ajuster uniquement le modèle/descripteur si un besoin manque

Ordre recommandé :
- d’abord la structure et le prototype ;
- ensuite seulement la généralisation.

---

# 9. Risques / points à surveiller

## 9.1 Conflits avec la navigation actuelle

Risque :
- si les 4 boutons page sont ajoutés par erreur dans `ui_navigation.c`, ils entreront en conflit avec la navigation locale template.

Mesure :
- réserver `BTN_PAGE_1..4` à la navigation locale du modèle template.

## 9.2 Conflits avec `ui_param`

Risque :
- `ui_param` ne sait gérer qu’une banque globale de 4 slots.

Conséquence :
- une seule page/famille éditable à la fois, ce qui est cohérent avec le modèle actuel ;
- il faut impérativement réappeler `ui_param_set_bank()` à chaque changement de sous-page.

## 9.3 Endroits où le code impose déjà 4 slots fixes

Les contraintes “4” existent déjà dans plusieurs endroits :

- `ui_param_bank_t params[4]`
- `ui_param_handle_encoder()` avec `encoder < 4`
- `ENC_COUNT == 4`
- le comportement actuel des encodeurs

Pour le modèle template demandé, ce n’est pas un problème :
- le nouveau gabarit veut précisément 4 paramètres éditables.

Mais il faut bien voir que cette architecture proposée n’essaie pas de généraliser au-delà de 4 paramètres ; elle s’aligne volontairement sur l’existant.

## 9.4 Confusion autour du nom des boutons/encodeurs

Points de confusion actuels :

- `BTN_UNUSED_1..4` correspondent en réalité à des boutons page physiques.
- `ENC_PAGE` n’est pas aujourd’hui un encodeur de navigation au sens structurel.

Mesures recommandées :

- ajouter des alias `BTN_PAGE_1..4`.
- documenter explicitement que, dans cette architecture, la navigation de sous-pages repose sur les boutons page, pas sur `ENC_PAGE`.

## 9.5 Risque de sur-ingénierie

Risque :
- vouloir introduire trop tôt un système générique de widgets ou de layout engine.

Mesure :
- rester sur un **renderer template unique** + un **descripteur simple** ;
- ne pas transformer les pages libres en pages déclaratives.

## 9.6 Point à confirmer

À confirmer avant implémentation :

- faut-il **toujours** réinitialiser `active_subpage` à `default_subpage` quand on rouvre une famille, ou faut-il mémoriser la dernière sous-page visitée ?

Pour un premier prototype, je recommande la réinitialisation systématique à `default_subpage` : comportement simple, déterministe, compatible avec la logique actuelle des pages.

---

# 10. Recommandation finale

Architecture recommandée :

- **garder `ui_page_manager` tel quel sur le principe** : une seule page active ;
- **ajouter un mode de page `UI_PAGE_MODE_TEMPLATE_FIXED`** à côté du mode libre ;
- **représenter une famille template comme une page normale contenant 4 sous-pages internes** ;
- **utiliser un renderer partagé `ui_renderer_template_fixed`** pour le layout commun ;
- **réutiliser `ui_param` tel quel** en changeant simplement de banque lors du changement de sous-page ;
- **réserver les 4 boutons page à la navigation locale template** ;
- **conserver la navigation globale actuelle pour ouvrir les familles/pages**.

C’est une architecture :

- **compatible avec l’existant** ;
- **incrémentale** ;
- **légère** ;
- **suffisante pour prototyper rapidement** un premier ensemble à gabarit fixe ;
- **extensible** si d’autres gabarits apparaissent plus tard.

Conclusion pragmatique :

- il n’y a pas besoin d’un gros refactor destructif pour introduire le modèle d’UI à gabarit ;
- une petite extension de `ui_page_t`, quelques modules nouveaux dédiés, et une discipline claire entre navigation globale et navigation locale suffisent ;
- c’est la stratégie que je recommande pour ajouter le nouveau modèle tout en conservant entièrement les pages libres actuelles.
