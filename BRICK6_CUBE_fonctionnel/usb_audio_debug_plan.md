# Plan de debug — USB Audio BRICK6 (preuves UART)

## Objectif
Obtenir des **preuves chiffrées** que le flux USB audio traverse (ou pas) chaque étape :

```
USB Iso OUT
 → tud_audio_rx_done_isr()
 → audio_io_usb_on_rx_samples()
 → buffers USB (ring)
 → audio_core_process_block()
 → audio_out / SAI
```

Aucun correctif fonctionnel : **uniquement des logs 1 Hz** via `diagnostics_tasklet`.

---

## 1) Liste ordonnée des preuves à observer

1. **IRQ RX USB appelée**
   - Preuve: `USB RX done` augmente.
2. **USB fournit des samples**
   - Preuves: `samples` et `bytes` augmentent, `zero_reads` reste bas.
3. **Ring buffer USB alimenté**
   - Preuves: `written` augmente, `buf_avail` > 0.
4. **Pas de saturation du buffer**
   - Preuves: `dropped` reste à 0, `buf_free` > 0.
5. **audio_core consomme**
   - Preuves: `core_calls` augmente, `core_src` correspond à USB ou MIX.
6. **audio_core utilise réellement les blocs USB**
   - Preuves: `core_usb_used` augmente.
7. **Pas de repli SAI (fallback)**
   - Preuve: `core_fallback` reste stable.

---

## 2) Logs attendus si tout fonctionne

- `USB RX done` augmente chaque seconde.
- `bytes` et `samples` augmentent régulièrement.
- `written` augmente, `dropped = 0`.
- `buf_avail` oscille (remplissage / consommation), `buf_free` > 0.
- `core_calls` augmente au rythme audio.
- `core_src = 1` (USB) ou `3` (MIX).
- `core_usb_used` augmente régulièrement.
- `core_fallback` reste quasi constant.
- `last_frames = 256`, `last_avail` ≥ `last_samples`.

---

## 3) Logs attendus si ça casse (par étape)

### A) Callback USB jamais appelé
- `USB RX done` reste à 0.
- `bytes = 0`, `samples = 0`.

### B) Callback appelé mais pas de données
- `USB RX done` augmente.
- `bytes` faible ou nul.
- `samples` faible, `zero_reads` augmente.
- `written` reste bas.

### C) Buffer USB rempli mais saturé
- `written` augmente, **`dropped` augmente**.
- `buf_free` proche de 0.
- `buf_avail` plafonne proche de `buf_cap`.

### D) audio_core ne consomme pas USB
- `core_calls` augmente.
- `core_src = 0` (SAI) ou `2` (SD).
- `core_usb_used` n’augmente pas.
- `core_fallback` augmente.

### E) audio_core consomme mais pas assez (cadence / alignement)
- `core_src = 1` (USB) ou `3` (MIX).
- `core_usb_missed` augmente.
- `last_avail < last_samples` fréquemment.
- `core_fallback` augmente.

---

## 4) Checklist de debug terrain

1. **Brancher USB Audio** et lancer la lecture audio depuis le host.
2. Observer les logs `USB RX ...`.
3. Vérifier que `USB RX done` et `samples` montent.
4. Vérifier que `written` monte et que `dropped` reste à 0.
5. Vérifier que `core_src` est USB (1) ou MIX (3).
6. Vérifier que `core_usb_used` augmente.
7. Si `core_fallback` augmente → le flux USB ne nourrit pas le bloc.
8. Si `core_usb_missed` augmente → problème de cadence / taille buffer.

---

## 5) Correspondance des sources audio (core_src)

- `0` = SAI (fallback)
- `1` = USB
- `2` = SD
- `3` = MIX
