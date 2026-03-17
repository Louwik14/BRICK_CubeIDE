# Audit UI – Architecture actuelle (Brick6)

## 1. Vue d’ensemble

L’interface utilisateur est organisée autour d’une **UI à pages pilotée par un tasklet**, synchronisé avec le **moteur audio**.

L’architecture sépare clairement :

* lecture des entrées (boutons / encodeurs)
* logique UI
* rendu graphique
* transfert SPI vers l’écran

Le rendu et le transfert SPI sont **découplés**, ce qui évite les glitches OLED.

---

# 2. Boucle principale

Pipeline réel dans `main.c` :

```
main loop
   │
   ├─ engine_tasklet_poll()
   │
   ├─ brick6_app_process()
   │
   ├─ MX_USB_HOST_Process()
   ├─ usb_host_tasklet_poll_bounded()
   ├─ midi_host_poll_bounded()
   │
   ├─ ui_tasklet_poll()
   │     └─ ui_core_tick()
   │
   ├─ ui_renderer_oled_service_poll()
   │     └─ ui_renderer_oled_draw()
   │
   └─ display_flush_service_poll()
         └─ drv_display_update()
```

---

# 3. Cadence temporelle

La cadence UI est **dérivée de l’audio**.

Configuration :

```
sample rate : 48 kHz
audio block : 32 frames
```

Cadence :

```
48 000 / 32 = 1500 Hz
```

Donc :

```
engine_tick = 1500 Hz
ui_core_tick ≈ 1500 Hz max
```

Mais **le rendu écran est limité**.

---

# 4. Limitation de la fréquence de rendu

Le rendu UI est limité à **≈30 FPS**.

Dans :

```
ui_renderer_oled.c
```

Constante :

```
UI_RENDER_PERIOD_MS = 33
```

Pipeline :

```
ui_renderer_oled_service_poll()

if (now - last_render >= 33 ms)
    ui_renderer_oled_draw()
```

Donc :

```
render ≈ 30 Hz
```

Ce qui évite de reconstruire le framebuffer 1500 fois par seconde.

---

# 5. Limitation du flush écran

Le transfert SPI vers l’écran est **séparé du rendu**.

Module :

```
display_flush_service.c
```

Fréquence :

```
DISPLAY_FLUSH_PERIOD_MS = 33
```

Pipeline :

```
display_flush_service_poll()

if 33 ms elapsed
    if renderer not active
        drv_display_update()
```

Donc :

```
SPI flush ≈ 30 Hz
```

Protection supplémentaire :

```
if (ui_renderer_oled_is_rendering())
    return
```

Empêche flush pendant rendu.

---

# 6. engine_tasklet

Module :

```
engine_tasklet.c
```

Rôle :

Créer une **base de temps stable dérivée de l’audio**.

---

## 6.1 Accumulation en IRQ

Appelé depuis l’IRQ audio :

```
engine_tasklet_notify_frames(frames)
```

Code :

```
engine_frames_accum += frames
```

Ultra léger.

---

## 6.2 Consommation dans la main loop

```
engine_tasklet_poll()
```

Traitement :

```
while accumulated frames ≥ 32
      engine_tick()
```

---

## 6.3 engine_tick()

Exécute :

```
buttons_update()
encoders_update()
led_service()
mux_pots_scan()
```

Et incrémente :

```
engine_tick_count
```

---

# 7. UI Tasklet

Module :

```
ui_tasklet.c
```

Responsabilité :

```
initialisation lazy
+
appel ui_core_tick()
```

Pipeline :

```
ui_tasklet_poll()

if first call
    drv_display_init()
    ui_core_init()

ui_core_tick()
```

---

# 8. UI Core

Module :

```
ui_core.c
```

Responsabilités :

```
- lecture encodeurs
- génération événements boutons
- navigation pages
- tick logique page active
```

Pipeline complet :

```
ui_core_tick()

1) encoder_consume_delta()

2) ui_param_handle_encoder()

3) ui_event_from_inputs()

4) ui_event_pop()

       ├─ ui_navigation_handle_event()
       └─ active_page->handle_event()

5) active_page->tick()
```

Le rendu est **géré ailleurs**.

---

# 9. Système d’événements

Module :

```
ui_event.c
```

Implémente une **queue circulaire lock-free**.

Taille :

```
UI_EVENT_Q_LEN = 32
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

Types :

```
UI_EVENT_BUTTON_PRESS
UI_EVENT_BUTTON_RELEASE
```

---

# 10. Navigation entre pages

Module :

```
ui_navigation.c
```

Navigation **data-driven** via une table :

```
static const ui_nav_rule_t g_ui_nav_rules[]
```

Exemple :

```
BTN_PARAM_1 → PAGE_PARAM_TEST
BTN_PARAM_2 → PAGE_MAIN
BTN_PARAM_3 → PAGE_HALL_KEY_DEBUG
BTN_PARAM_4 → PAGE_CALIBRATION
```

Pipeline :

```
event bouton
      ↓
ui_navigation_handle_event()
      ↓
ui_page_set()
```

---

# 11. Page Manager

Module :

```
ui_page_manager.c
```

Responsabilités :

```
- registry statique des pages
- gestion page active
- enter / leave hooks
```

Structures :

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

---

# 12. Structure d’une page

Interface :

```
ui_page_t
```

Fonctions :

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

# 13. Renderer OLED

Module :

```
ui_renderer_oled.c
```

Rôle :

Construire le **framebuffer complet**.

Pipeline :

```
ui_renderer_oled_draw()

page = ui_page_get()

drv_display_clear()

page->render()
```

Note :

```
pas de flush SPI ici
```

Protection :

```
g_ui_rendering flag
```

---

# 14. Service de flush écran

Module :

```
display_flush_service.c
```

Responsabilité :

Envoyer le framebuffer vers l’écran.

Pipeline :

```
drv_display_update()
```

Ce module :

* limite la fréquence
* évite collision avec rendu

---

# 15. Driver écran

Module :

```
drv_display.c
```

Driver **SSD1309 SPI**.

Architecture :

```
framebuffer software
+
dirty page tracking
+
flush SPI
```

Framebuffer :

```
128 × 64
1 bit / pixel
```

Mémoire :

```
buffer[1024 bytes]
```

---

## Dirty tracking

Variables :

```
display_dirty
dirty_pages
```

Lors d’un pixel modifié :

```
dirty_pages |= page_bit
```

Flush :

```
drv_display_update()

for each page dirty
    SPI send page
```

---

# 16. Primitives graphiques

Fournies par :

```
drv_display
```

Primitives :

```
draw_pixel
draw_rect
fill_rect
clear_rect

draw_char
draw_text
draw_number
```

Fonts :

```
FONT_5X7
FONT_4X6
```

---

# 17. Système de paramètres UI

Module :

```
ui_param.c
```

Concept :

```
encoder → param_id → param_registry
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

Pipeline :

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
min
max
step
```

---

# 18. Pages existantes

Pages enregistrées dans :

```
ui_core_init()
```

Ordre :

```
0 → MAIN
1 → PARAM_TEST
2 → HALL_DEBUG
3 → CALIBRATION
```

---

## Calibration Page

Module :

```
ui_page_calibration.c
```

Fonction :

Calibration capteurs hall.

UI :

```
grid 8 × 2
16 cellules
```

Affichage :

```
rectangle
niveau = nombre de hits
```

Quand calibration terminée :

```
save calibration
affiche "CAL OK"
attend 1 s
retour MAIN
```

---

# 19. Architecture finale UI

```
UI SYSTEM

main loop
 ├─ engine_tasklet
 │     └─ input scan
 │
 ├─ ui_tasklet
 │     └─ ui_core
 │           ├─ encoders
 │           ├─ events
 │           ├─ navigation
 │           └─ page logic
 │
 ├─ ui_renderer_oled
 │     └─ build framebuffer
 │
 └─ display_flush_service
       └─ SPI OLED flush
```

---

# 20. Problème OLED initial (résolu)

Ancien comportement :

```
render + SPI flush à chaque tick UI
≈1500 FPS
```

Problèmes :

```
SPI saturé
HAL_SPI_Transmit bloquant
glitches écran
```

---

# 21. Solution actuelle

Découplage :

```
UI tick      : ~1500 Hz
UI render    : ~30 Hz
OLED flush   : ~30 Hz
```

Bénéfices :

```
✔ charge CPU stable
✔ SPI non saturé
✔ rendu fluide
✔ architecture modulaire
✔ séparation logique / rendu / IO
```

---

# 22. Conclusion

Architecture actuelle :

Points forts :

```
✔ UI déterministe basée audio
✔ rendu décorrélé du SPI
✔ page system propre
✔ navigation data-driven
✔ framebuffer + dirty tracking
✔ code modulaire
```

Points restant améliorables :

```
- SPI toujours blocking
- rendu full frame (clear + redraw)
- snprintf dans certaines pages
```

Mais la structure est maintenant **robuste et stable pour un système embarqué temps réel**.
