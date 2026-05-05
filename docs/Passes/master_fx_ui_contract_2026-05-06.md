# Passe - MASTER FX UI contract

## Contrat ajoute
- Nouvelle track `Master/FX` exposee en `CFG`.
- Scope: UI, stockage de valeurs et contrat runtime track-aware uniquement.
- Statut DSP: aucun traitement audio actif, aucun routing audio reel modifie.

## Pages TONE
- Page 1 `FX1`: `FX1`, `LVL`, `A`, `B`
- Page 2 `FX2`: `FX2`, `LVL`, `A`, `B`
- Page 3 `FX3`: `FX3`, `LVL`, `A`, `B`
- Page 4 `FX4`: `FX4`, `LVL`, `A`, `B`

`LVL` est commun a chaque slot comme intensite/impact stocke, non cable DSP.

## Types FX et macros
- `OFF`: `---` / `---`
- `DRIVE`: `TONE` / `SHAPE`
- `CRUSH`: `BITS` / `RATE`
- `PUMP`: `RATE` / `REL`
- `CHOP`: `RATE` / `SHAPE`
- `ECHO`: `TIME` / `FB`
- `WOBBLE`: `RATE` / `DEPTH`
- `COMB`: `TUNE` / `FB`
- `RING`: `FREQ` / `COLOR`
- `PITCH`: `SEMI` / `FINE`
- `TALK`: `VOWL` / `TONE`
- `STUTTER`: `SIZE` / `RATE`
- `FREEZE`: `TIME` / `HOLD`

Liste enum exposee: `OFF`, `DRIVE`, `CRUSH`, `PUMP`, `CHOP`, `ECHO`, `WOBBLE`, `COMB`, `RING`, `PITCH`, `TALK`, `STUTTER`, `FREEZE`.

## ROUT
- Pour `Master/FX`, le raw mode `ARP` est projete visuellement en `ROUT`.
- L'etat ROUT ajoute est local UI-only et sert a preparer la selection pass/bypass par track.
- Aucun branchement audio n'est effectue dans cette passe.

## Fichiers touches
- `Inc/UI/ui_core.h`
- `Src/UI/ui_track_catalog.c`
- `Inc/Core/track_runtime.h`
- `Src/Core/track_runtime.c`
- `Inc/Core/track_tone_sound_state.h`
- `Src/Core/track_tone_sound_state.c`
- `Inc/Param/param_store.h`
- `Src/Param/param_registry_catalog.c`
- `Src/Param/param_registry.c`
- `Inc/Param/param_registry_backends.h`
- `Src/Param/param_registry_backends.c`
- `Src/Param/param_registry_tone_backends.c`
- `Src/UI/pages/ui_page_template_tone.c`
- `Src/UI/ui_hall_mode_projection.c`
- `Src/UI/ui_core_runtime_bridge.c`
- `docs/architecture/ARCHITECTURE_GLOBAL.md`
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/architecture/z6_state_persistence_patterns_projects.md`
- `readme.md`

## Prochaines passes recommandees
- Decider si `Master/Buffer` et `Master/FX` doivent coexister ou rester exclusifs dans le catalogue Master.
- Promouvoir l'etat ROUT Master/FX vers une autorite projet/pattern si le pass/bypass doit persister.
- Brancher le routing audio reel en Z1 seulement apres validation de l'autorite ROUT et du budget hard-RT.
- Ajouter le backend DSP MacroFX master sans ajouter FILTER ni REVERB a la grammaire MacroFX.

## Validation 2026-05-06
- Build non lance: environnement marque non compatible avec les builds.
- Verification locale lancee: `git diff --check`, OK hors warnings de fins de ligne CRLF existants dans les fichiers modifies.
- Bugs trouves et corriges: liste MacroFX `REVERSE` remplacee par `FREEZE`; labels dynamiques corriges pour `OFF`, `PUMP`, `RING` et `FREEZE`.
- Confirmation: aucun `REVERSE`, `FILTER` ou `REVERB` n'est expose dans la grammaire Master/FX MacroFX.

## Audit cible 2026-05-06
- Resultat: aucune anomalie bloquante trouvee dans le contrat UI Master/FX apres remplacement `REVERSE` -> `FREEZE`.
- Liste MacroFX verifiee comme unique et coherente dans le catalogue param, la page TONE et les docs de contrat: `OFF`, `DRIVE`, `CRUSH`, `PUMP`, `CHOP`, `ECHO`, `WOBBLE`, `COMB`, `RING`, `PITCH`, `TALK`, `STUTTER`, `FREEZE`.
- Labels dynamiques A/B verifies: `OFF ---/---`, `DRIVE TONE/SHAPE`, `CRUSH BITS/RATE`, `PUMP RATE/REL`, `CHOP RATE/SHAPE`, `ECHO TIME/FB`, `WOBBLE RATE/DEPTH`, `COMB TUNE/FB`, `RING FREQ/COLOR`, `PITCH SEMI/FINE`, `TALK VOWL/TONE`, `STUTTER SIZE/RATE`, `FREEZE TIME/HOLD`.
- Stockage verifie: chaque page `FX1..FX4` pointe vers son param `TYPE`, puis `LVL`, `A`, `B`; le changement de type ne reset pas `LVL/A/B`.
- Exposition ensembles verifiee: `PLAY` reste masque pour `Master/FX`; les autres ensembles prevus restent disponibles via le masque runtime.
- ROUT verifie: pour `Master/FX`, `ARP` est projete en `ROUT` et ne touche qu'un etat UI-only local; aucun appel de routing audio reel n'est ajoute pour `Master/FX`.
- Anomalies corrigees pendant cet audit: aucune.

## Risques notes
- `PARAM_COUNT` augmente: les layouts binaires de snapshots/projets contenant des tableaux `PARAM_COUNT` changent.
- Compat anciennes donnees: les nouveaux params et le nouveau type sont ajoutes en fin d'enum; les donnees anterieures restent lisibles tant que le loader accepte les layouts plus courts. Le risque porte sur les snapshots/projets crees avec ce nouveau `PARAM_COUNT` et relus par un firmware plus ancien.
- ROUT Master/FX reste local UI-only, non persiste et non cable au routing audio reel.
- Prochaine passe recommandee: formaliser une autorite persistable pour ROUT Master/FX avant tout cablage audio/DSP.

## Correction LED ROUT 2026-05-06
- Cause trouvee: le label passait par `effective_view=ROUT`, mais le rendu LED testait encore le mode brut `ARP`; hors cas `Master/Buffer`, il retombait donc sur le renderer clavier/ARP.
- Decision d'autorite: `ui_hall_mode_projection` expose maintenant une cle explicite `ui_hall_mode_resolve_rout_context(track, raw_mode)`, separee du simple label visible.
- Difference de contextes:
  - `Master/Buffer ROUT`: conserve le renderer et l'etat existants `brick6_master_buffer_get_source_enabled`, avec action qui modifie uniquement le routing buffer existant.
  - `Master/FX ROUT`: utilise l'etat UI-only local `g_master_fx_route_enabled[]`, expose par le runtime bridge pour les LEDs; aucun routing audio reel n'est modifie.
- Fichiers touches: `Inc/UI/ui_hall_mode_projection.h`, `Src/UI/ui_hall_mode_projection.c`, `Inc/UI/ui_core_runtime_bridge.h`, `Src/UI/ui_core_runtime_bridge.c`, `Drivers/Drv_app/Src/led_rgb.c`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_ui_contract_2026-05-06.md`.
- Statut check/build: build non lance selon contrainte environnement; `git diff --check` lance, OK hors warnings CRLF.

## Correction warning build LED ROUT 2026-05-06
- Cause warning: `led_rgb.c` appelait `ui_core_runtime_bridge_get_master_fx_route_enabled()` sans inclure le header public du runtime bridge.
- Correction appliquee: inclusion de `UI/ui_core_runtime_bridge.h` dans `Drivers/Drv_app/Src/led_rgb.c`; le prototype reste expose dans `Inc/UI/ui_core_runtime_bridge.h` avec la meme signature que la definition (`uint8_t ui_core_runtime_bridge_get_master_fx_route_enabled(uint8_t track)`).
- Fichiers touches: `Drivers/Drv_app/Src/led_rgb.c`, `docs/Passes/master_fx_ui_contract_2026-05-06.md`.
- Statut check/build: build non lance selon contrainte environnement; `git diff --check` lance, OK hors warnings CRLF.

## Correction ergonomie ROUT destination 2026-05-06
- Contrat LED/action ajoute: dans `ROUT Master/Buffer` et `ROUT Master/FX`, le hall de la track master courante est affiche en vert fonce, informatif, fixe, non selectionnable et non deselectionnable.
- Autorites conservees: le rendu LED reste dans `led_rgb.c`; les toggles ROUT restent dans `ui_core_runtime_bridge_handle_master_buffer_routing_event`; les contextes `Master/Buffer` et `Master/FX` restent separes par `ui_hall_mode_resolve_rout_context`.
- Comportement: le toggle sur `hall == active_track` est consomme et ignore pour les deux contextes ROUT; les autres tracks restent toggleables selon leur contexte. Aucun routing audio reel ni DSP n'est modifie.
- Fichiers touches: `Drivers/Drv_app/Src/led_rgb.c`, `Src/UI/ui_core_runtime_bridge.c`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_ui_contract_2026-05-06.md`.
- Statut check/build: build non lance selon contrainte environnement; `git diff --check` lance, OK hors warnings CRLF.
