# 🧱 Plan d’intégration USB Audio — BRICK6 (version consolidée)

## 1) Rappel du contexte

* STM32H743, USB Full Speed
* Pas de RTOS, pas de malloc
* Architecture coopérative par tasklets
* Audio interne en **24 bits stockés en 32 bits**
* Audio cadencé par **SAI + DMA**
* USB Device déjà présent pour **MIDI**
* On veut ajouter **USB Audio Device class compliant**
* Sans casser l’existant

---

## 2) Décision d’architecture

### ✅ 2.1 Classe USB : stratégie A

➡️ On implémente **UNE SEULE classe USB composite maison** :

```
USBD_BRICK6_COMPOSITE
 ├── MIDI
 └── AUDIO
```

* Elle expose :

  * 1 config descriptor unique
  * Interfaces MIDI existantes
  * Interfaces Audio Control + Audio Streaming
* Elle dispatch :

  * les callbacks vers le code MIDI existant
  * et vers le nouveau backend USB Audio

👉 On **n’essaie pas** de faire cohabiter deux classes ST séparées.

---

### ✅ 2.2 Audio : backend supplémentaire

```
Audio Engine
 ├── Backend SAI (maître d’horloge)
 └── Backend USB Audio (consommateur)
```

* Le moteur produit au rythme SAI
* Le backend USB :

  * consomme ce qu’il peut
  * si underflow → envoie silence
  * si overflow → drop
* **USB ne cadence jamais le moteur**

---

## 3) Hypothèses simplificatrices (phase 1)

* Pas de feedback endpoint
* Pas d’asservissement de cadence
* Pas d’entrée audio USB au début (optionnel)
* Pas de cache / MPU (désactivés dans CubeMX)
* Mode **adaptive / synchronous**

---

## 4) Format et débit

* USB Audio Class 1.0
* 48 kHz
* 2 canaux
* 24 bits stockés en 32 bits
* 8 bytes par frame stéréo

USB FS = 1 ms frame :

```
48 frames * 8 bytes = 384 bytes par paquet isochrone
```

---

## 5) Organisation des modules

### 5.1 Nouveau backend audio

**Fichier :**

```
usb_audio_backend.c / .h
```

**Responsabilités :**

* FIFO circulaire TX (et plus tard RX)
* Stockage en `int32_t` natif (copie brute)
* API simple :

```c
void usb_audio_backend_init(void);
uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames);
void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames);
```

* Aucune IRQ
* Aucune dépendance USB directe

---

### 5.2 Intégration moteur

Dans le moteur audio :

* Après rendu d’un bloc :

  * push dans le backend USB Audio
* Le moteur **ne sait pas** si quelqu’un consomme ou non.

---

### 5.3 Classe USB composite

**Nouveaux fichiers (dans usb_stack ou middlewares) :**

```
usbd_brick6_composite.c / .h
```

Rôle :

* Fournir :

  * Init / DeInit
  * Setup
  * DataIn / DataOut
  * GetCfgDesc
* Router :

  * les endpoints MIDI → code MIDI existant
  * les endpoints Audio → usb_audio_backend + glue

---

### 5.4 Interface Audio ST

On peut :

* soit partir de `usbd_audio.c` ST simplifié
* soit intégrer directement la logique dans la classe composite

Dans tous les cas :

* Les callbacks Audio :

  * ne font que :

    * demander N frames au backend
    * copier dans le buffer USB
    * relancer un transfert

---

## 6) Descripteurs USB

Dans :

```
App/usb_stack/usbd_desc.c
```

* Un seul device descriptor
* Un seul config descriptor
* Contient :

  * Interfaces MIDI existantes
  * * Audio Control interface
  * * Audio Streaming interface IN

Audio Streaming :

* Isochronous IN
* MaxPacketSize = 384
* Format Type I
* 2 canaux
* Subframe size = 4
* Bit resolution = 24

---

## 7) Flux de données

### 7.1 TX (moteur → PC)

```
Engine render block
  → usb_audio_backend_push_frames()

USB IN callback
  → usb_audio_backend_pop_frames()
  → copie vers buffer USB
  → si pas assez : compléter avec silence
```

---

### 7.2 RX (plus tard)

Même principe, en sens inverse, mais ignoré au début.

---

## 8) Politique temporelle

* SAI = horloge maîtresse
* USB = consommateur opportuniste
* Pas de blocage
* Pas de rétroaction dans la phase 1

---

## 9) Mémoire

* Pas de cache → pas de nettoyage / invalidation
* Buffers USB :

  * statiques
  * alignés 32 bits
* FIFO USB :

  * ~4 à 8 ms de profondeur
  * donc ~2 à 4 KB

---

## 10) Fichiers modifiés / ajoutés

### Modifiés

* `App/usb_stack/usbd_desc.c` → descripteur composite
* `App/usb_stack/usb_device.c` → enregistrer la classe composite
* `App/usb_stack/usbd_conf.c` → taille static malloc si besoin

### Ajoutés

* `usbd_brick6_composite.c/.h`
* `usb_audio_backend.c/.h`
* éventuellement `usbd_audio_minimal.c/.h` ou équivalent

---

## 11) Plan d’intégration par étapes

### Étape 1 — Enumération

* Classe composite
* Descripteurs OK
* Le PC voit :

  * MIDI
  * Audio

---

### Étape 2 — Silence TX

* Endpoint Audio IN actif
* Envoi de zéros
* Vérifier :

  * stabilité
  * pas de glitch
  * bon débit

---

### Étape 3 — RX jeté (optionnel)

* Si endpoint OUT activé :

  * on reçoit
  * on ignore

---

### Étape 4 — Connexion au moteur

* Le moteur push dans le backend USB
* L’USB consomme
* Le PC entend le vrai son

---

### Étape 5 — Plus tard

* Feedback endpoint
* Asservissement fin
* Entrée audio USB

---

## 12) Règles d’or

* ❌ Pas de logique audio dans les callbacks USB
* ❌ Pas de blocage
* ❌ Pas de malloc
* ❌ Pas de dépendance circulaire
* ✅ USB = backend passif
* ✅ Architecture inchangée

---

# 🏁 Conclusion

Cette approche :

* respecte totalement BRICK6
* minimise les risques
* permet un bring-up progressif
* prépare proprement l’async plus tard
* ne sacrifie ni la qualité ni la maintenabilité


