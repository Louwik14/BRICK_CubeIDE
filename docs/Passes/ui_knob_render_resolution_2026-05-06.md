# UI knob render resolution - 2026-05-06

## Cause des 7 positions

- Autorite widget: `Src/UI/ui_widgets.c`, fonction `uiw_draw_knob`.
- Autorite valeur affichee: `Src/UI/ui_renderer_template.c`, via `ui_renderer_template_get_param_display_value()`.
  - Param global ou `TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED`: `param_get(id)`.
  - Param track-scoped: miroir UI actif `param_store_get_active(id)`.
- Mapping precedent: le renderer convertissait `value/min/max` en entiers `x10`, puis `uiw_draw_knob()` ramenait toute la plage sur `0..6`.
- Rendu precedent: dessin vectoriel local, pas bitmap/sprite; corps octogonal en 8 traits et indicateur choisi dans 7 couples `(x,y)`.

## Nouvelle methode

- `uiw_draw_knob()` recoit maintenant directement `float value, vmin, vmax`.
- La valeur parametre reste inchangee; seul le mapping graphique normalise `0..1` est utilise.
- Le corps est un contour 16 segments plus rond, toujours via `drv_display_draw_line()`.
- L'indicateur utilise 33 positions sur un arc bas-gauche -> haut -> bas-droite:
  - min: gauche/bas,
  - milieu: haut,
  - max: droite/bas.
- Le centre est marque par un petit carre 3x3 pour garder une lecture stable sur OLED 128x64.

## Fichiers touches

- `Src/UI/ui_widgets.c`
- `Inc/UI/ui_widgets.h`
- `Src/UI/ui_renderer_template.c`

## Limites restantes

- Le backend expose des primitives pixel/ligne entieres; l'indicateur reste donc quantifie par la grille OLED.
- Pas d'ajout de trigonometrie runtime ni de primitive driver nouvelle pour limiter le cout CPU et le perimetre UI.
- Les widgets enum/switch/icons ne sont pas modifies.

## Statut check

- Valeurs parametres, plages, steps, edition encodeur, `PARAM_COUNT`, stockage et audio non modifies.
- Min/mid/max: mapping graphique borne et non inverse.
- Depassement: contour rayon 7 et indicateur rayon 6 restent dans le frame template 31x37.
- `git diff --check`: OK.
- Build complet non lance.
