# BRICK — REGISTRE D’AUDIT CROSS-DOMAIN / BOOT / USB / IRQ

## Statut

Ce document centralise les anomalies **déjà découvertes** dans le secteur CROSS-DOMAIN.

Dernière consolidation : **après ROUND 5**.

Décision produit importante :

> l’ancien middleware USB Device/Host sera supprimé et remplacé par **TinyUSB** pour gérer MIDI Device/Host sur le même port.

Conséquence :

- ne pas investir dans une refonte durable interne de l’ancienne stack ;
- distinguer les problèmes métier qui survivront à TinyUSB des détails middleware condamnés.

---

# 1. Architecture cible

## M4 futur

- CONTROL
- Seq
- MIDI sémantique
- Storage
- USB
- UI

## M7 futur

- Audio IRQ
- engines
- DSP
- voices
- mix
- maintenance Audio locale

Le port H747 doit être principalement physique après clean architecture.

---

# 2. CONTROL→AUDIO multi-producer

Déjà démontré :

- CONTROL_RT
- UI_SERVICE
- STORAGE_IO

peuvent atteindre la FIFO finale.

Classification : `P0 / BEFORE H747`

Ne pas recompter sauf nouvelle source.

---

# 3. USB Device start avant `midi_init()`

Ordre connu :

```text
control_domain_init
→ board_usb_device_init
→ usb role/device start
→ callbacks MIDI possibles

puis

control_domain_start
→ midi_init
```

Risque :

- paquet reçu puis effacé par init MIDI.

Classification : `P0/P1 — boot order`

---

# 4. Ancien TX MIDI Device — buffer stack asynchrone

Ancienne stack :

- `midi.c` transmet des buffers locaux ;
- backend conserve le pointeur pour envoi async ;
- lifetime invalide ;
- corruption possible.

Classification :

`BLOCKING CURRENT H743` si nécessaire avant migration,
sinon `DEFER TO TINYUSB MIGRATION`.

---

# 5. MIDI TX Device — state partagé / MPSC

Producers possibles :

- CONTROL_RT
- TIM5 IRQ
- USB_SERVICE

Sections critiques protègent partiellement H743 mais ownership non propre.

Classification :

`BEFORE H747`
et probablement `FIX AS PART OF TINYUSB`.

---

# 6. MIDI Host queue — faux SPSC via discard

`midi_host_event_tail` peut être modifié par :

- CONTROL pop ;
- USB_SERVICE discard.

Classification : `P1`

---

# 7. MIDI Host — événements stale après role switch

Des événements Host peuvent survivre au stop Host/start Device.

Classification : `P1`

---

# 8. TIM12 avant `seq_runtime_init()`

TIM12 démarre avant que Seq runtime soit prêt.

Classification : `P1 — BEFORE H747`

---

# 9. TIM7 avant binding encodeur/UI

Round 4.

TIM7 démarre avant :

- `ui_core_init()`
- publication binding encodeur.

Un événement peut être capturé avec binding nul puis perdu.

Classification : `P1 — BEFORE H747`

---

# 10. `g_display_state` écrit depuis SPI Error IRQ

Round 4.

- writer IRQ ;
- reader UI task ;
- pas de protocole/snapshot/volatile suffisant démontré.

Classification : `P1 — BEFORE H747`

---

# 11. Boundary MIDI non indépendante de l’ancienne stack

Round 3/4.

`midi.c` dépend encore de :

- `hUsbDeviceFS`
- `USBD_MIDI_*`

et lie certaines queues/reset métier au lifecycle transport.

Classification : `FIX BEFORE TINYUSB`

---

# 12. `MIDI_DEST_USB` ne cible que Device

Round 4.

- TX Host absent ou perdu ;
- `midi_host_send()` non utilisé.

Classification : `P1 — FIX BEFORE TINYUSB / BEFORE H747`

---

# 13. Device et Host ont deux représentations transport avant CONTROL

Round 4.

Device et Host ne convergent pas encore assez tôt vers une représentation commune.

Classification : `P1 — FIX AS PART OF TINYUSB`

---

# 14. `button_states` / `enc_accumulated_delta` partagés CONTROL/UI

Round 3.

Risques :

- lost events ;
- delta écrasé ;
- reset concurrent.

Classification : `P1`

---

# 15. UI modifie directement des états CONTROL/AUDIO

Round 3.

Exemples :

- encoder dispatcher ;
- Audio FX ;
- mute ;
- param installs.

Classification : `P1`

---

# 16. UI lit/réinitialise des états STORAGE non publiés

Round 3.

Exemples :

```text
g_progress
g_present
g_project_save
g_sd_preview.gain
```

Classification : `P1`

---

# 17. Hall ADC — sémantique exécutée dans IRQ

Round 5.

`HAL_ADC_ConvCpltCallback()` exécute directement :

- détecteur Hall complet ;
- mutation d’état métier ;
- publication événements.

Owner naturel futur :

`CONTROL_RT / M4`.

Invariant cible :

> IRQ capture/produit les samples ou événements bruts ; CONTROL possède la sémantique.

Classification : `P1 — BEFORE H747`

---

# 18. MIDI clock TIM5 — sémantique dans IRQ

Round 5.

TIM5 callback déclenche directement :

- génération MIDI ;
- routing/destination.

`midi_clock_mode` et `midi_clock_dest` écrits par CONTROL mais lus IRQ sans protocole propre.

Classification :

`P1 — BEFORE H747`
`FIX BEFORE TINYUSB` pour la frontière métier.

Appel backend USB actuel :

`DEFER TO TINYUSB MIGRATION`.

---

# 19. Power button — consumer runtime absent

Round 5.

`power_shutdown_service()` n’est appelé nulle part.

Conséquence :

> bouton shutdown/reboot runtime sans consumer.

La fonction agrège actuellement des arrêts directs :

- AUDIO ;
- UI ;
- USB.

Donc la brancher naïvement créerait un nouvel ownership leak.

Classification :

`P1 — BLOCKING CURRENT H743`

USB shutdown actuel :

`DEFER TO TINYUSB MIGRATION`

---

# 20. FUSB302 EXTI avant init

Un `irq_pending` peut être posé avant `fusb302_init()`, puis perdu lors du `memset`.

Classification :

`DEFER TO TINYUSB MIGRATION`

---

# 21. AUDIO_BG ↔ IRQ races

Connues et documentées dans registre CONTROL/AUDIO.

Ne pas recompter ici.

---

# 22. `g_audio_sample_clock`

Connu et documenté dans CONTROL/AUDIO.

Ne pas recompter.

---

# 23. Zones explicitement validées comme propres

À ne pas rouvrir sans nouvelle preuve :

- boot hardware global hors cas listés ;
- DMA/SAI principal ;
- SD/DMA callbacks principaux ;
- LED DMA principal ;
- USB callbacks hors vieux middleware ;
- MIDI semantic polling dans CONTROL hors TIM5 clock leak ;
- MIDI Device RX principal ;
- Host internal RX queue confinée au service ;
- routing MIDI sémantique principal dans CONTROL ;
- AUDIO_BG_LOCAL hors races Audio déjà connues ;
- STORAGE_IO principal ;
- UI_SERVICE principal hors shared states listés ;
- flags IRQ simples déjà audités ;
- absence de nouveau direct USB→Audio/Seq/Track/Param au Round 5.

---

# 24. Classification TinyUSB

Pour chaque future anomalie USB/MIDI :

## `FIX BEFORE TINYUSB`
Frontière métier incorrecte indépendamment du middleware.

## `FIX AS PART OF TINYUSB`
Doit naturellement être corrigé pendant migration.

## `DEFER TO TINYUSB MIGRATION`
Bug strictement interne à l’ancienne stack.

## `BLOCKING CURRENT H743`
Nécessite éventuellement un micro-fix temporaire pour rendre BRICK utilisable avant migration.

---

# 25. Règles pour prochains audits

Ne compter comme nouveau que :

- nouveau producer-before-owner ;
- nouveau IRQ/task leak ;
- nouveau shared state dangereux ;
- nouveau couplage métier bloquant TinyUSB ;
- nouveau blocker H747 ;
- nouveau power/boot lifecycle bug.

Ne pas recompter les entrailles USBD/USBH condamnées.

Mettre à jour ce document à la fin de chaque round.

---

## ROUND 6

### R6-IRQ-01 — NEW INSTANCE OF KNOWN RULE

- Classification : `P1 / BEFORE H747`
- Contexts : bootstrap `main` et IRQ SAI/DMA RX ; lecteur `UI_SERVICE`.
- Callchain : `audio_start()` → `board_audio_start_stream()` →
  `HAL_SAI_RxHalfCpltCallback()` / `HAL_SAI_RxCpltCallback()` →
  `audio_boot_diag_producer_publish_cpu()` ; en parallele,
  `audio_start()` → `audio_boot_diag_producer_publish_state()`.
- Owner attendu : producteur diagnostic unique cote Audio M7 ; `UI_SERVICE`
  lecteur uniquement.
- Cause : `g_audio_diag` et `g_audio_boot_diag_layout` sont publies par le
  bootstrap et par l'IRQ RX sans serialisation. Le protocole seqlock suppose
  un seul writer ; une preemption peut donc perdre ou melanger une publication.
- Impact H743 : incoherence possible de l'etat/error et de la charge CPU dans
  le diagnostic partage.
- Impact H747 : fuite de ownership IRQ/IPC a supprimer avant le split M4/M7.
- TinyUSB : `N/A` (hors USB/MIDI).
- Nature : nouvelle instance de la regle partage cross-context / IRQ-task,
  pas une nouvelle categorie architecturale.

---

# 26. Nouvelles découvertes à ajouter

```text
## ROUND N

### ID — classification
- Contexts :
- Callchain :
- Owner attendu :
- Cause :
- Impact H743 :
- Impact H747 :
- TinyUSB :
```
