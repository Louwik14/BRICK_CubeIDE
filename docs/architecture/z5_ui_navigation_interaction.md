# Z5 — UI, navigation et interaction

Z5 décrit la surface UI actuelle. La résolution se fait par contexte : ensemble demandé, track active, rôle topologique, famille/type et capacité runtime.

Pour une Play Track `Sampler/Stream`, la capacité runtime rend visible la sous-page `ENV/VCA` et ses encodeurs ADSR/retrigger. Le `Looper` conserve son contrat de transport et n'expose pas d'ADSR VCA vocal; la page et les paramètres VCA restent résolus par les chemins ENV communs.

## Navigation par track

`TRACK` puis `HALL 0..7` sélectionne les huit Play Tracks. Les Special sont sélectionnées uniquement lorsqu'elles existent dans la variante : Master, Looper, Input1..N et FX.

La page CFG permet de modifier famille/type uniquement pour une Play Track. Les Special affichent leur rôle fixe et leur identité CFG ; elles ne proposent ni conversion de famille ni browser Play.

Dans CFG Play, l'encodeur suit l'ordre borné `Off → Synth → Drum → MIDI → External → Sampler`, sans wrap aux limites et avec saut des familles indisponibles. Cet ordre d'affichage/navigation est distinct des valeurs enum persistées ; `Input1..3` n'en font jamais partie.

La navigation Low-Cost et Premium partage les mêmes ensembles et se distingue uniquement par la topologie publiée : Low-Cost expose une entrée, Premium trois. Toute page conditionnelle passe par `track_runtime_is_ui_ensemble_available()`.

`ui_template_family_resolve_effective_for_track()` est l'autorité commune entre masque runtime, rôle topologique et template effectif. Navigation, page TONE et clipboard d'ensemble réutilisent cette résolution ; la famille/type UI brute reste uniquement le fallback des Play Tracks et des rôles déjà couverts par ce contrat.

## Navigateur SD et lecture Stream

Le browser Sampler parcourt le catalogue persistant `Samples` avec un chemin borné et une navigation parent explicite. La profondeur 3 ou plus ne constitue pas un plafond de navigation : elle peut seulement provoquer un miss du cache de vues et donc une nouvelle opération SD catalogue.

Pendant une fenêtre Stream réellement active, l'owner `SD_ACCESS_CLIENT_SAMPLE_STREAM`, les locks de pages ou la policy `streaming_critical` restent l'autorité d'arbitrage. Le browser peut alors afficher `SD STREAM` et reporte l'opération. Après libération effective du reader, des pages pending et des locks owner/génération, l'absence d'owner/policy active autorise à nouveau le catalogue ; `sd_access_gate_last_owner()` n'est qu'un historique diagnostique et ne doit jamais maintenir ce feedback.

Le mode gate conserve le reader pendant la décroissance VCA jusqu'à `mixer_track_vca_requires_source() == 0`; le launch ignore le Note Off par contrat. Un stop transporté ou forcé libère ensuite le reader et l'owner via le service Stream hors IRQ.

## Chaîne Hall press/release et notes

Les deux variantes transmettent les mesures Hall brutes calibrées directement à `hall_engine_process_sample()` : il n'y a pas de moyenne numérique multi-échantillons entre l'ADC et les seuils. La cadence par touche est de 2,8 ms en Low-Cost et 0,8 ms en Premium ; `HALL_THRESHOLD_PPM` et `HALL_HYST_PPM` restent relatifs à la calibration `min/max`.

La navigation/on-off lit uniquement l'état pressé projeté par `hall_surface_refresh()` et ne consomme ni la vélocité ni une fenêtre d'attente. Le chemin note consomme séparément les drapeaux `hall_engine_consume_note_on()` / `hall_engine_consume_note_off()` ; la vélocité est calculée au franchissement press et le Note On est publié dans le même cycle superloop. Le Note Off reste soumis à l'hystérésis de release, sans debounce additionnel.

## Ensembles et boutons

Les ensembles UI courants sont `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX`. Le mapping produit des boutons de paramètres est :

- `BTN_PARAM_1` → ENV ;
- `BTN_PARAM_2` → TONE ;
- `BTN_PARAM_3` → MOD ;
- `BTN_PARAM_4` → MIX ;
- `BTN_PARAM_5` → PLAY pour les familles moteur qui le supportent ;
- `BTN_PARAM_6` Premium → ENV, sous-page VCA.

BTN6 Premium ouvre directement `ENV/VCA` avec `ui_page_template_env_open_vca()`. Il n'existe aucune page ou ensemble VCA autonome. Le module courant de la page est `Src/UI/pages/ui_page_template_env.c`.

Les LEDs reflètent l'ensemble logique actif, pas un backend physique : le bouton de l'ensemble courant est blanc et les autres boutons paramètre sont verts lorsqu'ils existent.

## ENV et rôles Special

ENV regroupe filtre, VCA, ENV3 et retriggers. La navigation expose les sous-pages ENV correspondantes. Le backend mixer VCA et le backend `mod_env3` restent invisibles comme ensembles autonomes.

Sur Master, TONE ouvre directement la reverb Mutable en deux pages (`WET/SIZE/DECAY/PRED`, puis `DAMP/HPF/LPF`), puis parcourt delay et compresseur. Sur FX, TONE ouvre FX1 et parcourt les quatre MacroFX FX1 à FX4. Ces deux surfaces sont résolues par le rôle issu de `track_topology` ; Master et FX ne sont pas fusionnés. Pour les deux rôles, seuls `CFG`, `TONE` et la séquence/action Special sont accessibles : `ENV`, `MOD` et `MIX` ne résolvent aucun template ou fallback vide.

`MIX` reste limité aux contrôles de mixage track-aware : niveau, pan, sends et mute. Les routes Looper et le contexte UI-only des MacroFX réutilisent le contexte ROUT approprié ; ils ne créent pas une nouvelle famille de paramètres.

## MIDI FX et ARP

`SHIFT + HALL 10` ouvre MIDI FX. Le raccourci physique `ARP` ouvre la même surface sans créer de hall mode ARP, sans double-tap et sans changer le mode musical `SEQ`/`KEYBOARD`.

MIDI FX est un modèle de quatre slots par Play Track. Chaque slot porte quatre valeurs génériques (`PARAM1`, `PARAM2`, `PARAM3`, `MODEL`) et le modèle `OFF/ARP`; un seul slot ARP est effectif par track. MIDI FX est disponible seulement selon la capacité runtime de la Play Track. Aucune Special n'alloue de `note_fx_state`.

## Séquence, p-lock et édition

Les Play Tracks possèdent chacune leur séquence, clavier, MIDI FX, scheduler, live record et mute. Les Special ont leur modèle de séquence fixe et ne reçoivent pas de notes PLAY.

Les p-locks sont résolus par ensemble et par capacité de la track. L'UI ne présente que les paramètres effectivement supportés ; un paramètre structurel, CFG ou global non p-lockable reste éditable par son chemin normal.

`SHIFT + -` prépare `PATTERN RECALL` et `TRACK + -` effectue `PATTERN STORE`. Les modes QUICK MUTE et PREPARE MUTE conservent leur priorité propre ; l'UI délègue l'état audio à `track_mute`.

## Clipboard, clear et undo

Les scopes sont explicites :

- `TRACK + COPY/PASTE` copie ou colle la track active sans ses steps séquenceur ;
- `TRACK + SHIFT + PASTE` clear la track active ;
- `PARAM` maintenu + `COPY/PASTE` agit sur l'ensemble ;
- le bouton de page maintenu + `COPY/PASTE` agit sur la page ;
- `SHIFT + PASTE` dans un scope ensemble/page remet les paramètres ciblés à leur minimum.

Le collage par ensemble/page utilise l'intersection des `param_id` compatibles ; il ne dépend pas d'un layout strict. Le collage d'une track valide `role + ordinal`, famille/type, capacités et ressources exclusives avant mutation. Une track `External` conserve exactement son entrée physique ; un conflit est refusé sans substitution automatique.

Le scope TONE Master agrège les pages reverb, delay et compresseur et manipule leurs globals par `param_get/param_set`; son undo/redo utilise le snapshot global Pattern. Le scope TONE FX couvre les seize paramètres des quatre MacroFX et reste track-aware sur l'index de la Special FX. Un collage sans intersection compatible est refusé.

Les overlays de scène utilisent le même feedback visuel de lock que l'édition p-lock ; ils ne créent pas un indicateur distinct par ancienne banque Macro.

Patch est Play-only : ses overlays ne s'appliquent jamais à Master, Looper, Input ou FX. Kit et snapshot Track utilisent leurs scopes respectifs.

## Historique utile

Les anciens modes ARP séparés, l'ouverture d'une page VCA autonome, les familles Special configurables et le module `ui_page_template_filter` ne sont pas des chemins UI courants. Les noms historiques peuvent rester dans les audits, mais ne doivent pas servir à résoudre une page actuelle.
