# BRICK UI Simulator — passe 1

Ce projet Windows isolé compile directement le framebuffer, U8g2, le renderer OLED,
le gestionnaire de pages et la page BRICK originale `NAME_EDIT`. Cette page a été
retenue plutôt que CFG pour limiter la première fermeture de dépendances : son rendu
reste intégralement celui du firmware, avec ses polices et primitives réelles.

## Prérequis

- Windows 10
- CMake et Ninja dans `PATH`
- GCC UCRT64 dans `C:\msys64\ucrt64\bin`
- paquet MSYS2 `mingw-w64-ucrt-x86_64-sdl3`

## Compiler et lancer

Depuis la racine BRICK :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\run.ps1
```

`-ExecutionPolicy Bypass` est nécessaire uniquement lorsque la stratégie locale
interdit l'exécution de scripts non signés.

L'échelle par défaut est ×8. Elle peut être changée avec
`.\sim\run.ps1 --scale 6`. SDL utilise une texture 128 × 64 avec filtrage nearest.

## Capture ponctuelle

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\run.ps1 --capture sim/out/first-screen.png
```

Le PNG provient directement du framebuffer de 1 024 octets et mesure 128 × 64.
Les sorties sont limitées à `sim/out`, `sim/build` et `sim/dist`.

## Session CLI JSON Lines

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\run.ps1 --headless --stdio
```

Une requête JSON est lue par ligne. Une réponse JSON est écrite par ligne sur
`stdout`; les diagnostics utilisent `stderr`. Un `id` JSON éventuel est recopié.

```jsonl
{"id":1,"cmd":"discover"}
{"id":2,"cmd":"inspect"}
{"id":3,"cmd":"advance","ms":100}
{"id":4,"cmd":"capture","path":"sim/out/first-screen.png"}
{"id":5,"cmd":"quit"}
```

Commandes de cette passe : `discover`, `inspect`, `advance`, `capture`, `quit`.
Les réponses ont la forme `{"ok":true,"result":...}` ou
`{"ok":false,"error":{"code":"...","message":"..."}}`.

## Limites

La passe 1 ne simule ni boutons, ni encodeurs, ni navigation, ni browsers. La page
initiale est fixe. Aucun DSP, stockage, MIDI ou séquenceur n'est compilé.
