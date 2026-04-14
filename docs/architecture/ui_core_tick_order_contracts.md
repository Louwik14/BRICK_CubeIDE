# ui_core_tick_order_contracts

Date: 2026-04-14
Scope: `ui_core_tick` + handlers appeles directement depuis cette boucle.

## Sequence contractuelle (ordre reel)
Source: `Src/UI/ui_core.c:2709`.

1. `ui_core_handle_track_selection_event(&ev)`
2. `ui_core_mute_handle_event(&ev)` -> `continue` si non-zero
3. `ui_core_is_track_hall_event_consumed(&ev)` -> `continue` si non-zero
4. `ui_core_handle_master_buffer_routing_event(&ev)` -> `continue` si non-zero
5. `ui_core_handle_transport_event(&ev)` -> `continue` si non-zero
6. `ui_page_settings_handle_event(&ev)` -> `continue` si non-zero
7. `ui_core_handle_global_shortcuts(&ev)` -> `continue` si non-zero
8. `ui_core_handle_pattern_mode_event(&ev)` -> `continue` si non-zero
9. `ui_core_handle_seq_mode_event(&ev)` -> `continue` si non-zero
10. `ui_navigation_handle_event(&ev)` (jamais de `continue`)
11. `active_page->handle_event(&ev)` (handler final)

## Table: etape -> precondition -> consomme -> bloque aval -> fragilite
| Etape | Precondition d entree | Consomme | Bloque aval | Fragilite / sensibilite ordre |
|---|---|---|---|---|
| track_selection | `ev!=0`, mute inactif; seulement SHIFT / TRACK_MOD press-release (`ui_core.c:1284`) | Non (void, update etat) | Non | Doit rester avant handlers qui lisent `shift_down` / `track_select_armed` (transport, shortcuts, pattern, seq). Invariant reel. |
| mute | `ev!=0`; modes quick/prepare + gestures (`ui_core.c:485`) | Oui pour de nombreux events (hall, transpose_up, certains boutons) | Oui (`continue`) | Prioritaire par design. Si deplacee, mute perd son exclusivite operationnelle. Invariant reel. |
| consume track-hall | `track_select_armed!=0`, `mute_active==0`, mode != pattern, event hall (`ui_core.c:1357`) | Oui (predicate de consommation) | Oui (`continue`) | Gate defensif pour eviter fuite des halls vers seq/page/navigation pendant armement track. Invariant reel. |
| master-buffer routing | Track active = master buffer, mode ARP, hall press, track_select inactif (`ui_core.c:1375`) | Oui quand match | Oui (`continue`) | Doit preceder transport/settings/shortcuts/seq pour garantir interpretation hall en routing buffer. Invariant reel (niche). |
| transport | `ev!=0`, mute inactif; boutons PLAY/REC/TRANSPOSE_DOWN combos (`ui_core.c:1428`) | Oui quand match | Oui (`continue`) | Prioritaire sur settings/shortcuts/pattern/seq/navigation. Sensible car BTN_TRANSPOSE_DOWN ouvre pattern avant seq-mode. Invariant reel. |
| settings gate | Settings page ouverte (`ui_page_settings_is_open()!=0`, `ui_page_settings.c:522`) | Oui (tous events) | Oui (`continue`) | Verrou global quand settings ouverte: masque shortcuts/pattern/seq/navigation/page handler final. Invariant reel. |
| global shortcuts | `ev!=0`; clipboard / BTN_SETTINGS / SHIFT+COPY undo (`ui_core.c:2438`) | Oui quand match | Oui (`continue`) | Prioritaire sur pattern/seq/navigation/page. Peut masquer silencieusement actions metier aval. Sensible a l ordre. |
| pattern mode | `hall_mode==PATTERN` (`ui_core.c:2390`) | Oui pour HALL_PRESS pattern et sortie mode; non sinon | Oui si match | Place apres shortcuts: un shortcut peut court-circuiter pattern. Sensible a l ordre (choix policy). |
| seq mode | `hall_mode==SEQ` (`ui_core.c:2485`) | Oui pour events seq pertinents | Oui si match | Place apres pattern et shortcuts, donc seq est volontairement moins prioritaire. Invariant policy actuel. |
| navigation | `event BUTTON_PRESS` + regle valide (`ui_navigation.c:42`) | Non (void) | Non | Cumulable: la page peut changer puis l event continue vers `active_page->handle_event` de la nouvelle page. Sensible (effet page-switch intra-event). |
| page handler final | `active_page!=0 && handle_event!=0` (`ui_core.c:2778`) | N/A (pas de retour) | N/A | Derniere chance. Peut recevoir l event apres navigation (nouvelle page). Habitude d implementation acceptable mais effet non trivial. |

## Handlers prioritaires / exclusifs / cumulables / sensibles a l ordre
- Prioritaires: `mute`, `consume track-hall`, `master-buffer routing`, `transport`, `settings gate`, `global shortcuts`, `pattern`, `seq` (car `continue`).
- Exclusifs: ceux ci-dessus quand ils retournent non-zero (ils rendent l event exclusif pour ce tour).
- Cumulables: `track_selection` (maj etat seulement), `navigation` + `page handler final` (peuvent tous deux s executer sur le meme event).
- Sensibles a l ordre:
  - `track_selection` doit preceder tous les handlers qui dependent de `shift_down`/`track_select_armed`.
  - `transport` avant `pattern/seq` fixe la semantique de `BTN_TRANSPOSE_DOWN`.
  - `settings gate` avant `global/pattern/seq/navigation/page` impose un mode modal strict.
  - `navigation` avant `page handler final` fait traiter l event par la page potentiellement nouvelle.

## Cas de masquage silencieux par `continue`
- Settings ouverte: tous les events sont absorbes par `ui_page_settings_handle_event`, aucun passage vers shortcuts/pattern/seq/navigation/page final.
- Track-select arme + hall event (hors pattern): `ui_core_is_track_hall_event_consumed` absorbe, donc pas de seq/page/navigation.
- Mute actif: de nombreux events sont absorbes par `ui_core_mute_handle_event` (dont hall press/release), masquant tout aval.
- Event reconnu transport/global/pattern/seq: coupe completement navigation + page handler final.

## Invariants d ordre a preserver
- Mettre a jour `shift_down`/`track_select_armed` avant toute lecture decisionnelle de ces flags.
- Garder `mute` en tete de chaine de consommation pour conserver son exclusivite.
- Garder le gate `consume track-hall` avant les modes metier hall (`seq`, navigation/page).
- Preserver `transport` avant `pattern`/`seq` pour la semantique actuelle des combos.
- Preserver le mode modal settings (consommation totale) avant shortcuts et logique page.
- Preserver `navigation` juste avant `page handler final` si on veut conserver le comportement "switch puis dispatch sur nouvelle page".

## Plus petit point d extraction recommande ensuite
- Extraire uniquement la politique de priorite `if (...) continue;` de `ui_core_tick` vers une table locale de "stages" (meme ordre, memes calls, meme semantique), sans toucher aux handlers.
- Objectif minimal: rendre explicites les contrats `consumes_event`/`blocks_downstream` sans changer le comportement.
