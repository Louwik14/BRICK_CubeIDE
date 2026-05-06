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

## Contextualisation valeurs MacroFX 2026-05-06
- Autorite label visible: `Src/UI/pages/ui_page_template_tone.c`, via `ui_page_template_tone_param_text()` sur les slots `LVL/A/B` Master/FX.
- Autorite valeur affichee: meme couche UI TONE, avec lecture pure `param_registry_get_track_value()` de `TYPE` et de la macro concernee.
- Autorite mapping sonore: `Src/Audio/fx_master_macro.c`; le formatage UI suit les helpers DSP existants sans write runtime, sans migration, sans changement encodeur, sans changement `PARAM_COUNT`.
- `Src/UI/ui_renderer_template.c` autorise maintenant un override de nom/valeur dans le pipeline parametre standard; les tracks hors Master/FX gardent le chemin catalogue standard.

Table finale label/value:
- `LVL`: `0..100%` pour tous les types FX, stockage interne conserve en `0..127`.
- `OFF`: `---=---`, `---=---`.
- `DRIVE`: `TONE=-64..+63`, `SHAPE=SOFT/CLIP/HARD/FOLD`.
- `CRUSH`: `BITS=16..4bit`, `RATE=1x..96x`.
- `PUMP`: `RATE=4/1..1/16`, `REL=0..100%`.
- `CHOP`: `RATE=4/1..1/16`, `SHAPE=SOFT/GATE/HARD`.
- `ECHO`: `TIME=1/8..2/1`, `FB=0..74%`.
- `WOBBLE`: `RATE=0.1..7.6Hz`, `DEPTH=0..100%`.
- `COMB`: `TUNE=90..4090Hz approx visible`, `FB=0..80%`.
- `RING`: `FREQ=1Hz..1.5kHz approx visible`, `COLOR=SIN/TRI/SQR/DIRT`.
- `PITCH`: `SEMI=-12..+12st`, `FINE=-100..+100ct`.
- `TALK`: `VOWL=A/E/I/O/U`, `TONE=0..100%`.
- `STUTTER`: `SIZE=1/32..3/4`, `RATE=0.5x/0.75x/1x/1.5x/2x/3x/4x/6x`.
- `FREEZE`: `TIME=1/8..2/1`, `HOLD=SHORT/MID/LONG/INF`.

Ecarts vs suggestions:
- `CRUSH BITS` affiche `16..4bit`, car le DSP clamp le bit depth effectif a 4 bits minimum.
- `CRUSH RATE` affiche le hold effectif `1x..96x`, car le DSP utilise une sample-hold quadratique continue, pas une liste stricte `1x..32x`.
- `RING COLOR` expose maintenant `DIRT`: le DSP morphe `SIN/TRI/SQR/DIRT`, sans sync mesure.
- `RATE` de `CHOP/PUMP` affiche les divisions issues du tableau DSP `0.25..16 cycles/beat`; la suggestion `1/2..1/64` ne correspond pas au code actuel.
- `ECHO/FREEZE TIME` affiche les divisions du tableau DSP `1/8..2/1`; la duree effective reste ensuite clampée par le DSP selon BPM.
- `PITCH FINE` affiche `-100..+100ct`, car le DSP mappe actuellement la macro fine sur +/-100 cents.
- `COMB TUNE` affiche une approximation lineaire compacte pour rester lisible; le DSP conserve son mapping exponentiel exact.

Fichiers touches:
- `Src/UI/pages/ui_page_template_tone.c`
- `Src/UI/ui_renderer_template.c`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/Passes/master_fx_ui_contract_2026-05-06.md`

Statut check:
- `git diff --check`: OK.
- Build complet non lance, conformement a la contrainte de passe.

## Correction regression template MacroFX 2026-05-06
- Cause de regression visuelle: `Src/UI/ui_renderer_template.c` utilisait `virtual_slot_text` sur des slots parametres reels Master/FX et retournait avant le pipeline standard; cela activait le rendu texte special (`uiw_draw_enum_text`) au lieu du potard normal.
- Correction appliquee: `virtual_slot_text` est de nouveau limite aux slots vides `PARAM_COUNT`; un hook texte parametre standard `param_text` injecte seulement le nom et la valeur affichee avant le rendu habituel.
- Confirmation template: `LVL/A/B` Master/FX restent des params reels, conservent le widget potard standard, le layout, le fond, la selection et la navigation existants. `FXn` garde le champ enum de type FX.
- DSP, ROUT, stockage et `PARAM_COUNT`: inchanges.
- Fichiers touches: `Inc/UI/ui_template_page.h`, `Src/UI/ui_renderer_template.c`, `Src/UI/pages/ui_page_template_tone.c`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_ui_contract_2026-05-06.md`.
- Statut check: `git diff --check` OK; build complet non lance.

## Validation anti-rustine 2026-05-06
- Validation anti-rustine OK: le renderer template ne connait ni `Master/FX` ni les types MacroFX; il appelle seulement le hook generique `param_text` avant le rendu parametre standard.
- La logique label/value Master/FX reste confinee a `Src/UI/pages/ui_page_template_tone.c`, avec garde `family=Master`, `type=Master/FX`, subpage active et param courant.
- Les valeurs utilisent l'enum d'autorite `fx_master_macro_type_t` via `Audio/fx_master_macro.h`; pas de nouveau changement DSP, ROUT, stockage ou `PARAM_COUNT`.
- `git diff --check`: OK; build complet non lance.

## Correction edition discrete MacroFX 2026-05-06
- Cause du comportement chelou: les macros Master/FX `A/B` restent des params `0..127` a `step=1`; les labels contextualises affichaient des domaines discrets sans que l'encodeur canonise la valeur vers ces domaines.
- Correction appliquee: `ui_param` garde le chemin d'edition standard mais intercepte uniquement les params `Master/FX` macro `A/B`, apres garde `family=Master` + `type=Master/FX`, pour convertir `step discret <-> raw 0..127`.
- Strategie raw: index courant = arrondi de `raw * (N-1) / 127`; delta encodeur ajoute ou retire des index; raw stocke = arrondi de `index * 127 / (N-1)`. Le DSP recoit donc une valeur canonique stable sans changement de mapping audio.
- Continus conserves: `LVL`, `DRIVE TONE`, `CRUSH RATE`, `RING FREQ`, `PUMP REL`, `COMB TUNE`, `COMB FB`, `WOBBLE RATE`, `WOBBLE DEPTH`, `ECHO FB`, `TALK TONE`, `PITCH FINE`.
- Parametres quantifies: `DRIVE SHAPE` 4, `CRUSH BITS` 13, `RING COLOR` 4, `CHOP RATE` 10, `CHOP SHAPE` 3, `PUMP RATE` 10, `ECHO TIME` 8, `FREEZE TIME` 8, `FREEZE HOLD` 4, `STUTTER SIZE` 8, `STUTTER RATE` 8, `TALK VOWL` 5, `PITCH SEMI` 25.
- Ecarts vs suggestions: `CHOP/PUMP RATE` suivent les 10 divisions du tableau DSP; `CRUSH RATE` reste continu car le DSP derive un hold `1..96x` depuis une courbe continue; `CRUSH BITS` est quantifie car le DSP arrondit effectivement vers 13 bit-depths `16..4`.

## Audit edition/temps MacroFX 2026-05-06
- Autorite edition encodeur: `Src/UI/ui_param.c`; `ui_param_master_fx_quantize_edit()` intercepte seulement les macros `A/B` Master/FX discretes, convertit `raw 0..127 -> step -> raw 0..127`, puis repasse par `param_registry_apply_track_edit`.
- Autorite label/value: `Src/UI/pages/ui_page_template_tone.c`; le renderer template reste generique et ne contient pas de logique Master/FX.
- Canonique: stockage `0..127` conserve; les valeurs discretes sont canonisees sur les raws arrondis correspondant aux steps DSP/UI.
- Continus confirmes: `LVL`, `DRIVE TONE`, `CRUSH RATE`, `RING FREQ`, `PUMP REL`, `COMB TUNE`, `COMB FB`, `WOBBLE RATE`, `WOBBLE DEPTH`, `ECHO FB`, `TALK TONE`, `PITCH FINE`.
- Discrets confirmes: `DRIVE SHAPE` 4, `CRUSH BITS` 13, `RING COLOR` 4, `CHOP RATE` 10, `CHOP SHAPE` 3, `PUMP RATE` 10, `ECHO TIME` 8, `FREEZE TIME` 8, `FREEZE HOLD` 4, `STUTTER SIZE` 8, `STUTTER RATE` 8, `TALK VOWL` 5, `PITCH SEMI` 25.
- `CRUSH BITS`: discret car le DSP arrondit vers `16..4bit`; `CRUSH RATE`: continu volontaire car le DSP calcule un sample-hold quadratique `1..96x`.
- Coherence DSP ajoutee: `DRIVE SHAPE`, `RING COLOR`, `TALK VOWL` et `PITCH SEMI` sont aussi re-quantifies cote DSP pour que les raws canonises par l'UI correspondent a des positions sonores nettes.
- Fichiers touches: `Src/UI/ui_param.c`, `docs/architecture/z5_ui_navigation_interaction.md`, `docs/Passes/master_fx_ui_contract_2026-05-06.md`.
- Statut check: `git diff --check` OK; build complet non lance.

## Validation quantification UI/DSP 2026-05-06
- Resultat: aucune divergence locale bloquante trouvee; pas de patch code applique dans cette validation.
- Autorite edition: `Src/UI/ui_param.c` uniquement. La quantification n'est pas codee dans `Src/UI/ui_renderer_template.c`; le renderer appelle seulement le hook generique `param_text` et garde le widget potard standard.
- Centralisation: la decision discret/continu est centralisee cote edition dans `ui_param_master_fx_discrete_count()`. Les labels restent dans la page TONE et les mappings DSP restent dans `fx_master_macro.c`; cette duplication est bornee car les formules d'index sont identiques (`round(raw * (N-1) / 127)`) et les listes ont ete auditees.
- Verification raw: les counts `4/5/8/10/13/25` convertissent `step -> raw -> label index` et `step -> raw -> DSP index` sans ecart.

Table finale continu/discret confirmee:
- Continus: `LVL`, `DRIVE TONE`, `CRUSH RATE`, `RING FREQ`, `PUMP REL`, `COMB TUNE`, `COMB FB`, `WOBBLE RATE`, `WOBBLE DEPTH`, `ECHO FB`, `TALK TONE`, `PITCH FINE`.
- Discrets: `DRIVE SHAPE` 4, `CRUSH BITS` 13, `RING COLOR` 4, `CHOP RATE` 10, `CHOP SHAPE` 3, `PUMP RATE` 10, `ECHO TIME` 8, `FREEZE TIME` 8, `FREEZE HOLD` 4, `STUTTER SIZE` 8, `STUTTER RATE` 8, `TALK VOWL` 5, `PITCH SEMI` 25.

Points audites:
- `DRIVE SHAPE`: labels `SOFT/CLIP/HARD/FOLD`, edition 4 steps, DSP `shape_idx=round(B*3)`.
- `RING COLOR`: labels `SIN/TRI/SQR/DIRT`, edition 4 steps, DSP `color_idx=round(B*3)`.
- `TALK VOWL`: labels `A/E/I/O/U`, edition 5 steps, DSP `vowel=round(A*4)/4`.
- `PITCH SEMI`: labels `-12..+12st`, edition 25 steps, DSP `semi_step=round(A*24)-12`.
- `CRUSH BITS`: labels `16..4bit`, edition 13 steps, DSP `bits=16-round(A*12)` avec minimum effectif 4 bits.
- `CRUSH RATE`: continu conserve, affichage `1x..96x`, DSP sample-hold `1 + B^2 * 95`.
- Rythmiques: `CHOP/PUMP RATE` utilisent les 10 divisions du tableau DSP `0.25..16 cycles/beat`; `ECHO/FREEZE TIME` utilisent les 8 divisions `1/8..2/1`; `STUTTER SIZE/RATE` utilisent les tableaux DSP 8 positions.

Garde-fous verifies:
- Tracks non `Master/FX`: pas d'interception, chemin encodeur standard conserve.
- ROUT/routing audio reel: aucune modification.
- `PARAM_COUNT`: aucun changement dans cette validation.
- `git diff --check`: OK, hors warnings CRLF/LF existants.

Risques restants:
- Les tables labels/edition/DSP restent dans trois fichiers differents; elles sont petites et auditees, mais une future extension MacroFX devra les maintenir ensemble.
- Pas de build complet lance conformement a la contrainte de passe.
