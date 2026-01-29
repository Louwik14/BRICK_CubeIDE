# 🧱 Plan d’intégration USB Audio — BRICK6 (version consolidée)

## 1) Contexte

* STM32H743, USB Full Speed
* Pas de RTOS, pas de malloc
* Architecture coopérative par tasklets
* Audio interne en **24 bits stockés en 32 bits**
* Audio cadencé par **SAI + DMA**
* USB Device déjà présent pour **MIDI**
* Objectif : ajouter **USB Audio Device class compliant**
* Sans casser l’architecture audio

---

## 2) Décision d’architecture

### 2.1 Classe USB

➡️ On implémente **UNE SEULE classe USB composite maison** :

```
USBD_BRICK6_COMPOSITE
 ├── MIDI
 └── AUDIO
```

* Une seule config descriptor
* Contient :

  * Interfaces MIDI
  * Interfaces Audio Control + Audio Streaming
* Le core USB ST ne gère **qu’une seule classe** → on ne peut pas empiler.

👉 L’ancienne classe MIDI device sera **absorbée** dans cette classe composite.

---

### 2.2 Rôle de l’audio USB

```
Audio Engine
 ├── Backend SAI (maître d’horloge)
 └── Backend USB Audio (consommateur)
```

* Le moteur produit au rythme SAI
* L’USB Audio :

  * consomme ce qu’il peut
  * si underflow → envoie silence
  * si overflow → drop
* **USB ne cadence jamais le moteur**

---

## 3) Hypothèses phase 1

* Pas de feedback endpoint
* Pas d’asservissement
* Pas d’entrée audio USB au début
* Pas de cache / MPU (désactivés)
* Mode **synchronous / adaptive**

---

## 4) Format et débit

* USB Audio Class 1.0
* 48 kHz
* 2 canaux
* 24 bits stockés en 32 bits
* 8 bytes par frame stéréo

USB FS = 1 ms frame :

```
48 frames × 8 bytes = 384 bytes / paquet isochrone
```

---

## 5) Architecture logicielle

### 5.1 Backend audio USB

**Fichier :**

```
App/audio/usb_audio_backend.c / .h
```

**Rôle :**

* FIFO circulaire TX (et plus tard RX)
* Stockage en `int32_t`
* API :

```c
void usb_audio_backend_init(void);
uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames);
void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames);
```

* Pas de dépendance USB
* Pas d’IRQ

---

### 5.2 Intégration moteur

* Après rendu d’un bloc audio :

  * push vers `usb_audio_backend`
* Le moteur **ne sait pas** qui consomme.

---

### 5.3 Classe USB composite

**Nouveaux fichiers :**

```
App/usb_stack/usbd_brick6_composite.c / .h
```

**Rôle :**

* Implémente :

  * Init / DeInit
  * Setup
  * DataIn / DataOut
  * SOF (optionnel)
  * GetCfgDesc
* Gère :

  * endpoints MIDI
  * endpoint Audio IN
* Contient :

  * le code MIDI device (repris / intégré)
  * le glue audio USB

---

## 6) Descripteurs USB

Dans :

```
App/usb_stack/usbd_desc.c
```

* Un seul device descriptor
* Un seul config descriptor
* Contient :

  * Interfaces Audio Control
  * Interface Audio Streaming IN
  * Interfaces MIDI

Audio Streaming :

* Isochronous IN
* wMaxPacketSize = 384
* Format Type I
* 2 canaux
* Subframe size = 4
* Bit resolution = 24

---

## 7) Flux de données

### 7.1 TX (BRICK6 → PC)

```
Engine render
   → usb_audio_backend_push_frames()

USB IN callback
   → usb_audio_backend_pop_frames()
   → compléter avec 0 si underflow
   → envoyer paquet USB
```

---

## 8) Politique temporelle

* SAI = horloge maîtresse
* USB = consommateur passif
* Pas de blocage
* Pas de dépendance cyclique

---

## 9) Mémoire

* Buffers USB statiques
* Alignés 32 bits
* FIFO ~ 4–8 ms ≈ 2–4 KB

---

## 10) Fichiers impactés

### 10.1 Fichiers existants à modifier

* `App/usb_stack/usbd_desc.c`
  → devient descripteur composite

* `App/usb_stack/usb_device.c`
  → enregistre `USBD_BRICK6_COMPOSITE`

* `App/usb_stack/usbd_conf.c`
  → ajustement buffers si nécessaire

---

### 10.2 Nouveaux fichiers

* `App/usb_stack/usbd_brick6_composite.c / .h`
* `App/audio/usb_audio_backend.c / .h`

---

## 11) Plan d’exécution

### Étape 1 — Bring-up USB composite

* Écrire :

  * classe composite
  * descripteur composite
* Objectif :

  * le PC voit :

    * MIDI
    * Audio device

---

### Étape 2 — Audio USB muet

* Endpoint audio IN actif
* Envoie **silence stable**
* Test :

  * Ableton / Windows / Linux
  * Pas de glitch, pas de reset USB

---

### Étape 3 — Connexion moteur

* Le moteur push vers backend USB
* L’USB consomme
* Le PC entend le vrai son

---

## 12) Règles absolues

* ❌ Pas de logique lourde en callbacks USB
* ❌ Pas de malloc
* ❌ Pas de blocage
* ❌ Pas d’USB qui pilote l’audio
* ✅ USB = backend passif
* ✅ Architecture audio intacte

---

## 13) Ce qu’on ne fait PAS (pour l’instant)

* Pas de feedback endpoint
* Pas d’entrée audio USB
* Pas de resampling
* Pas d’asservissement fin

---

# 🏁 Conclusion

Cette approche :

* respecte totalement BRICK6
* ne pollue pas l’engine
* permet un bring-up rapide
* prépare l’async plus tard
* reste maintenable et maîtrisée


