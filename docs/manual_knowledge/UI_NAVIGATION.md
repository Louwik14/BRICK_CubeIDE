# Cartographie UI

## Pages enregistrees

19 pages sont effectivement enregistrees au boot. Les IDs 0 et 1 sont des trous reserves et ne sont pas des pages.

| ID | Identifiant | Nom/fonction | Acces/sortie |
|---:|---|---|---|
| 2 | `UI_PAGE_CALIBRATION` | calibration usine | Settings/flux dedie ; retourne a l'appelant |
| 3 | `UI_PAGE_USER_CALIBRATION` | calibration Hall utilisateur | Settings ou Keyboard ; retourne a l'appelant |
| 4 | `UI_PAGE_TEMPLATE_ENV` | ENV | PARAM 1 ; PARAM/PAGE ou autre ensemble |
| 5 | `UI_PAGE_TEMPLATE_CFG` | CFG | fallback, PARAM 6 depuis ENV, contexte track |
| 6 | `UI_PAGE_TEMPLATE_REC_CFG` | REC CFG | chord Audio REC ; retour modal |
| 7 | `UI_PAGE_TEMPLATE_TONE` | TONE | PARAM 2 |
| 8 | `UI_PAGE_TEMPLATE_MOD` | MOD | PARAM 3 |
| 9 | `UI_PAGE_TEMPLATE_KEYBOARD` | KEYBOARD | mode Hall KBD |
| 10 | `UI_PAGE_MIDI_FX` | MIDI FX | SHIFT + Hall B1 / alternance FX |
| 11 | `UI_PAGE_TEMPLATE_SEQ` | SEQ | contexte sequence |
| 12 | `UI_PAGE_TEMPLATE_MIX` | MIX | PARAM 4 |
| 13 | `UI_PAGE_TEMPLATE_PLAY` | PLAY | PARAM 5 |
| 15 | `UI_PAGE_AUDIO_FX` | FX audio | SHIFT + Hall B1 / alternance FX |
| 16 | `UI_PAGE_AUDIO_REC` | capture audio | chord/mode Audio REC ; navigation ensemble ferme la modale |
| 17 | `UI_PAGE_REC_EDIT` | trim de take | PAGE 4 depuis REC lorsqu'un take existe |
| 18 | `UI_PAGE_PATCH_ASSIGN` | browser Patch | mode PATCH ; RETURN |
| 19 | `UI_PAGE_NAME_EDIT` | editeur de nom modal | Save/Rename ; Confirm/Cancel |
| 20 | `UI_PAGE_SETTINGS` | Settings/browser assets/projects | SETTINGS/back |
| 21 | `UI_PAGE_THEME_BROWSER` | themes | Settings > Theme ; RETURN/LOAD |

Sources : `Inc/UI/ui_page_manager.h`, `Src/UI/ui_bootstrap.c::ui_bootstrap_register_pages`, `Src/UI/ui_navigation.c`.

## Ensembles et sous-pages

La page d'ensemble est conditionnee par la topologie et le moteur de la lane. Chaque couple page/sous-ensemble memorise sa derniere sous-page. Une page indisponible retombe sur CFG. Le changement de piste resynchronise contexte, owner et disponibilite sans reinitialiser arbitrairement toutes les sous-pages.

| Ensemble | Sous-pages principales |
|---|---|
| ENV | FILTER, ADSR, VCA, ENV 3 ; External : MODE ; master GROUP : FILTER et ENV 3 |
| CFG | TRACK, MIDI ; REC CFG : MAIN, LEN |
| TONE | depend du moteur, voir `PARAMETERS.md` |
| MOD | MATRIX, LFO 1, LFO 2, LFO 3 ; second subset MULTI, SLEW |
| KEYBOARD | PLAY, MODE, VELOCITY |
| MIDI FX | GENERATOR, VOICER, SCALER, TRIG |
| FX audio | FX A, FX B, ROUTING/SPATIAL ; child GROUP : LOCAL SENDS |
| SEQ | COMMON, GROOVE 1, GROOVE 2 |
| MIX | MIX, SEND3 |
| PLAY | V1..V4 puis V5..V8 |

Sources : initialisateurs `ui_template_family_t` dans `Src/UI/pages/ui_page_template_*.c` et `Src/UI/pages/ui_page_midi_fx.c`.

## Modales et browsers

- Settings racine : SAMPLE, PROJECT, CALIBRATION, THEME, CPU LOAD.
- SAMPLE : MULTI, WAVE, STREAM ; racines SD `0:/Multi`, `0:/WAVETABLES`, `0:/Samples`.
- PROJECT : slot virtuel `[ + NEW PROJECT ]` puis jusqu'a 16 slots. Footer commun RETURN/LOAD/SAVE et SHIFT RENAME/DELETE.
- PATCH : slot virtuel `[ + NEW PATCH ]`, catalogues ALL/FOCUS/SYN/SMP/DRM. Footer RETURN/LOAD/SAVE/PREVIEW ; SHIFT `-/RENAME/DELETE/-`. PREVIEW est momentane jusqu'au relachement PAGE 4.
- Name Edit : PAGE 1 gauche, PAGE 2 droite, PAGE 3 caractere/operation, PAGE 4 confirmation/annulation selon contexte ; SHIFT inhibe ces actions normales.
- Theme : PAGE 1 RETURN, PAGE 2 LOAD.

Sources : `Src/UI/pages/Settings/ui_settings_navigation.inc`, `Inc/UI/ui_browser_actions.h`, `Src/UI/pages/ui_page_patch_assign.c`, `Src/UI/pages/ui_page_name_edit.c`, `Src/UI/pages/ui_page_theme_browser.c`.
