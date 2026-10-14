# BRICK UI Simulator

Le simulateur Windows compile le vrai pipeline BRICK : entrées physiques, événements,
navigation, pages, renderer OLED, U8g2 et framebuffer 128 × 64. SDL3 ne présente que
le framebuffer. Les services audio, DSP, MIDI et stockage restent exclus.

## Prérequis et compilation

- Windows 10, CMake, Ninja
- GCC UCRT64 dans `C:\msys64\ucrt64\bin`
- MSYS2 `mingw-w64-ucrt-x86_64-sdl3`

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\run.ps1
```

La fenêtre est agrandie ×8 par défaut (`--scale 6` pour changer). Le clavier est
décrit dans [KEYBOARD.md](KEYBOARD.md) et configuré par `config/keymap.json`.

## Capture

```powershell
.\sim\build\windows-ucrt\brick-ui-sim.exe --capture sim/out/screen.png
```

Le PNG vient directement du framebuffer de 1 024 octets, sans bordure, en 128 × 64.
En mode graphique, `F12` écrit `sim/out/keyboard-capture.png`.

## CLI JSON Lines

```powershell
.\sim\build\windows-ucrt\brick-ui-sim.exe --headless --stdio
```

`stdout` ne contient que les réponses JSON; les diagnostics vont sur `stderr`.
Les réponses sont `{"ok":true,"result":...}` ou
`{"ok":false,"error":{"code":"...","message":"..."}}`.

Commandes : `discover`, `inspect`, `advance`, `press`, `release`, `tap`, `hold`,
`combo`, `double_tap`, `rotate`, `push_encoder`, `wait_until`, `capture`, `quit`.
`discover` retourne les 44 boutons, les quatre encodeurs, les pages et champs
d'inspection. Les durées sont en millisecondes virtuelles.

Séquence testée :

```jsonl
{"id":1,"cmd":"tap","button":"PARAM_2"}
{"id":2,"cmd":"tap","button":"PAGE_2"}
{"id":3,"cmd":"rotate","encoder":"A","delta":2}
{"id":4,"cmd":"inspect"}
{"id":5,"cmd":"capture","path":"sim/out/tone-page-2.png"}
{"id":6,"cmd":"tap","button":"SETTINGS"}
{"id":7,"cmd":"tap","button":"PAGE_1"}
{"id":8,"cmd":"wait_until","page":"TONE","subpage":1,"timeout_ms":50}
{"id":9,"cmd":"quit"}
```

`press` et `release` peuvent être séparés pour les maintiens et accords précis.
`tap` accepte `duration_ms`; `double_tap` accepte `duration_ms` et `interval_ms`;
`combo` reçoit `buttons`; `rotate` reçoit `encoder` et un `delta` signé.

## Limites de la passe 2

Les pages CFG, ENV, TONE, MOD, MIX, PLAY et SETTINGS, leurs sous-pages, les
encodeurs et les overlays accessibles sont issus du firmware. Les browsers et leurs
catalogues sont réservés à la passe 3. Les états audio/séquenceur sont déterministes
et minimaux; les champs privés sans API sont signalés `available:false` par `inspect`.
