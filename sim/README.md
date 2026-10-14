# BRICK UI Simulator

Le simulateur Windows compile le pipeline UI BRICK original (entrées, navigation,
pages, renderer OLED, U8g2 et framebuffer 128 × 64). SDL3 ne fait que présenter
le framebuffer. Aucun DSP, MIDI, stockage réel ou asset du dépôt n'est modifié.

## Compiler et lancer

Prérequis : Windows 10, CMake, Ninja, GCC UCRT64 dans
`C:\msys64\ucrt64\bin`, et le paquet MSYS2
`mingw-w64-ucrt-x86_64-sdl3`.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\sim\run.ps1
```

La compilation valide aussi `config/keymap.json` avec l'exécutable qui vient
d'être produit. Une action inconnue ou un conflit de scancode fait échouer le
build au lieu de provoquer une erreur au prochain lancement graphique.

La fenêtre est agrandie ×8 sans interpolation (`--scale 6` pour changer).
Le mapping AZERTY est documenté dans `KEYBOARD.md` et défini dans
`config/keymap.json`.

## CLI JSON Lines

```powershell
.\sim\build\windows-ucrt\brick-ui-sim.exe --headless --stdio
```

Une requête et une réponse JSON par ligne. `stdout` est réservé au protocole;
les diagnostics utilisent `stderr`. Les commandes sont : `discover`, `inspect`,
`advance`, `press`, `release`, `tap`, `hold`, `combo`, `double_tap`, `rotate`,
`push_encoder`, `wait_until`, `capture` et `quit`.

`inspect` expose la page, la sous-page, la piste, le transport, les touches
maintenues et les quatre paramètres réellement publiés par la page (identifiant,
libellé et valeur). Les états privés de Settings restent explicitement
indisponibles. Exemple :

```jsonl
{"cmd":"tap","button":"PARAM_2"}
{"cmd":"rotate","encoder":"A","delta":-8}
{"cmd":"inspect"}
{"cmd":"capture","path":"sim/out/tone.png"}
{"cmd":"quit"}
```

Les captures sont toujours issues du framebuffer BRICK, en PNG natif 128 × 64.

## Scénarios documentaires

Un scénario est un fichier JSONL composé uniquement des commandes CLI. Il utilise
donc les vraies entrées BRICK et échoue au premier ordre invalide ou au premier
`wait_until` expiré.

```powershell
.\sim\build\windows-ucrt\brick-ui-sim.exe --headless --scenario sim/scenarios/manual-core.jsonl
.\sim\build\windows-ucrt\brick-ui-sim.exe --headless --scenario sim/scenarios/manual-browsers.jsonl
.\sim\build\windows-ucrt\brick-ui-sim.exe --headless --scenario sim/scenarios/manual-patch-name.jsonl
```

Ces scénarios produisent sous `sim/out/` les pages CFG, TONE, MOD, le Sample
Browser, le Patch Browser, le Project Browser et le Name Editor. Un code de
sortie non nul signale un échec.

## Simulation et limites

Les paramètres TONE, ENV et MOD sont conservés en mémoire et relus par le vrai
Param Registry et le renderer. Le catalogue WAV fictif contient dossiers,
sous-dossiers, noms courts et longs; les projets et patches fictifs sont recréés
au démarrage. Le Sample Browser, le Project Browser, le Patch Browser et le Name
Editor réels sont rendus et navigables.

Cette passe reste partielle : les opérations complètes LOAD/SAVE/DELETE/RENAME
des assets Sample/Multi/Wavetable ne sont pas toutes raccordées, les browsers
Multi/Wavetable reposant sur FatFs ne disposent pas encore d'un catalogue hôte,
et les détails privés de sélection/confirmation de Settings ne sont pas exposés
par `inspect`. Aucun état miroir n'est créé pour masquer ces limites.
