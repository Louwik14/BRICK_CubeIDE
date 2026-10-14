# SYS-01 — Reglages, themes, calibration et diagnostics

## Statut

CONFIRME pour les entrees visibles ; PARTIEL pour l'exhaustivite hardware.

SETTINGS racine contient SAMPLE, PROJECT, CALIBRATION, THEME et CPU LOAD. Theme ouvre un browser RETURN/LOAD et persiste l'identite de theme ; CPU LOAD bascule l'affichage. Calibration propose HALL KBD et HALL VEL, avec pages usine/utilisateur et retour memorise. La calibration globale est stockee dans `0:/BRICK/HALL.B6C`; absence/invalidation conserve le besoin de calibration.

Le boot affiche une progression/diagnostic AUDIO. Une bibliotheque crash ecrit des capsules best-effort sur SD. Le shutdown volontaire fige l'ingress, reconcilie le Pattern actif et ecrit un Resume double-slot dans `BRICK/RESUME` sans modifier le Project saved.

Sources : `Src/UI/pages/Settings/*`, `ui_page_theme_browser.c`, `ui_page_calibration.c`, `Src/UI/ui_theme.c`, `Src/App/power_shutdown.c`, `Src/Platform/crash_library.c`, docs architecture associees.
