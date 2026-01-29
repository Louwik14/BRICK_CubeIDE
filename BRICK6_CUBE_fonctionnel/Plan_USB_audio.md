# Plan d’intégration USB Audio (BRICK6)

## 1) Analyse rapide de l’existant (USB + Audio)

### USB device (MIDI)
- La pile USB Device est initialisée manuellement dans `brick6_app_init()`, puis la classe MIDI est enregistrée par `MX_USB_DEVICE_Init()` via `USBD_RegisterClass(&USBD_MIDI)`. Cela confirme un device mono‑classe MIDI aujourd’hui. 
- Les descripteurs USB sont centralisés dans `App/usb_stack/usbd_desc.c` (strings, VID/PID, config/interface), et la configuration PCD/LL dans `App/usb_stack/usbd_conf.c`.
- Le MIDI device est géré via la classe ST `usbd_midi`, avec callbacks faibles `USBD_MIDI_OnPacketsReceived/OnPacketsSent` raccordés au module `midi.c`.
- La mémoire USB Device est déjà statique (pas de `malloc`) via `USBD_static_malloc()` dans `usbd_conf.c`.

### USB host (MIDI)
- USB Host est piloté en tasklet coopératif (boucle bornée) dans `usb_host_tasklet_poll_bounded()`, appelé depuis la main loop.

### Scheduler / tasklets
- La boucle principale est strictement coopérative, avec priorité audio en tête (`audio_tasklet_poll()`), puis engine, SD, USB host, MIDI host, UI, diagnostics.
- Les callbacks IRQ audio (SAI DMA) ne font que lever des flags et des compteurs. Le travail lourd est fait hors IRQ dans `audio_tasklet_poll()`.

### Audio SAI (backend existant)
- `audio_out.c` cadence le moteur via `engine_tasklet_notify_frames()` quand une demi‑période DMA est prête.
- Les buffers audio sont en `int32_t`, format 24‑bits alignés à gauche, et traités par blocs/ring buffers.
- Il existe déjà un TODO explicite pour la maintenance de cache D‑Cache sur STM32H7 dans `audio_tasklet_poll()`.

## 2) Objectif et contraintes rappelées
- Ajouter un **USB Audio Device Class 1.0** FS, 48 kHz, stéréo, 24‑bits dans 32‑bits, isochrone.
- Coexister avec MIDI USB (device composite).
- Zéro RTOS, zéro malloc, pas de logique lourde en callbacks USB, pas de refonte d’archi.
- Le backend USB Audio doit être un backend audio **supplémentaire** (comme SAI), sans casser l’existant.

## 3) Architecture cible proposée (adaptée à BRICK6)

### 3.1 Vue d’ensemble
```
Audio Engine
 ├── Backend SAI (existant)
 └── Backend USB Audio (nouveau)

USB Device
 ├── MIDI class (existant)
 └── Audio class (nouveau)
```

### 3.2 Principes d’intégration
- **Ne pas “polluer” la stack USB** : isoler le backend USB Audio dans un module applicatif dédié, avec une API minimale, calquée sur les modules audio existants.
- **Callbacks USB minimalistes** : IRQ/USB callbacks ne font que déposer les données (ou lever des flags), traitement dans tasklet dédiée.
- **Pas de copies inutiles** : données audio USB directement écrites/lues dans des buffers circulaires par blocs, alignés et adaptés au format 32‑bit.
- **Découplage** : le backend USB Audio n’impacte pas la cadence SAI. Il devient une source/sink audio **optionnelle** du moteur.

### 3.3 Modules à ajouter
**Nouveaux modules applicatifs (dans `Src/` + `Inc/`)**
- `usb_audio_backend.c/h`
  - Rôle : interface entre USB Audio class et moteur audio.
  - Gère buffers circulaires (RX/TX), mapping 24‑bit→32‑bit, stats, états (streaming/idle).
  - Fournit une API similaire à `audio_in/out` (init, start/stop, poll, get_buffer, etc.) sans IRQ.
- `usb_audio_tasklet.c/h` (optionnel si séparé)
  - Rôle : traitement périodique hors IRQ (drain/flush des endpoints isochrones, copie de blocs, gestion des underflows/overflows).

**Nouvelle classe USB (dans `App/Middlewares/.../STM32_USB_Device_Library/Class/`)**
- Ajouter la classe **USBD_AUDIO** (UAC1) côté Device.
- Fournir un `usbd_audio_if.c/h` adapté (interface callbacks) qui route vers `usb_audio_backend`.

**Composite Device**
- Introduire une couche composite **spécifique** (ex: `usbd_composite.c/h` dans `App/usb_stack/` ou dans `Middlewares`), qui agrège:
  - Interface MIDI (existante)
  - Interface Audio (nouvelle)
- Le composite se contente de déléguer les callbacks class à chaque sous‑classe sans changer l’archi applicative.

## 4) Intégration dans la pile USB Device

### 4.1 Où placer les descripteurs
- Garder **un seul fichier descripteur principal** (actuel `App/usb_stack/usbd_desc.c`).
- Étendre la **configuration descriptor** pour inclure les interfaces Audio + MIDI (composite).
- Conserver les strings globales (Manufacturer/Product), ajouter string interface Audio si nécessaire.

### 4.2 Classe composite
- `MX_USB_DEVICE_Init()` deviendra responsable de **l’enregistrement d’une classe composite unique** (au lieu d’enregistrer MIDI seul).
- Cette classe composite devra fournir:
  - Un `GetCfgDesc()` unique qui concatène MIDI + Audio.
  - La gestion des interfaces multiples (Audio Control + Audio Streaming + MIDI).

### 4.3 Rappel sur l’absence de malloc
- Reprendre la logique de `USBD_static_malloc()` existante (taille ajustée pour la nouvelle classe Audio).
- S’assurer que la classe Audio n’utilise pas `malloc` indirectement.

## 5) Flux de données proposé

### 5.1 RX (USB → moteur)
```
USB Iso IN (host -> device)
  -> Endpoint OUT Audio
  -> buffer RX circulaire (usb_audio_backend)
  -> tasklet USB audio (copie par blocs)
  -> ring buffer moteur (format 24b-in-32b)
```

### 5.2 TX (moteur → USB)
```
Moteur audio (blocs 24b/32b)
  -> buffer TX circulaire (usb_audio_backend)
  -> tasklet USB audio
  -> Endpoint IN Audio
```

### 5.3 Synchronisation
- Démarrer en **synchronous/adaptive** (sans feedback) pour sécuriser le bring‑up.
- Ajouter **feedback endpoint** en étape ultérieure pour asynchronisme propre.

## 6) Ce qui est réutilisé / modifié / nouveau

### Réutilisé
- `usb_stack` existant (init, PCD, descriptors base).
- Module `midi` et classe `usbd_midi` sans modification fonctionnelle.
- Architecture tasklet coopérative + ring buffers audio existants.

### Modifié
- `App/usb_stack/usbd_desc.c` : configuration descriptor composite.
- `App/usb_stack/usb_device.c` : enregistrement de la classe composite au lieu de MIDI seul.
- `App/usb_stack/usbd_conf.c` : taille de `USBD_static_malloc()` si nécessaire.

### Nouveau
- Classe `USBD_AUDIO` (UAC1) + interface `usbd_audio_if`.
- Couche composite `USBD_Composite` (ou équivalent maison).
- Backend applicatif USB Audio (`usb_audio_backend.*`) + tasklet associée.

## 7) Références ST à utiliser (et à éviter)

### À utiliser comme **référence technique uniquement**
- Exemple ST `STM32H743I-EVAL/Applications/USB_Device/Audio_Standalone` :
  - Descripteurs Audio UAC1 (`usbd_desc.c`).
  - Interface class (`usbd_audio_if.c`) pour comprendre les callbacks attendus.
  - Constantes de fréquence/format.

### À ne pas reprendre tel quel
- Toute la BSP audio (WM8994, BSP_AUDIO_*), incompatible avec BRICK6.
- La logique `usbd_audio_if.c` du BSP (play/stop/volume) : elle doit être remplacée par `usb_audio_backend`.
- Les initialisations CubeMX/board‑specific de l’exemple.

## 8) Plan d’intégration par étapes sûres

### Étape 1 — Énumération uniquement
- Ajouter composite + descripteurs Audio (sans flux).
- Vérifier que l’USB voit bien MIDI + Audio sur PC (Device Manager / `lsusb`).

### Étape 2 — Flux de silence (TX)
- Activer endpoint isochrone IN (device → host).
- Envoyer silence 24‑bit/32‑bit via buffer TX local, sans lien moteur.

### Étape 3 — RX “jeté”
- Activer endpoint isochrone OUT (host → device).
- Réceptionner les buffers et les compter, sans injection moteur (juste stats).

### Étape 4 — Connexion au moteur audio
- Connecter RX/TX aux ring buffers audio internes.
- Vérifier latence, underflow, et stabilité de cadence.

### Étape 5 — Feedback / async
- Ajouter endpoint feedback pour synchronisation fine.
- Ajuster la cadence selon drift SAI/USB si nécessaire.

## 9) Points de vigilance STM32H7
- **D‑Cache** : toute zone DMA doit être nettoyée/invalidation explicite (déjà signalé comme TODO dans `audio_tasklet_poll()`).
- **Alignement mémoire** : buffers USB isochrones en 32‑bit alignés, idéalement en DTCM/AXI SRAM selon usage.
- **ISR USB** : éviter toute copie lourde ou logique audio en callbacks USB.
- **SOF** : utile pour la cadence (feedback), mais à traiter hors IRQ.
- **Isochronous FS** : vérifier taille paquet (48 kHz * 2 ch * 4 octets = 384 B / ms).

## 10) Résumé décisionnel
- Le backend USB Audio est traité **comme un backend audio supplémentaire**, sans toucher au SAI existant.
- L’intégration se fait **progressivement**, d’abord énumération, puis flux minimal, puis lien moteur.
- On réutilise l’exemple ST **seulement pour les descripteurs et l’API de classe**, jamais pour l’archi.
