# Audit des dépendances circulaires — BRICK6_CUBE_fonctionnel

## 1️⃣ Résumé exécutif

- ✅ **Aucune dépendance circulaire dangereuse détectée** entre les modules analysés.
- ⚠️ **Couplage élevé mais non cyclique** : `diagnostics_tasklet` et `brick6_app_init` touchent à de nombreux sous-systèmes (logique d’observabilité et d’initialisation). Cela reste acceptable si l’on maintient leur rôle “haut de la pyramide”.

## 2️⃣ Graphe de dépendances (texte)

```
main
 ├── brick6_app_init
 │    ├── diagnostics_tasklet
 │    ├── sd_stream
 │    │    └── sd_audio_block_ring
 │    ├── sdram
 │    ├── audio_out
 │    │    ├── audio_in
 │    │    ├── engine_tasklet
 │    │    └── sd_audio_block_ring
 │    ├── audio_in
 │    ├── engine_tasklet
 │    ├── midi
 │    ├── usb_host
 │    └── usb_device (CubeMX)
 ├── audio_out
 │    ├── audio_in
 │    ├── engine_tasklet
 │    └── sd_audio_block_ring
 ├── engine_tasklet
 ├── sd_stream
 │    └── sd_audio_block_ring
 ├── usb_host
 ├── midi_host
 │    ├── midi
 │    └── usb_host
 ├── ui_tasklet
 └── diagnostics_tasklet
      ├── audio_in
      ├── audio_out
      ├── engine_tasklet
      ├── sd_stream
      └── sdram (+ sdram_alloc)
```

## 3️⃣ Liste détaillée des cycles trouvés

- **Aucun cycle direct ou indirect identifié** sur le périmètre demandé.

## 4️⃣ Analyse architecturale

### Modules “en bas de la pyramide” (fondations)
- `sd_audio_block_ring` : buffer de base, pas de dépendance externe.
- `engine_tasklet` : logique autonome (tick), sans dépendance externe.
- `audio_in` : acquisition DMA, dépend seulement des HAL SAI/UART.
- `sdram` : init/test mémoire externe, dépend seulement HAL/FMC/UART.

### Modules “en haut de la pyramide” (orchestration)
- `main` : boucle principale et sequencing des tasklets.
- `brick6_app_init` : orchestrateur d’initialisation (touche SDRAM, SD, USB, audio, MIDI, diagnostics).
- `diagnostics_tasklet` : observabilité + tests, dépend de nombreux modules (audio, engine, SD, SDRAM).

### Modules trop couplés
- `diagnostics_tasklet` est volontairement “central” : il interroge presque tout. **Couplage élevé mais directionnel** (tous vers diagnostics, pas l’inverse).
- `brick6_app_init` est un “orchestrateur” qui touche à tous les sous-systèmes. C’est normal pour un module d’init mais doit rester isolé du runtime.

### Isolation diagnostics
- `diagnostics_tasklet` **ne semble pas être dépendu** par les autres modules (sauf `brick6_app_init` qui l’utilise pour initialiser les logs SD). L’isolation est donc raisonnable.

### Protection audio / engine
- `audio_out` dépend de `engine_tasklet` et `audio_in` (nécessaire pour cadence et loopback).
- `engine_tasklet` ne dépend d’aucun autre module ⇒ **pas de boucle**.
- `audio_in` ne dépend pas de `audio_out` ⇒ **pas de boucle**.

## 5️⃣ Recommandations

1. **Conserver l’orientation “top-down”** : `main` et `brick6_app_init` doivent rester les seuls modules qui orchestrent plusieurs sous-systèmes.
2. **Limiter l’expansion de `diagnostics_tasklet`** :
   - éviter d’y ajouter des appels “fonctionnels” (ex : déclencher du traitement audio),
   - privilégier des interfaces de lecture (stats) plutôt que des commandes actives.
3. **Prévenir l’apparition de cycles futurs** :
   - si `midi` ou `audio` doit notifier des erreurs, préférer une interface de log abstraite (callback) plutôt qu’un include direct de `diagnostics_tasklet`.
4. **Couplage MIDI/USB** : actuellement `midi_host` dépend de `midi` et `usb_host`, mais l’inverse n’existe pas. Garder cette direction pour éviter un futur cycle `midi ↔ usb_host`.

