# 🧱 Plan d’intégration USB Audio — BRICK6 (REBOOT depuis exemple ST)

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

## 2) Nouvelle stratégie (basée sur ST)

### ✅ 2.1 Base de départ

➡️ On part de l’exemple ST :

> **Composite_Audio_HID** (ou Audio seul si plus simple au début)

Ce projet fournit déjà :

* ✅ infrastructure composite ST
* ✅ classe USB Audio officielle ST
* ✅ endpoints isochrones
* ✅ callbacks propres
* ❌ BSP codec (qu’on va jeter)

---

### ✅ 2.2 Architecture cible

On aura :

```
USBD Composite (ST)
 ├── Audio Class (ST)
 │     └── branché sur moteur BRICK6
 └── MIDI Class (ton existant)
```

👉 On **n’écrit PAS** de classe composite à la main.
👉 On **utilise le mécanisme composite ST**.

---

## 3) Stratégie d’intégration

### Phase A — Audio seul (sans MIDI)

Objectif :

> Avoir un **USB Audio OUT + IN reconnu par Windows/macOS** et stable.

* Partir de l’exemple Audio ST
* Supprimer :

  * BSP codec
  * I2C, WM8994, etc
* Remplacer dans :

```
usbd_audio_if.c
```

Les callbacks :

```c
AUDIO_Play()
AUDIO_Record()
```

par :

```
→ push / pop vers TON moteur audio
```

Au début :

* TX = silence
* RX = ignoré

---

### Phase B — Greffe du MIDI

Quand Audio seul fonctionne :

* Ajouter la classe MIDI dans le composite ST
* Réutiliser :

  * ton `usbd_midi.c`
  * ses descripteurs
* On obtient :

```
Audio + MIDI composite ST officiel
```

---

## 4) Architecture audio interne (inchangée)

```
Audio Engine
 ├── Backend SAI (maître d’horloge)
 └── Backend USB Audio (consommateur/producteur)
```

* Le moteur reste maître
* L’USB est un backend comme un autre

---

## 5) Backend USB Audio BRICK6

**Fichiers :**

```
usb_audio_backend.c / .h
```

**Rôle :**

* FIFO circulaire TX/RX
* Format interne : `int32_t`
* API :

```c
void usb_audio_backend_init(void);
uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames);
void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames);
```

* Aucune dépendance USB directe

---

## 6) Raccordement avec la classe Audio ST

Dans :

```
usbd_audio_if.c
```

On remplace :

```c
BSP_AUDIO_OUT_Play(...)
BSP_AUDIO_IN_Record(...)
```

par :

```c
usb_audio_backend_push_frames(...)
usb_audio_backend_pop_frames(...)
```

Les callbacks USB :

* ne font que :

  * copier
  * adapter format si besoin
  * compléter avec silence si underflow

---

## 7) Format et débit USB

* USB Audio Class 1.0
* 48 kHz
* 2 canaux
* 24 bits stockés en 32 bits
* 8 bytes par frame stéréo

USB FS = 1 ms frame :

```
48 frames * 8 bytes = 384 bytes par paquet
```

---

## 8) Descripteurs USB

➡️ On part **des descripteurs ST Audio existants** :

* Ils sont déjà valides
* Déjà reconnus par Windows / macOS
* On modifie seulement :

  * le format (24 bits)
  * le nombre de canaux si besoin

---

## 9) Politique temporelle

* SAI = horloge maître
* USB = backend adaptatif
* Pas d’asservissement dans la phase 1
* Pas de feedback endpoint au début

---

## 10) Mémoire

* Pas de cache / ou géré plus tard
* Buffers USB :

  * statiques
  * alignés 32 bits
* FIFO USB :

  * ~4 à 8 ms de profondeur (~2–4 KB)

---

## 11) Plan d’intégration par étapes



### Étape 1 — Débrancher le codec ST

* Supprimer BSP audio
* Mettre :

  * TX = silence
  * RX = jeté

---

### Étape 2 — Brancher le backend BRICK6

* Implémenter `usb_audio_backend`
* Relier à `usbd_audio_if.c`
* Le PC reçoit le son du moteur

---

### Étape 3 — Ajouter le MIDI

* Ajouter la classe MIDI dans le composite ST
* Vérifier :

  * MIDI fonctionne toujours
  * Audio toujours OK

---

### Étape 4 — Raffinements

* Format exact
* Latence
* Plus tard :

  * feedback endpoint
  * asservissement fin

---

## 12) Règles d’or

* ❌ Pas de logique audio dans les callbacks USB
* ❌ Pas de blocage
* ❌ Pas de malloc
* ❌ Pas de classe composite maison
* ✅ On s’appuie sur ST
* ✅ USB = backend passif
* ✅ Architecture BRICK6 inchangée

---

# 🏁 Conclusion

Cette approche :

* ✅ s’appuie sur du code **déjà validé par ST**
* ✅ évite 100% des pièges de descripteurs
* ✅ garantit la compatibilité Windows / macOS
* ✅ te fait gagner des **semaines**
* ✅ isole proprement USB du moteur audio

