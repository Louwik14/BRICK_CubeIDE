# ASSET-01 — Bibliotheques Sample, Wavetable et Multi

## Statut

PARTIEL — logique confirmee, supports/formats a valider physiquement.

## Acces et commandes

SETTINGS -> SAMPLE -> STREAM/WAVE/MULTI. Les browsers enumerent `0:/Samples`, `0:/WAVETABLES`, `0:/Multi`, gerent dossiers et parent, focus Library/Slots, preview, load/replace, clear/unload, rename/delete selon contexte et copie gauche/droite avec variante SHIFT.

STREAM accepte LOAD/REPLACE FROM SD, PREVIEW/STOP, CLEAR. Un fichier deja charge est detecte. Les WAV non supportes/48 kHz requis peuvent proposer une conversion cooperative. Wavetable et Multi ont leurs pipelines dedies ; Multi affiche scan/conversion/index/preparation.

## Limites et erreurs

SD absente/fault, path trop long, catalogue tronque, pool plein, transport ou recorder actifs, scheduler busy et erreur format sont exposes par statuts. Les mutations dangereuses demandent confirmation. Preview prend le gate SD et peut finir, echouer ou etre arretee explicitement.

## Sources

`Src/UI/pages/Settings/ui_settings_{sample,multi}_assets.inc`, `ui_settings_catalog_browser.inc`, `ui_settings_asset_ops.inc`, `Src/Storage/{wav_loader,wav_convert,sd_preview}.c`, `Src/Sampler/multi_sample_loader.c`, `wavetable_pool.c`.
