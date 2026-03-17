Voici le **nouveau `Audit_UI.md`** mis à jour pour refléter l’architecture actuelle (driver hybride **U8G2 + framebuffer interne + flush SPI séparé**).

---

# Audit UI – Architecture actuelle (Brick6)

## 1. Vue d’ensemble

L’interface utilisateur repose sur une **UI temps réel pilotée par le moteur audio**.

Architecture en **pipeline décorrélé** :

```
inputs → logique UI → rendu framebuffer → flush SPI écran
```

Objectifs :

```
✔ UI déterministe
✔ aucun travail UI dans l’IRQ audio
✔ rendu découplé du SPI
✔ coût CPU stable
✔ architecture modulaire
```

Le système combine maintenant :

```
U8G2 → moteur de dessin
Driver interne → framebuffer + SPI
```

U8G2 n’envoie **aucune donnée au display**.
Il dessine uniquement dans **notre framebuffer externe**.

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

La cadence UI dérive du moteur audio.

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
ui_core_tick ≈ 1500 Hz
```

Mais le rendu écran est **fortement limité**.

---

# 4. Limitation du rendu UI

Module :

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
UI render ≈ 30 FPS
```

Cela évite de reconstruire le framebuffer à 1500 Hz.

---

# 5. Limitation du flush écran

Module :

```
display_flush_service.c
```

Constante :

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

Protection :

```
if (ui_renderer_oled_is_rendering())
    return
```

Empêche un flush pendant un rendu.

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

Puis incrémente :

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
- logique de page
```

Pipeline :

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

Le rendu est géré ailleurs.

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

Navigation **data-driven**.

Table :

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
- page active
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

Cycle :

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

Responsabilité :

Construire **le framebuffer complet**.

Pipeline :

```
ui_renderer_oled_draw()

page = ui_page_get()

drv_display_clear()

page->render()
```

Important :

```
aucun flush SPI ici
```

Protection :

```
g_ui_rendering flag
```

---

# 14. Driver écran

Module :

```
drv_display.c
```

Driver **SSD1309 SPI**.

Architecture actuelle :

```
framebuffer interne
+
U8G2 comme moteur graphique
+
flush SPI manuel
```

---

# 15. Framebuffer

Mémoire :

```
128 × 64
1 bit / pixel
```

Taille :

```
1024 bytes
```

Stockage :

```
SDRAM
```

Buffer :

```
uint8_t buffer[1024]
```

---

# 16. Intégration U8G2

U8G2 est utilisé **uniquement comme moteur de dessin**.

Initialisation :

```
u8g2_SetupDisplay(...)
u8g2_SetupBuffer(...)
```

Le buffer fourni à U8G2 est **le framebuffer du driver**.

Donc :

```
U8G2
  ↓
framebuffer
  ↓
drv_display_update()
  ↓
SPI OLED
```

U8G2 **ne contrôle pas le bus SPI**.

---

# 17. Flush écran

Module :

```
drv_display_update()
```

Pipeline :

```
for page = 0..7
    set page address
    send 128 bytes
```

Donc :

```
8 pages × 128 bytes
```

Total :

```
1024 bytes / frame
```

---

# 18. Primitives graphiques

Implémentées via U8G2 :

```
draw_pixel
draw_rect
fill_rect
clear_rect
draw_line
```

Texte :

```
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

# 19. Paramètres UI

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

# 20. Pages existantes

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

# 21. Calibration Page

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

Fin calibration :

```
save calibration
affiche "CAL OK"
attend 1 s
retour MAIN
```

---

# 22. Architecture finale UI

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
 │     └─ build framebuffer (U8G2)
 │
 └─ display_flush_service
       └─ SPI OLED flush
```

---

# 23. Différence avec l’ancienne architecture

Avant :

```
driver maison complet
```

Maintenant :

```
driver hybride
```

Structure :

```
U8G2 → rendu graphique
Driver interne → SPI
Framebuffer → partagé
```

Avantages :

```
✔ primitives graphiques riches
✔ fonts U8G2
✔ rendu plus simple
✔ pas de dépendance SPI U8G2
✔ contrôle total du flush
```

---

# 24. Performance

Cadences :

```
UI tick     ≈ 1500 Hz
UI render   ≈ 30 Hz
OLED flush  ≈ 30 Hz
```

Coût :

```
render framebuffer ≈ très faible
SPI transfer ≈ 1 KB / frame
```

---

# 25. Points forts

```
✔ architecture déterministe
✔ rendu décorrélé du SPI
✔ U8G2 utilisé seulement pour le dessin
✔ framebuffer unique
✔ pipeline clair
✔ pas d’appel SPI dans U8G2
✔ UI stable et modulaire
```

---

# 26. Améliorations possibles

```
- SPI DMA au lieu de blocking
- dirty page tracking
- double buffering
- batching des draw calls
- profiler temps render
```

---

Si tu veux, je peux aussi te faire une **section supplémentaire dans l’audit qui explique exactement pourquoi ton port U8G2 buguait à l’init** (c’est très probablement lié au `u8x8_byte` driver manquant). C’est un point intéressant pour la doc.
