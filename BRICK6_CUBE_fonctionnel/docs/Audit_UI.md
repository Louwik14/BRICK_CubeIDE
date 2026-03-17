# UI Architecture – Brick6 (Current Implementation)

## 1. Vue d’ensemble

L’interface utilisateur est organisée autour d’une **UI à pages pilotée par un tasklet** synchronisé avec le moteur audio.

Architecture globale :

```
main loop
   │
   ├─ engine_tasklet_poll()
   │
   └─ ui_tasklet_poll()
          │
          └─ ui_core_tick()
                 │
                 ├─ lecture encoders
                 ├─ génération événements boutons
                 ├─ navigation pages
                 ├─ logique page active
                 └─ rendu écran OLED
```

Le rendu graphique est réalisé via :

```
drv_display
```

qui implémente un **framebuffer software + flush SPI SSD1309**.

---

# 2. Pipeline UI complet

## 2.1 Déclenchement dans la main loop

Dans `main.c` :

```c
if(engine_tick_count != last_tick)
{
    last_tick = engine_tick_count;
    ui_tasklet_poll();
}
```

L’UI est donc **cadencée par l’audio engine**.

Cadence :

```
audio block = 32 frames
48kHz / 32 = 1500 Hz
```

Donc `ui_tasklet_poll()` peut être appelé jusqu’à **1500 fois par seconde**.

---

# 3. ui_tasklet

Fichier :

```
ui_tasklet.c
```

Responsabilité :

```
initialisation lazy + appel ui_core
```

Logique :

```
ui_tasklet_poll()
    if first call
        drv_display_init()
        ui_core_init()

    ui_core_tick()
```

---

# 4. ui_core

Fichier :

```
ui_core.c
```

C’est le **coeur du système UI**.

Responsabilités :

```
- gestion des pages
- gestion des paramètres via encodeurs
- gestion des événements boutons
- appel du renderer OLED
```

Pipeline d’un tick :

```
ui_core_tick()

1) Lecture encodeurs
   encoder_consume_delta()

2) Application paramètres
   ui_param_handle_encoder()

3) Génération événements boutons
   ui_event_from_inputs()

4) Consommation file d'événements
   ui_event_pop()

       ├─ ui_navigation_handle_event()
       └─ active_page->handle_event()

5) Tick logique page
   active_page->tick()

6) Rendu écran
   ui_renderer_oled_draw()
```

---

# 5. Système d’événements

Module :

```
ui_event.c
```

Implémente une **queue circulaire lock-free**.

Structure :

```
UI_EVENT_Q_LEN = 32
```

Types d'événements :

```
UI_EVENT_BUTTON_PRESS
UI_EVENT_BUTTON_RELEASE
```

Pipeline :

```
buttons driver
    ↓
ui_event_from_inputs()
    ↓
ui_event_push()
    ↓
queue
    ↓
ui_event_pop()
```

---

# 6. Navigation entre pages

Module :

```
ui_navigation.c
```

Navigation **data-driven**.

Table :

```
static const ui_nav_rule_t g_ui_nav_rules[]
```

Exemple :

```
BTN_PARAM_1 -> PAGE_PARAM_TEST
BTN_PARAM_2 -> PAGE_MAIN
BTN_PARAM_3 -> PAGE_HALL_DEBUG
BTN_PARAM_4 -> PAGE_CALIBRATION
```

Logique :

```
event bouton
      ↓
ui_navigation_handle_event()
      ↓
ui_page_set(target_page)
```

---

# 7. Page Manager

Module :

```
ui_page_manager.c
```

Responsabilités :

```
- registry statique des pages
- gestion page active
- enter/leave hooks
```

Structure :

```
g_ui_pages[16]
g_ui_page_count
g_ui_current_page_id
```

Changement de page :

```
ui_page_set()

current_page->leave()
current_page_id = new
next_page->enter()
```

Récupération page active :

```
ui_page_get()
```

---

# 8. Structure d'une page

Chaque page implémente :

```
ui_page_t
```

Structure :

```
enter()
leave()
handle_event()
tick()
render()
```

Cycle de vie :

```
enter()

loop:
    handle_event()
    tick()
    render()

leave()
```

---

# 9. Renderer OLED

Module :

```
ui_renderer_oled.c
```

Pipeline :

```
ui_renderer_oled_draw()

page = ui_page_get()

drv_display_clear()

page->render()

drv_display_update()
```

Protection reentrance :

```
static drawing flag
```

---

# 10. Driver écran

Module :

```
drv_display.c
```

Driver **SSD1309 SPI**.

Architecture :

```
Framebuffer software
+ dirty pages
+ flush SPI
```

Framebuffer :

```
buffer[OLED_WIDTH * OLED_HEIGHT / 8]
```

Pour écran :

```
128 x 64
```

Format :

```
vertical pages
8 pixels / byte
```

---

## Dirty tracking

Variables :

```
display_dirty
dirty_pages
```

Chaque pixel modifié :

```
dirty_pages |= page_bit
```

Update écran :

```
drv_display_update()

for each page
    if dirty
        SPI send page
```

---

# 11. Primitive graphique

Fournies par :

```
drv_display
```

Primitives disponibles :

```
draw_pixel
draw_rect
fill_rect
clear_rect

draw_char
draw_text
draw_number
```

Fonts supportées :

```
FONT_5X7
FONT_4X6
```

---

# 12. Système de paramètres UI

Module :

```
ui_param.c
```

Concept :

```
encoder -> param_id -> param_registry
```

Structure :

```
ui_param_bank
```

Exemple :

```
{
PARAM_GRAN_DENSITY
PARAM_GRAN_PITCH
PARAM_GRAN_MIX
PARAM_GRAN_FREEZE
}
```

Flow :

```
encoder delta
    ↓
ui_param_handle_encoder()
    ↓
param_get()
param_set()
```

Clamp automatique :

```
min/max
step
```

---

# 13. Pages existantes

## Main Page

```
ui_page_main.c
```

Fonction :

```
page principale UI
```

Affiche :

```
BRICK6 MAIN
BTN1: PARAM TEST
BTN2: MAIN PAGE
BTN3: HALL DEBUG
```

---

## Calibration Page

```
ui_page_calibration.c
```

Fonction :

```
calibration capteurs hall
```

UI :

```
grid 8x2
16 cellules
```

Affichage :

```
rectangle
compteur hits
OK quand >=3
```

Puis :

```
save calibration
retour main
```

---

# 14. Timing réel de l'UI

Important.

Cadence max :

```
1500 Hz
```

Mais **le rendu OLED est appelé à chaque tick** :

```
ui_renderer_oled_draw()
```

Donc :

```
clear framebuffer
render page
SPI flush
```

jusqu'à **1500 fois par seconde**.

---

# 15. Chemin complet d'un rendu écran

```
main loop
   ↓
engine_tasklet_tick
   ↓
ui_tasklet_poll
   ↓
ui_core_tick
   ↓
ui_renderer_oled_draw
   ↓
drv_display_clear
   ↓
page->render
   ↓
drv_display_update
   ↓
SPI transfer
```

---

# 16. Points sensibles actuels

Architecture solide mais **plusieurs points peuvent provoquer le glitch OLED**.

### 1. fréquence de rendu extrême

OLED appelé potentiellement :

```
1500 fps
```

Un SSD1309 SPI ne peut pas suivre.

---

### 2. HAL_SPI_Transmit bloquant

```
HAL_SPI_Transmit()
```

bloque CPU.

Si rendu trop fréquent :

```
SPI saturation
bus contention
glitch écran
```

---

### 3. rendu heavy

Calibration page :

```
16 rectangles
+ 16 textes
+ snprintf
```

C'est beaucoup plus lourd que la page main.

---

### 4. clear + redraw complet

Chaque frame :

```
drv_display_clear()
render
update
```

pas de diff.

---

### 5. SPI non DMA

Actuellement :

```
HAL_SPI_Transmit
```

donc **blocking transfer**.

---

# 17. Résumé architecture

```
UI SYSTEM

main
 └─ ui_tasklet
      └─ ui_core
           ├─ encoders
           ├─ events
           ├─ navigation
           ├─ page manager
           └─ renderer

renderer
 └─ drv_display

pages
 ├─ main
 ├─ param_test
 ├─ hall_debug
 └─ calibration
```

---

# 18. Conclusion

Architecture actuelle :

Points forts :

```
✔ architecture propre
✔ pages modulaires
✔ navigation data-driven
✔ param system générique
✔ renderer séparé
```

Points faibles :

```
✘ rendu OLED trop fréquent
✘ SPI blocking
✘ clear full frame
✘ rendu lourd pages complexes
```


