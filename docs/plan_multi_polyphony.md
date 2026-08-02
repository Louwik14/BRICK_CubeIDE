# Plan d'action — Multi Sampler polyphonique par voix

## 1. Verdict de faisabilité

**Faisable sans modifier le contrat mono/stéréo, les pages, les présocles ni le cache.** Le Multi est déjà polyphonique du point de vue des readers et de l'allocation de sources, mais il est paraphonique pour le traitement temporel : les readers sont sommés dans un buffer de track, puis un seul filtre et un seul VCA de track traitent cette somme.

La recommandation est un **pool statique de slots DSP Multi indépendant du pool synth**. Chaque slot est lié à une voix Multi par index + `trigger_order`/génération et contient l'état filtre, l'état VCA et le cycle de vie DSP de cette voix. Le reader reste dans la voix sampler. Le filtre et le VCA de track ne doivent plus traiter le signal Multi après sommation ; le gain, le pan, les inserts, les sends et le routing de track restent post-sommation.

Cette option applique le nouveau contrat produit de **8 voix Multi globales maximum**, avec une limite par piste égale à `clamp(VOICES, 1, 8)`, évite de gonfler les voix Stream/RAM et évite de coupler l'identité Multi à `synth_polyphony`.

Audit effectué sur `HEAD` `c6fdd682c` après les commits Stream/Multi mono. Aucun code, build, test, instrumentation ou commit n'est requis par cette passe.

Dans la suite, `8` est la capacité Multi globale cible ; toute occurrence de `16` décrit explicitement l'état HEAD, le pool synth ou la capacité Stream/page-cache séparée, jamais le nouveau budget Multi.

## 2. Architecture actuelle vérifiée

### 2.1 Contrat de format

Le contrat est déjà homogène par instrument Multi :

- `sample_audio_format_t` ne permet que `SAMPLE_AUDIO_FORMAT_FLOAT32_MONO` et `SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED`.
- `multi_sample_index_validate()` vérifie le format global de l'index et le format de chaque sample dans `Src/Sampler/multi_sample_index.c`.
- `multi_sample_pool_set_instrument_format()` et `multi_sample_pool_set_sample_format()` refusent un mélange mono/stéréo dans un instrument.
- `brick6_sampler_runtime_track_is_mono_native()` ne sélectionne la voie mono que si toutes les voix actives du Multi sont mono.
- `brick6_sampler_runtime_multi_voice_format_compatible()` arrête une voix si elle ne correspond plus au format de l'instrument.

Géométrie actuelle : mono = 1 float/frame, 4096 frames/page ; stéréo interleavée = 2 floats/frame, 2048 frames/page ; page physique = 16 KiB.

Le page-cache à HEAD expose `SAMPLE_PAGE_CACHE_MAX_VOICES = 16` et réserve `SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES = 16 × SAMPLE_PAGE_MULTI_WINDOW_PAGES × 2`, soit 128 pages avec la fenêtre stéréo actuelle de 4 pages. Cette valeur est une réserve de fenêtres, pas l'autorité synth de 16 voix. Le nouveau contrat devra réserver 8 fenêtres Multi : 8 × 4 × 2 = 64 pages, soit 1 MiB de pages physiques, tout en séparant explicitement la réserve Stream (`SAMPLE_STREAM_MAX_ACTIVE = 16`) et en conservant les invariants de capacité globale. Il ne faut pas réduire aveuglément une constante commune si cela ampute Stream ; la cible est une capacité Multi déterministe de 8 et une capacité Stream déterministe inchangée.

### 2.2 Déclenchement

Le chemin séquenceur est `seq_play_scheduler_emit_local_note()` dans `Src/Seq/seq_play_scheduler.c` :

1. `track_runtime_resolve_track()` fournit le type, l'instance, le mix target et le filter target.
2. Pour `TRACK_RUNTIME_TYPE_MULTI`, le scheduler appelle `brick6_sampler_runtime_trigger_multi_track_note_velocity_token()` pour Note On et `brick6_sampler_runtime_note_off_multi_track_note_token()` pour Note Off.
3. Pour un Note On, `brick6_sampler_runtime_trigger_multi_note_velocity()` vérifie le track sampler, l'instrument prêt, puis appelle `multi_sample_pool_resolve()` pour choisir la zone selon note/vélocité.
4. Le sample est obtenu par `multi_sample_pool_get_sample()` ; la page 0 doit être `SAMPLE_PAGE_READY`.
5. `brick6_sampler_runtime_multi_alloc_voice()` alloue une case libre ou vole une voix.
6. Le play plan est construit par `brick6_sampler_runtime_build_common_play_plan()`, avec pitch de zone, gain, loop Multi et garde de démarrage.
7. `sample_voice_reader_bind_play_plan()` lie le reader ; les pages courantes et de loop sont réservées par `sample_stream_manager_reserve_active_pages()`.
8. La voix reçoit un `trigger_order`, devient `BRICK6_SAMPLER_VOICE_MULTI`, puis `mixer_track_vca_note_on()` est appelé sur le mix target. Le scheduler a déjà appelé `mixer_track_filter_note_on()` sur le filter target.

Le Note Off actuel est recherché par `[track_id, note]` dans toutes les cases actives. Toutes les voix Multi de même hauteur sont marquées `release_pending` et reçoivent un `mixer_track_vca_note_off()`. Le `trigger_order` existe, mais n'est pas utilisé pour apparier un Note Off à une occurrence précise.

Le retrigger crée donc une nouvelle occurrence tant que le budget le permet ; il n'a pas d'identifiant public de note actif permettant de fermer une seule occurrence de même hauteur.

### 2.3 Limite de voix et ownership

Les constantes réelles sont :

| Autorité | Valeur | Rôle |
|---|---:|---|
| `SAMPLER_MULTI_MAX_VOICES_PER_TRACK` | `clamp(VOICES, 1, 8)` | limite Multi par piste selon le contrat cible |
| `SAMPLER_MULTI_MAX_GLOBAL_VOICES` | 16 à HEAD, cible 8 | tableau `g_sampler_multi_voice[]` et budget Multi ; la réduction à 8 est une modification planifiée |
| `SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET` | 16 | pool synth et `g_poly_filters_hot[]`, séparé du Multi |
| `SYNTH_POLYPHONY_MAX_VOICES` | 8 | slots par track synth, sans autorité actuelle sur Multi |
| `SAMPLE_PAGE_CACHE_MAX_VOICES` | 16 | réserve de fenêtres page-cache, pas un pool DSP de voix |
| `SEQ_TRACK_COUNT` | 14 | tableau de runtime sampler ; seuls les tracks routables peuvent jouer |

Le Multi ne partage pas actuellement les slots `synth_polyphony` avec Prism/Stack/Wave/DELUGE. Le nouveau budget de 8 est donc propre au Multi ; le budget synth `SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET = 16` ne change pas. Le Multi ne réserve pas non plus une voix Stream commune. En revanche, `brick6_sampler_runtime_global_volable_voice_count()` compte les voix Multi actives et les voix one-shot classic/RAM considérées volables ; l'allocation Multi peut voler une voix Multi globale ou une voix classic/RAM plus ancienne. Stream n'est pas compté dans cette autorité de voix, mais Stream/Multi/RAM partagent des ressources de page-cache et des budgets de slots de samples.

Politique actuelle :

- si une piste atteint sa limite `clamp(VOICES, 1, 8)`, la plus ancienne voix Multi de cette piste est volée ;
- si le budget global Multi de 8 est atteint, la plus ancienne voix Multi est volée ; à défaut une voix classic/RAM one-shot est volée ;
- si aucune cible n'est disponible, le Note On est refusé ;
- l'âge est `trigger_order`, incrémenté par `brick6_sampler_runtime_next_trigger_order()` ;
- les owners Stream sont protégés par `owner_id` + `owner_generation` dans `sample_stream_manager`.

### 2.4 `VOICES` et `SPREAD` — contrat cible

Le Multi doit utiliser les paramètres CFG communs existants, sans créer de second vocabulaire :

- `PARAM_CFG_POLY_VOICES` (`"VOICES"`) : `PARAM_TYPE_INT`, bornes 1..8, pas 1, défaut 1 ;
- `PARAM_CFG_POLY_SPREAD` (`"SPREAD"`) : `PARAM_TYPE_FLOAT`, bornes 0..1, pas 0,01, défaut 0 ;
- mêmes IDs, encodage, affichage, validation et emplacement CFG non p-lockable que pour les moteurs synthétiques ;
- aucune nouvelle valeur persistée, aucun nouveau slot de p-lock et aucune nouvelle identité Pattern/Project.

À HEAD, `param_registry_apply_track_value()` route ces IDs vers `synth_polyphony_set_voice_count()`/`synth_polyphony_set_spread()`, et `track_runtime_get_play_voice_count_from_descriptor()` renvoie encore une valeur codée en dur pour un Multi. L'étape dédiée doit donc réutiliser le descripteur commun et le contrat de paramètre, mais introduire une façade de configuration polyphonique capable de router une piste Multi vers son état sampler sans appeler l'allocator synth. Il faut auditer `param_registry.c`, `track_runtime.c`, `ui_page_template_cfg.c`, `ui_param.c`, `pattern_live_ram.c`, `undo_v2.c`, `patch_v1.c` et `kit_v1.c` avant toute modification ; le stockage existant reste inchangé.

Allocation cible : la limite Multi par piste est `clamp(VOICES, 1, 8)`. Une piste avec `VOICES = 8` peut donc utiliser les 8 voix si elles sont disponibles. Plusieurs pistes Multi partagent le même pool global de 8. Sous la limite de la piste, une case libre est choisie. À la limite de piste, la plus ancienne voix de cette piste est volée selon `trigger_order`. À la saturation globale, la politique globale de vol ou de refus s'applique ; une neuvième voix n'est jamais active.

Lorsqu'une piste réduit `VOICES` alors que davantage de voix sont actives, les voix de cette piste sont ordonnées par `trigger_order` et les plus anciennes sont évincées jusqu'à respecter `clamp(VOICES, 1, 8)`, avec la même fermeture bornée que pour un steal. Une augmentation de `VOICES` conserve toutes les voix actives et rend seulement les rangs libres disponibles aux prochains Note On.

`SPREAD` doit reprendre la formule synth actuelle de `synth_polyphony_get_voice_pan()` : pour N voix, la position normalisée dépend de l'index stable `0..N-1`, avec toutes les voix au centre à N=1, et une excursion proportionnelle à `SPREAD`. L'index de rendu doit être stable pour une génération ; un simple ordre de scan qui changerait après un steal est interdit.

- `SPREAD = 0` : toutes les voix restent centrées ; un Multi mono peut rester sur le chemin mono-native jusqu'à la somme ;
- `SPREAD > 0` : un Multi mono est promu en accumulation stéréo au point nécessaire pour placer les voix ; un Multi stéréo reste sur son chemin stéréo ;
- le pan de track est appliqué une seule fois après la somme ; aucun pan/spread ne doit être réappliqué dans le mixer après la somme ;
- le filtre/VCA par voix reçoit le signal avant cette projection stéréo, avec VCA commun aux deux canaux pour une source stéréo ;
- une modification de `SPREAD` affecte les voix actives au bloc suivant via une version de configuration, sans réinitialiser reader, filtre ou enveloppe ; les nouvelles voix prennent la valeur courante.

Le nouveau pool DSP Multi est donc de 8 slots maximum, même si le pool `g_poly_filters_hot` synth reste de 16 slots.

Le plan conserve ces valeurs et cette architecture statique tant qu'une décision produit explicite n'a pas changé le budget.

### 2.5 Rendu actuel

`brick6_sampler_runtime_render_multi_track()` parcourt actuellement les 16 cases Multi appartenant au track et appelle `brick6_sampler_render_multi()` pour chaque voix. Le nouveau contrat doit réduire ce tableau et toutes ses boucles à 8 cases. Chaque reader possède déjà son état de position, pas, plan, curseur et pages. Le reader ajoute son signal dans `out_l/out_r` ; la somme est donc déjà polyphonique côté source.

Le chemin audio est ensuite `brick6_render_sampler_tracks()` dans `Src/Core/brick6_audio_runtime.c` :

- Multi mono : rendu vers `mixer_begin_external_mono_native()`, puis traitement du track en mono natif ;
- Multi stéréo : rendu vers un buffer externe stéréo, puis traitement du track en stéréo ;
- le Multi ne passe jamais par `mixer_begin_external_poly()` ni `mixer_process_external_poly_voice()` ;
- les buffers temporaires partagés sont `sampler_tmp_l`, `sampler_tmp_r` et `sampler_tmp_mono_discard`, 64 frames.

Dans `mixer_process()` l'ordre effectif est : moteur externe → filtre de track → VCA de track → gain/pan/mute → inserts de track → sends/routing/bus. Pour une voie Multi, le filtre et le VCA sont donc appliqués une fois après la somme.

### 2.6 Filtre et VCA actuels

`mixer_track_filter_t` contient :

- `fx_biquad_filter_t` et `fx_biquad_filter_mono_t` ;
- `env_adsr_t filter_env` et `env_adsr_t vca_env` ;
- `fx_dj_eq3_t` et `fx_dj_eq3_mono_t` ;
- paramètres cibles/smoothers, keytrack, retrigger, valeurs de diagnostic et buffers de préparation d'enveloppe.

Les modes réels sont `MIXER_TRACK_FILTER_OFF`, `MIXER_TRACK_FILTER_EQ3`, `MIXER_TRACK_FILTER_LP_BI`, `MIXER_TRACK_FILTER_HP_BI` et `MIXER_TRACK_FILTER_BP_BI`.

Le filtre reçoit bien un Note On/Off au niveau track via `mixer_track_filter_note_on()`/`mixer_track_filter_note_off()`, mais son historique DSP et son `filter_env` sont uniques au track. Le VCA reçoit un Note On par voix via le compteur `vca_note_count`, mais l'enveloppe `vca_env` et la décision de source sont uniques au track. C'est une agrégation de gates, pas un VCA polyphonique.

### 2.7 Cycle de vie et dette de release

`brick6_sampler_runtime_multi_stop_voice()` arrête immédiatement le reader, libère l'owner Stream, ferme le gate VCA partagé et remet la case à zéro. Il est utilisé pour le steal, panic/stop de track, changement d'instrument et arrêt forcé.

Pour un Note Off normal, la source continue avec `release_pending`. Cependant `brick6_sampler_runtime_render_multi_track()` arrête la voix dès que `mixer_track_vca_requires_source()` devient faux. Comme cette fonction lit le VCA partagé du track, une release courte du VCA peut couper une autre source encore active ou une source dont le reader devait continuer. Le contrat demandé n'est donc pas satisfait.

À la fin naturelle du reader, `brick6_sampler_render_multi()` met la source inactive et appelle le chemin de libération Stream/VCA. Le VCA partagé peut rester dans un état de sustain pour les autres notes ; l'identité de la voix terminée n'est pas conservée dans un état DSP dédié.

Le contrat cible sépare explicitement la durée de vie de la source et celle du DSP :

- si le VCA est terminé/IDLE, le signal ne peut plus contribuer ; arrêter et libérer immédiatement le reader, les pages, l'owner Stream, le token et le slot DSP, même si le sample aurait encore des frames ;
- si la source produit encore du son et que le VCA est actif, continuer à rendre et traiter la voix ;
- si un one-shot atteint naturellement EOF alors que le VCA reste actif, terminer le reader, relâcher ses pages et son owner devenus inutiles, puis conserver seulement l'état DSP/gate nécessaire jusqu'à la fin propre du cycle VCA ; aucune donnée audio ni page ne doit rester épinglée inutilement ;
- Note Off avant EOF : gate VCA/filter off, reader encore vivant jusqu'à EOF ou jusqu'à ce que le VCA devienne IDLE ;
- EOF avant Note Off : reader/pages/owner libérés à EOF, slot DSP conservé uniquement tant que le VCA actif ; le Note Off ultérieur ferme le token actif sans lookup par hauteur ;
- loop : le reader reste vivant tant que le loop est actif et que le VCA contribue ; Note Off doit sortir selon le contrat de loop fixé à l'étape 1 ;
- release terminée avant la source : VCA IDLE entraîne l'arrêt immédiat de la source et la libération des pages/owner/slot ; le sample n'est pas rendu silencieusement pendant un état fantôme ;
- arrêt forcé, steal et panic : fermeture immédiate et bornée, invalidation du token par génération, tail de déclic éventuelle, puis libération complète sans attendre une release longue.

Cas actuels :

- one-shot : le reader termine et la source est retirée ;
- Note Off avant EOF : `release_pending`, mais la libération dépend du VCA track partagé ;
- loop : le plan Multi actuel n'active que le loop forward via `loop_enabled` ;
- reverse/ping-pong : le reader générique sait les traiter, mais le déclenchement Multi actuel force `reverse = 0` et `SAMPLE_PLAY_LOOP_NONE` avant le plan commun ; ils ne sont pas des modes Multi actifs à HEAD ;
- panic/transport stop/changement d'instrument : arrêt forcé immédiat, sans release audible garanti ;
- source terminée avant Note Off : la voix n'est plus trouvable par `[track,note]`, alors que le scheduler peut encore produire le Note Off.

## 3. Classification polyphonique / paraphonique

| Élément | État à HEAD | Conclusion |
|---|---|---|
| Résolution note/vélocité → zone | indépendante par Note On | polyphonique |
| Sample sélectionné et `sample_audio_key_t` | indépendant par voix | polyphonique |
| Reader, position, pas, direction, loop cursor | indépendant dans `g_sampler_multi_voice[]` | polyphonique côté source |
| Pages et owner Stream | indépendant par slot + `trigger_order` | polyphonique côté cache/ownership |
| Fade de démarrage/declick | fade de slot, avec tail de steal par track | partiellement par voix ; tail de sortie partagé par track |
| Gain de voix et vélocité | stockés dans chaque voix et appliqués avant somme | polyphonique |
| Accumulation | une somme par track | partagé par track, attendu après readers |
| Filtre | un `g_track_filters[mix_track]` après somme | paraphonique |
| Filter envelope/gate | un état par track | paraphonique |
| VCA/enveloppe | un `g_track_filters[mix_track].vca_env` et compteur de notes | paraphonique |
| Release et décision de fin | dépend du reader individuel et du VCA partagé | paraphonique et fragile |
| Pan/volume/mute/routing | une fois après somme | partagé au niveau track, correct |
| Inserts/sends/master | après traitement de track | partagé au niveau track, à conserver |
| `trigger_order` | présent pour age et owners Stream | ownership partiel ; insuffisant pour Note Off exact |

Le comportement observé n'est donc pas le faux modèle « plusieurs readers puis filtre/VCA partagés » par accident : c'est exactement ce modèle pour le filtre/VCA, malgré des readers réellement séparés.

## 4. Architecture recommandée

### 4.1 Modèle cible

L'allocator applique `track_limit = clamp(VOICES, 1, 8)` pour chaque piste et `global_limit = 8` pour l'ensemble des pistes Multi. Une piste réglée à `VOICES = 8` peut donc occuper les 8 slots globaux si aucune autre piste ne les utilise ; le partage inter-pistes reste borné par le pool global.

```text
Multi voice[i]
  reader + position + play plan + owner/generation
      ↓
  gain/fade de voix
      ↓
  DSP slot[i] : filtre + filter envelope + VCA + gate/release
      ↓
  somme Multi mono ou stéréo
      ↓
  volume/pan/mute/routing de track
      ↓
  inserts/sends/master
```

Le slot DSP doit être attribué au moment du Note On et libéré seulement après la fin de la voix. Il contient un état de filtre et un état VCA propres à la voix, mais reçoit des paramètres partagés du track via une synchronisation légère.

### 4.2 Stockage recommandé

Créer un pool statique de **8 slots DSP Multi maximum**, séparé de `g_poly_filters_hot`. La voix Multi conserve seulement une référence bornée : `dsp_slot` et `dsp_generation` (noms à arrêter à l'étape 1). Le slot porte `owner_voice_index`, `owner_track_id`, `owner_generation` et un état `FREE/HELD/RELEASE/TERMINAL`. Le tableau sampler, ses scans, ses owners et ses réserves de pages doivent tous avoir la borne globale 8 ; chaque piste applique en plus `clamp(VOICES, 1, 8)`. Le pool synth `g_poly_filters_hot` reste à 16.

La structure doit utiliser une union mono/stéréo pour le filtre actif, car un instrument Multi est homogène. Elle doit couvrir les modes actuels OFF, LP, HP, BP et EQ3 ; elle ne doit pas embarquer simultanément tout le biquad mono, biquad stéréo, EQ3 mono et EQ3 stéréo comme `mixer_track_filter_t`.

Placement recommandé :

- reader/position/plan/owner déjà chauds : conserver l'existant dans `.dtcm_audio` pendant la migration ; éviter d'agrandir massivement `brick6_sampler_voice_t` ;
- pool filtre/VCA par voix : `.ram_d1_audio`, aligné 32 octets, car il est fréquent mais plus volumineux que l'état de gate minimal ;
- bitmap/ownership/générations : `.ram_d2_lut` ou `.ram_d3_ctrl` selon la fréquence d'accès ; pas de gros état froid en DTCM ;
- pages audio : rester dans le page-cache/SDRAM existant ; ne pas déplacer les pages dans le nouveau pool.

### 4.3 Contrat de traitement

Le renderer Multi doit fournir une API mono et stéréo au DSP par voix, ou une API unique avec format connu du slot. Pour mono : reader mono → filtre mono → VCA mono → accumulation mono. Pour stéréo : reader stéréo → filtre stéréo lié → VCA commun aux deux canaux → accumulation stéréo. Si `SPREAD > 0`, le Multi mono est promu en accumulation stéréo après le traitement de voix ; à `SPREAD = 0`, il reste mono-native.

`mixer_process_external_poly_voice()` est une référence utile pour le séquencement filter/VCA, mais ne peut pas être réutilisé tel quel : il dépend de `synth_polyphony_get_slot()`, est limité à `SYNTH_POLYPHONY_TRACK_CAPACITY`, accepte un buffer mono et applique un pan de voix synth. Le Multi doit garder son pan de track unique et son pool d'ownership distinct.

Après migration, le chemin Multi doit publier une somme déjà filtrée/VCA-ée. Le mixer doit reconnaître ce format comme « DSP par voix déjà effectué » et bypasser seulement le filtre/VCA de track pour ce lane. Le gain/pan/mute, le pan issu de `SPREAD` avant sommation, les inserts, sends, routing, bus et master restent actifs une seule fois. L'ordre musical des inserts ne change pas.

### 4.4 Paramètres partagés et synchronisation

Les setters existants de `mixer.h` restent l'autorité des paramètres de track : cutoff, resonance, type, EQ3, keytrack, ADSR filtre, ADSR VCA, retrigger. Le slot conserve les mémoires internes et une copie compacte des cibles/version.

Politique recommandée :

- les paramètres courants sont appliqués à une nouvelle voix au Note On ;
- les changements de cutoff, résonance, type et ADSR sont propagés aux voix déjà actives au début de chaque bloc si la version de configuration a changé ;
- le setter incrémente une version par track, au lieu de recopier une grosse configuration à chaque bloc ;
- le smoothing reste bloc/échantillon comme dans le mixer actuel ;
- aucun état interne (`ic1eq`, `ic2eq`, phase d'enveloppe, gate, stage) n'est partagé ;
- les p-locks continuent d'utiliser les setters existants ; aucune refonte du système MOD/p-lock.

Cette politique correspond au chemin synth poly existant, où `mixer_poly_filter_sync_config()` copie les paramètres vers le slot à chaque rendu sans copier son historique DSP. Elle rend les changements live audibles sur les voix actives tout en évitant une copie inutile quand rien n'a changé.

## 5. Comparaison des architectures de stockage

| Option | RAM | CPU | Complexité | Risque | Mono/stéréo | Réutilisation | Verdict |
|---|---:|---:|---:|---:|---|---|---|
| A. Réutiliser `mixer_process_external_poly_voice()` | 0 nouvelle RAM au départ | bon, chemin existant | moyenne à forte | couplage synth/Multi, ownership incompatible, mono seulement | stéréo à étendre | forte | utile comme référence, pas recommandé tel quel |
| B. Pool filtre/VCA dédié Multi | un slot par voix, estimé 320–480 B selon union/modes | linéaire par voix | moyenne | duplication d'algorithmes si helpers non factorisés | naturel, union active | moyenne | bonne base d'implémentation |
| C. Ajouter filtre/VCA à `brick6_sampler_voice_t` | +320–480 B sur les 8 voix Multi, donc environ +2,5–3,8 KiB, mais aussi risque de gonfler les autres usages de la structure | identique à B | faible au début | gaspillage pour classic/Stream/RAM, DTCM saturée | naturel mais structure gonflée | faible | déconseillé |
| D. Pool statique DSP générique partagé | même ordre de RAM que B pour 8 slots | identique à B | moyenne à forte | génération/ownership à verrouiller, abstraction prématurée | naturel | forte pour Sampler Group futur | **recommandé, limité d'abord au Multi** |

Le coût de B/D est une estimation de planification : un biquad mono/stéréo, ou EQ3 mono/stéréo en union, deux `env_adsr_t`, des cibles compactes et des flags. Le `sizeof` exact du nouveau slot doit être figé par assertion à l'étape 2. Il ne faut pas reprendre les 928 octets complets de `mixer_track_filter_t` par voix sans mesurer, car cette structure contient simultanément les variantes mono et stéréo, EQ3, préparation d'enveloppe et diagnostics.

## 6. Contrat Note On / Note Off / release

### 6.1 Identité

Une voix active est identifiée par `(multi_voice_index, generation)` ; `trigger_order` devient la génération d'ownership stable de la voix. Le scheduler doit fournir un token/handle de note actif ou une identité équivalente au runtime Multi. Ce token est **uniquement un handle runtime actif**, créé et détruit avec l'occurrence en cours : il ne modifie aucun format persistant de step, ne crée aucune identité Pattern/Project et ne demande aucune refonte du stockage. Il sert seulement à distinguer les occurrences actives, notamment plusieurs notes identiques sur la même piste. Une paire `[track,note]` seule est interdite pour le nouveau chemin.

### 6.2 Note On

1. Résoudre instrument, zone, sample, format et play plan.
2. Réserver atomiquement une voix sampler et un slot DSP Multi.
3. Invalider l'ancien occupant en cas de steal avec la même génération ; fermer son gate et libérer ses pages selon la politique de steal.
4. Initialiser reader, filtre, filter envelope, VCA envelope, gate, fade et `generation`.
5. Copier les paramètres courants du track et retrigger les deux enveloppes propres à la voix.
6. Publier l'identité active au scheduler/runtime.

### 6.3 Note Off

Le Note Off ferme exactement le token correspondant. Il met `gate = 0`, appelle `env_adsr_gate_off()` pour le filtre et le VCA de cette voix, et laisse le reader actif tant que le contrat de fin l'exige. Les autres voix du track, y compris celles de même hauteur, ne sont pas touchées.

### 6.4 Fin de voix

```text
source_active = reader produit encore du son et n'est pas arrêté
vca_active    = VCA non IDLE
audio_active  = source_active && vca_active
voice_active  = source_active || vca_active, sauf arrêt forcé
```

La libération normale a deux phases. Dès que `vca_active == false`, arrêter et libérer immédiatement la source, les pages, l'owner et le slot DSP. Si `source_active == true` et `vca_active == true`, continuer à rendre. Si le reader finit avant le VCA, libérer immédiatement reader/pages/owner audio, conserver uniquement l'état DSP/gate nécessaire à la fin du VCA, puis libérer le slot et le handle quand le VCA devient IDLE. L'ordre de destruction est : terminer le bloc audio courant, libérer les pages/owner avec la génération, remettre le reader inactif, libérer le slot DSP, puis rendre le handle runtime réutilisable.

Cas obligatoires :

- one-shot finissant avant Note Off : reader/pages/owner audio libérés à EOF, VCA actif conservé dans le slot DSP sans données audio inutiles ; le Note Off ultérieur ferme le token runtime actif ;
- Note Off avant EOF : le reader continue selon loop/direction tant que le VCA contribue ; si le VCA devient IDLE avant EOF, arrêter alors immédiatement le reader et ses pages ;
- loop forward : Note Off doit sortir de la boucle selon une décision de contrat figée, puis attendre le VCA ;
- reverse/ping-pong : conserver les états de direction et de limite du reader ; ne pas réutiliser un slot tant que le reader n'a pas réellement atteint son terminal ; à HEAD ces modes ne sont pas exposés par le trigger Multi et restent désactivés jusqu'à une décision explicite ;
- release terminée avant sample : le VCA IDLE arrête immédiatement le reader, libère les pages/owner et le slot DSP ; le sample n'est pas conservé dans un état fantôme ;
- voice steal : arrêt forcé déterministe, fermeture gate, tail de déclic bornée, owners libérés avec génération ; pas de release longue qui bloque l'allocation ;
- panic/transport stop/changement d'instrument : arrêt forcé de toutes les voix concernées et invalidation de tous les handles ;
- retrigger : nouvelle génération, ancien handle inchangé jusqu'au steal ou à sa fin ;
- sample court : ne pas considérer page 0/EOF comme fin du VCA ;
- changement de cutoff/ADSR : ne jamais réinitialiser les mémoires DSP d'une voix active sauf changement de type nécessitant le reset documenté.

## 7. Coût RAM/CPU théorique et RAM cible

### 7.1 État actuel mesuré dans les artefacts existants

Les tailles ci-dessous sont celles des symboles de l'ELF/map Premium déjà présents dans le dépôt, sans relancer de build :

| Objet | Taille unitaire | Instances | Total | Placement |
|---|---:|---:|---:|---|
| `brick6_sampler_voice_t` / `g_sampler_multi_voice` | 984 B (`0x3D80 / 16`) | 16 | 15 744 B | `.dtcm_audio` |
| `brick6_sampler_voice_t` / `g_sampler_voice` | 984 B (`0x35D0 / 14`) | 14 | 13 776 B | `.dtcm_audio` |
| `mixer_track_filter_t` / `g_poly_filters_hot` | 928 B (`0x3A00 / 16`) | 16 | 14 848 B | `.dtcm_audio` |
| `mixer_track_filter_t` / `g_track_filters` | 928 B (`0x32C0 / 14`) | 14 | 12 992 B | `.dtcm_audio` |

Le Multi actuel paie donc déjà 984 B par voix pour reader/plan/metadata, mais aucune mémoire filtre/VCA par voix. Il paie un `mixer_track_filter_t` par track routable, pas par voice. Le tableau `g_sampler_multi_voice` et ses 16 cases sont une photographie de HEAD ; le contrat cible demande 8 cases.

### 7.2 Projection

| Élément | Multi actuel | Multi poly par voix |
|---|---:|---:|
| Readers | 1 par voix active, jusqu'à 16 à HEAD | 1 par voix active, jusqu'à 8 |
| Filtres | 1 par mix track, partagé par toutes les voix | 1 état indépendant par voix active, jusqu'à 8 slots |
| VCA | 1 par mix track, compteur partagé | 1 état ADSR/gate par voix active, jusqu'à 8 slots |
| États de release | `release_pending` par reader + VCA track partagé | gate/release/terminal par voix + génération |
| RAM mono par voix | 984 B | 984 B + slot DSP estimé 320–480 B |
| RAM stéréo par voix | 984 B | 984 B + slot DSP estimé 320–480 B |
| CPU mono par voix | reader ; filtre/VCA une fois par somme | reader + filtre mono + VCA par voix |
| CPU stéréo par voix | reader ; filtre/VCA une fois par somme | reader + filtre stéréo + VCA par voix |
| Coût fixe de track | gain/pan/VCA/filtre/inserts/sends | gain/pan/inserts/sends ; filtre/VCA track bypassés pour Multi |
| Libération de voix | source ou VCA partagé peut décider trop tôt | `reader terminé/arrêté && VCA terminé`, génération exacte |

Le delta brut maximal du nouveau pool est estimé à 8 × 320–480 B, soit environ 2,5–3,8 KiB. Le gain est musical et de cycle de vie, pas une économie CPU : à N voix, le filtre/VCA coûte N traitements au lieu d'un. En contrepartie, le filtre/VCA de track et sa double application sont supprimés du chemin Multi. `SPREAD > 0` ajoute une accumulation stéréo pour un Multi mono ; `SPREAD = 0` conserve le chemin mono-native. Le coût CPU théorique est linéaire avec N et dépend du mode de filtre ; aucune mesure CPU matérielle n'est demandée à l'agent. La mesure IRQ mono/stéréo et selon `VOICES/SPREAD` sera effectuée sur matériel par l'utilisateur.

Une piste à `VOICES = 8` ne crée pas un pool supplémentaire : elle peut consommer les 8 slots DSP/reader globaux, tandis que plusieurs pistes se les partagent sous la même borne. La RAM maximale reste donc celle de 8 slots, indépendamment de la répartition entre pistes.

Le mono ne reçoit pas automatiquement plus de voix que le contrat ne l'autorise. Le budget reste 8 global en mono et en stéréo ; les valeurs `VOICES = 1/2/4/8` sont validées et la limite effective par piste est toujours `clamp(VOICES, 1, 8)`. Le mono gagne seulement sur la largeur de données et le coût du filtre par voix, sauf promotion stéréo requise par `SPREAD > 0`.

Réserves cibles : 8 voix Multi × 4 pages de fenêtre stéréo × 2 owners de fenêtre = 64 pages, soit 1 MiB de pages physiques. Le budget Stream de 16 owners reste séparé ; les macros de `sample_page_cache_config.h` et les verrous de `sample_page_cache.c`/`sample_stream_manager.c` doivent être scindés ou explicitement dimensionnés sans amputer Stream. Les 16 slots et filtres synthétiques restent inchangés.

## 8. Risques et invariants

Risques principaux :

- double filtre ou double VCA si le mixer ne reconnaît pas le nouveau format externe ;
- désynchronisation voice ↔ DSP slot après steal ou réutilisation de génération ;
- Note Off par hauteur fermant plusieurs occurrences ;
- libération page-cache avant la fin du reader ou libération DSP avant la fin du VCA ;
- mélange mono/stéréo introduit par un slot mal initialisé ;
- activation d'un filtre EQ3 sans fournir son état mono/stéréo propre ;
- surcharge DTCM en ajoutant le DSP au `brick6_sampler_voice_t` ;
- changement de paramètre qui réinitialise l'historique filtre/enveloppe de toutes les voix ;
- couplage implicite avec `synth_polyphony` et conflit d'owner avec Prism/Stack/Wave/DELUGE ;
- confusion entre les paramètres communs `VOICES/SPREAD` et l'allocator synth, ou routage d'une piste Multi vers les slots synth ;
- dépassement du nouveau budget global de 8 lors d'un partage entre pistes Multi ;
- réduction de `VOICES` laissant des voix hors limite sans politique explicite de steal/release ;
- promotion stéréo oubliée quand `SPREAD > 0`, ou double pan après la somme ;
- steal Multi qui arrête une voix classic/RAM sans préserver son tail existant ;
- one-shot naturel rendu inactif alors que son handle de Note Off reste actif ;
- hypothèse erronée que reverse/ping-pong sont déjà des modes Multi produit.

Invariants à préserver :

- pas d'allocation dynamique, pas de warm-up, pas de création de filtre dans l'IRQ ;
- pools statiques et bornés, nombre de voix indépendant du cache ;
- instrument Multi homogène mono ou stéréo ;
- page physique 16 KiB, budget Multi cible 8 et budget Stream/RAM séparé ;
- aucune piste ne dépasse `clamp(VOICES, 1, 8)` et aucune combinaison de pistes ne dépasse 8 voix Multi actives ;
- `trigger_order`/generation et ownership pages toujours valides ;
- aucun filtre/VCA par voix appliqué une seconde fois après la somme ;
- volume/pan/routing de track, inserts, sends et master conservés dans le même ordre ;
- aucune régression Stream/RAM ;
- aucun changement de `SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET = 16` ; la capacité Multi cible est exactement 8.
- `PARAM_CFG_POLY_VOICES` et `PARAM_CFG_POLY_SPREAD` restent les seuls paramètres communs, en CFG et non p-lockables ;
- `SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET` reste à 16 pour Prism/Stack/Wave/DELUGE ; le Multi reste à 8 ;
- `SPREAD = 0` n'impose pas une promotion stéréo au Multi mono ; `SPREAD > 0` n'ajoute aucun pan après la somme.

## 9. Plan d'action exécutable

Chaque étape est autonome. Un agent exécutant « uniquement l'étape N » s'arrête après cette étape, rend le format demandé et ne commence pas l'étape suivante. Chaque étape produit un commit local dédié, sans push.

### Étape 1 — Figer le contrat de voix Multi

**But.** Documenter dans les interfaces internes l'identité `(voice index, generation)`, les états `FREE/HELD/RELEASE/TERMINAL`, l'appariement exact Note On/Note Off et la matrice de fin source/VCA, sans changer le rendu.

**Périmètre autorisé.** `Inc/Core/brick6_sampler_runtime.h`, `Src/Core/brick6_sampler_runtime.c`, `Inc/Audio/mixer.h`, `Src/Seq/seq_play_scheduler.c`, et un header interne dédié si nécessaire.

**Hors périmètre.** Pool DSP, filtre/VCA par voix, UI, p-lock/MOD, cache, mono/stéréo, implémentation des setters `VOICES/SPREAD`, Stream/RAM.

**Fichiers et symboles.** `brick6_sampler_voice_t`, `g_sampler_multi_voice`, `brick6_sampler_runtime_trigger_multi_note_velocity()`, `brick6_sampler_runtime_note_off_multi_track_note_token()`, `brick6_sampler_runtime_note_off_multi_track_note_all()`, `brick6_sampler_runtime_multi_stop_voice()`, `trigger_order`, `release_pending`, `seq_play_scheduler_emit_local_note()`.

**Modifications attendues.** Écrire le contrat de handle/génération et les transitions ; identifier l'endroit où le scheduler conserve le token runtime non persistant ; décider explicitement one-shot naturel, loop, reverse/ping-pong actuellement désactivés, steal, panic, transport stop, changement d'instrument et réutilisation de génération. Figer le plafond global Multi à 8, sans changer le budget synth de 16. Ne pas changer le comportement audio dans cette étape.

**Invariants.** Note On/Off actuel reste fonctionnel ; limite globale Multi 8 et limite par piste `clamp(VOICES, 1, 8)` ; token runtime absent du format persistant ; aucune page ni voix Stream/RAM modifiée.

**Validations ciblées.** Revue statique des transitions et vérification que deux notes identiques peuvent être représentées sans lookup par hauteur seul. Scénarios de contrat : `VOICES = 1/2/4/8`, piste à 8 voix si le pool est disponible, deux pistes partageant 8 voix, neuvième voix refusée ou vol global selon la politique, réduction déterministe de `VOICES`, augmentation conservant les voix actives, cold start, retrigger, Note Off, one-shot court, loop, reverse/ping-pong explicitement non exposés, saturation à 8.

**Builds.** Aucun build demandé pour cette étape ; si validation de compilation nécessaire, uniquement `Release Low-Cost` et `Release Premium`, jamais `TestPremium`.

**Critère de réussite.** Le contrat indique un handle unique par occurrence, une génération obligatoire et une condition de libération non ambiguë ; aucun code de rendu n'a changé.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 2 — Introduire le pool DSP Multi statique

**But.** Ajouter exactement 8 slots DSP Multi indépendants, sans encore déplacer le rendu final dans ces slots.

**Périmètre autorisé.** `Inc/Core/brick6_sampler_runtime.h`, `Src/Core/brick6_sampler_runtime.c`, nouveaux `Inc/Audio/*`/`Src/Audio/*` strictement dédiés au slot Multi, `Inc/Storage/memory_layout.h` uniquement si un attribut de placement manque, et les fichiers de capacité page-cache/Stream nécessaires (`Inc/Sampler/sample_page_cache_config.h`, `Src/Sampler/sample_page_cache.c`, `Src/Sampler/sample_stream_manager.c`).

**Hors périmètre.** Scheduler, Note Off public, mixer final, UI, paramètres, cache, pool synth, Stream/RAM.

**Fichiers et symboles.** Réutiliser `fx_biquad_filter_t`, `fx_biquad_filter_mono_t`, `fx_dj_eq3_t`, `fx_dj_eq3_mono_t`, `env_adsr_t`, `mixer_track_filter_type_t`, `SAMPLER_MULTI_MAX_GLOBAL_VOICES`, `SAMPLE_PAGE_CACHE_MAX_VOICES`, `SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES`, `SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT`, `SAMPLE_PAGE_WINDOW_LOCK_MAX`, `SAMPLE_STREAM_MAX_ACTIVE`, `trigger_order`. Introduire un type dédié du style `multi_voice_dsp_slot_t` et des helpers init/reset/acquire/release ; le nom final doit rester explicitement Multi.

**Modifications attendues.** Réduire le tableau Multi à 8 cases, créer le pool DSP statique de 8 slots, union mono/stéréo, ownership `(voice index, generation)`, init/reset déterministes, assertions `sizeof`/alignement et mapping voice→slot. Mettre à jour les boucles/scans et la réserve de fenêtres Multi du page-cache vers 8, sans réduire la capacité Stream de 16. Placer le pool en `.ram_d1_audio` aligné 32 ; garder les readers en `.dtcm_audio` à ce stade.

**Invariants.** Aucun slot synth utilisé ; pas de malloc ; aucune création/destruction dans l'IRQ ; homogénéité de format préservée.

**Validations ciblées.** Allocation/libération répétée, steal et réutilisation immédiate avec génération ancienne, 8 slots saturés, tentative d'une neuvième voix, mono et stéréo, deux voix partageant un sample, état filtre/VCA remis à zéro entre générations, réserves page-cache Multi/Stream.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** 8 slots statiques sont disponibles, chaque slot est traçable à une voix/génération, la neuvième voix est refusée, et la mesure `sizeof` exacte est inscrite dans le retour et protégée par assertion.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 3 — Brancher Note On, Note Off et ownership

**But.** Lier chaque Note On à un slot DSP et fermer exactement ce slot sur son Note Off.

**Périmètre autorisé.** `Src/Core/brick6_sampler_runtime.c`, `Inc/Core/brick6_sampler_runtime.h`, `Src/Seq/seq_play_scheduler.c`, interfaces internes de token nécessaires.

**Hors périmètre.** Traitement filtre/VCA audio effectif, ordre mixer, UI, p-lock/MOD, nouveaux modes de lecture.

**Fichiers et symboles.** `brick6_sampler_runtime_trigger_multi_note_velocity()`, `brick6_sampler_runtime_note_off_multi_track_note_token()`, `brick6_sampler_runtime_note_off_multi_track_note_all()`, `brick6_sampler_runtime_multi_stop_voice()`, `brick6_sampler_runtime_multi_stop_track()`, `brick6_sampler_runtime_stop_multi_instrument()`, `brick6_sampler_runtime_reset_track()`, `brick6_sampler_runtime_stop_transport_clips()`.

**Modifications attendues.** Réserver/libérer slot DSP atomiquement avec la voix, transporter generation dans les handles runtime non persistants, appliquer `clamp(VOICES, 1, 8)` par piste et le pool global partagé de 8, corriger le Note Off des notes identiques, voler la plus ancienne voix de la piste à sa limite, appliquer la politique globale de vol ou de refus à saturation, invalider les handles sur steal/panic/stop/instrument change, et préserver owner Stream et pages jusqu'à la fin du reader ou jusqu'à VCA IDLE selon le contrat. Une réduction de `VOICES` évince déterministiquement les voix excédentaires ; une augmentation conserve les voix actives et ouvre la capacité aux prochains Note On.

**Invariants.** Le reader actuel produit le même signal ; un Note Off refusé ne ferme aucune autre voix ; changement d'instrument ne laisse aucun owner actif.

**Validations ciblées.** Notes superposées, deux notes identiques, zones note/vélocité différentes, retrigger, `VOICES = 1`, `VOICES = 2/4/8`, voice steal same-track/global, deux pistes Multi partageant 8 slots, tentative d'une neuvième voix, réduction/augmentation de `VOICES`, panic, transport stop, changement d'instrument, key/génération réutilisés.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** Chaque Note Off ferme une seule occurrence ; `VOICES = 8` autorise 8 voix sur une piste si le pool les fournit ; aucune neuvième voix n'est active ; et aucun slot/page/owner ancien n'est observable après réutilisation de sa génération.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 4 — Filtre indépendant mono/stéréo par voix

**But.** Déplacer le filtre et son envelope dans le slot de chaque voix avant sommation.

**Périmètre autorisé.** Slot DSP Multi, `Src/Core/brick6_sampler_runtime.c`, `Src/Audio/mixer.c` uniquement pour extraire/réutiliser les helpers nécessaires, `Src/Core/brick6_audio_runtime.c` pour le format de publication.

**Hors périmètre.** VCA par voix, inserts, routing musical, refonte p-lock/MOD, cache.

**Fichiers et symboles.** `brick6_sampler_render_multi()`, `brick6_sampler_runtime_render_multi_track()`, `mixer_track_filter_process_block_mono()`, `mixer_track_filter_process_block()`, `mixer_poly_filter_sync_config()`, `mixer_track_filter_type_t`.

**Modifications attendues.** Appliquer OFF/LP/HP/BP/EQ3 à chaque reader avec l'état correspondant ; gérer mono natif et stéréo liée ; propager la configuration versionnée du track ; conserver le smoothing ; accumuler seulement après filtrage. Préparer l'interface de projection `SPREAD` sans pan individuel après la somme.

**Invariants.** Une seule histoire DSP par voix ; pas de filtre track supplémentaire pour la somme Multi ; mono/stéréo homogène ; inserts restent post-somme.

**Validations ciblées.** Multi mono une voix/plusieurs voix, Multi stéréo plusieurs voix, cutoff/résonance/type live, p-lock filtre, changement de type, deux voix même sample avec historiques distincts, aucun filtre post-somme par erreur.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** Deux voix identiques avec cutoff ou historique différent ne se contaminent pas ; le coût et le nombre d'appels filtre sont proportionnels aux voix actives.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 5 — VCA par voix et libération différée

**But.** Appliquer ADSR/gate/release à chaque voix et supprimer la coupure fondée sur le VCA track partagé.

**Périmètre autorisé.** Slot DSP Multi, `Src/Core/brick6_sampler_runtime.c`, `Src/Core/brick6_audio_runtime.c`, `Src/Audio/mixer.c` seulement pour bypass explicite du VCA Multi déjà traité.

**Hors périmètre.** Nouveaux effets par voix, pan individuel, nouveau séquenceur, changement de paramètres ADSR.

**Fichiers et symboles.** `mixer_track_vca_requires_source()`, `mixer_track_vca_note_on()`, `mixer_track_vca_note_off()`, `brick6_sampler_runtime_multi_stop_voice()`, `brick6_sampler_render_multi()`, `brick6_sampler_runtime_render_multi_track()`.

**Modifications attendues.** Note On = attaque/decay/sustain propre ; Note Off = gate off propre ; reader et VCA avancent indépendamment ; si le VCA devient IDLE, arrêter et libérer immédiatement reader/pages/owner/slot ; si le reader atteint EOF avant le VCA, libérer les données audio mais conserver l'état DSP jusqu'à VCA IDLE ; one-shot naturel, sample court, release long, loop et steal suivent la matrice de l'étape 1.

**Invariants.** Source jamais coupée simplement parce qu'une autre voix a fini sa release ; arrêt forcé reste immédiat ; aucun slot ne fuit après release ; VCA track Multi n'est pas réappliqué.

**Validations ciblées.** Note Off indépendant, releases de durées différentes, one-shot avant/après Note Off, release avant EOF, EOF avant release, reader/pages libérés avant VCA, loop, reverse/ping-pong explicitement rejetés tant que non supportés, voice steal, panic, transport stop.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** Une voix en release continue exactement jusqu'à sa fin sans être coupée par une autre voix ; la libération source/DSP/page respecte les deux conditions du contrat.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 6 — Publication et mixer de track

**But.** Finaliser le chemin mono/stéréo : somme Multi déjà traitée par voix, puis uniquement track volume/pan/routing/inserts/sends/master.

**Périmètre autorisé.** `Src/Core/brick6_audio_runtime.c`, `Src/Audio/mixer.c`, `Inc/Audio/mixer.h`, `Src/Core/brick6_sampler_runtime.c`.

**Hors périmètre.** Réorganisation des inserts, nouveaux effets, modification de la géométrie des pages, refonte générale du mixer.

**Fichiers et symboles.** `brick6_render_sampler_tracks()`, `mixer_begin_external_mono_native()`, `mixer_begin_external_stereo()`, `mixer_process()`, `mixer_lane_run_mono_native_path()`, `mixer_lane_run_stereo_path()`, `mixer_lane_accumulate_external_source()`, `mixer_track_filter_process_block_mono()`.

**Modifications attendues.** Introduire un marquage externe « Multi poly déjà filter/VCA » ou un chemin dédié ; bypasser exactement le filtre/VCA track pour ce format ; appliquer `SPREAD` avant la somme, avec promotion stéréo uniquement si nécessaire ; conserver le pan principal de track, gain/pan/mute, inserts, sends, routing et bus dans l'ordre actuel ; ne pas appliquer le pan individuel du chemin synth après la somme.

**Invariants.** Multi mono reste mono jusqu'au pan track ; Multi stéréo reste stéréo ; aucun second filtre/VCA ; Stream/RAM et synth poly inchangés.

**Validations ciblées.** Signal avant/après somme, gain/pan/mute, inserts, sends, master, routing CUE/MASTER, Multi mono avec `SPREAD = 0`, Multi mono avec spread actif et promotion stéréo, Multi stéréo avec spread, voice steal avec placement cohérent, vérification des chemins Stream/RAM et des synths poly.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** Le signal Multi traverse une seule fois le filtre/VCA par voix et une seule fois le gain/pan/inserts/sends de track ; aucun traitement musical n'est déplacé.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 7 — Paramètres partagés et p-locks sans refonte

**But.** Propager les valeurs courantes et les p-locks aux slots actifs selon la politique versionnée, et router `VOICES`/`SPREAD` vers le Multi, sans modifier le système p-lock/MOD.

**Périmètre autorisé.** `Src/Param/param_registry.c`, `Src/Param/param_registry_backends.c`, `Src/Param/param_registry_apply_wrappers.c`, `Src/Core/track_runtime.c`, `Src/Core/brick6_sampler_runtime.c`, `Src/Audio/mixer.c`, headers des setters existants.

**Hors périmètre.** Nouveaux paramètres, nouvelle modulation, modification de la persistance Pattern/Project, remapping de p-locks, second paramètre Multi.

**Fichiers et symboles.** `PARAM_CFG_POLY_VOICES`, `PARAM_CFG_POLY_SPREAD`, `param_registry_apply_track_value()`, `param_registry_apply_track_value_rt_fast()`, `track_runtime_get_play_voice_count()`, `track_runtime_get_play_voice_count_from_descriptor()`, `synth_polyphony_get_voice_pan()`, `synth_polyphony_set_voice_count()`, `synth_polyphony_set_spread()`, `mixer_set_track_filter_*()`, `mixer_set_track_vca_*()`, `mixer_poly_filter_sync_config()`, `param_backend_apply_tone_sampler()`, `PARAM_FILTER_*`, `PARAM_VCA_*`, `PARAM_SAMPLER_MULTI_*`.

**Modifications attendues.** Réutiliser les IDs, l'encodage, les bornes 1..8/0..1, l'affichage et le stockage CFG non p-lockable de `VOICES`/`SPREAD` ; router leur setter/getter vers une configuration Multi distincte sans consommer les slots synth ; appliquer `clamp(VOICES, 1, 8)` par piste et le pool global de 8 ; calculer les positions de spread avec la formule `synth_polyphony_get_voice_pan()` ; définir un index/rang stable, réindexer de manière déterministe les voix survivantes lors d'un changement de nombre de voix et appliquer la politique d'éviction documentée aux voix excédentaires lors d'une réduction ; une augmentation conserve les voix actives et ouvre les rangs libres pour les prochains Note On ; appliquer un changement de spread sur les voix actives sans reset. `param_registry_apply_track_value_rt_fast()` continue de traiter ces CFG comme non p-lockables ; les setters/getters normaux partagent les IDs mais ciblent l'état Multi quand le track est Multi, sans toucher aux slots synth. Pour le filtre/VCA, définir une version de configuration par track ; copier à l'initialisation Note On ; propager au bloc uniquement si version changée ; préserver l'historique filtre/enveloppe ; documenter p-lock sur notes déjà actives et nouvelles notes.

**Invariants.** Une seule source d'autorité pour les paramètres ; aucun changement d'ID ou de format ; MOD et p-locks existants gardent leur sémantique.

**Validations ciblées.** `VOICES = 1/2/4/8`, réduction et augmentation de `VOICES` pendant des notes, `SPREAD = 0`, spread faible/moyen/maximal, changements de spread sur voix actives, vérification mono-native/promotions stéréo, cutoff pendant plusieurs notes, ADSR pendant plusieurs notes, p-lock de filtre, changement de type, p-lock simultané Note On, voix nouvellement créées et voix déjà actives, absence de régression des paramètres synth.

**Builds.** `Release Low-Cost` et `Release Premium` seulement ; pas de `TestPremium`.

**Critère de réussite.** Les mêmes valeurs de track atteignent toutes les voix conformément au contrat, sans recopier de grosse configuration inchangée ni partager d'historique DSP.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 8 — Validation exhaustive du cycle de vie

**But.** Fermer les scénarios Multi mono/stéréo, zones, playback, ownership et absence de régression.

**Périmètre autorisé.** Tests et diagnostics Multi existants, assertions de runtime, documentation technique strictement liée au chantier ; `Src/Core/brick6_sampler_runtime.c` seulement pour diagnostics ciblés.

**Hors périmètre.** Optimisation SD, cache opportuniste, interpolation, Sampler Group, effets par voix, changement de budget, migration historique.

**Fichiers et symboles.** Diagnostics `brick6_sampler_runtime_diag_snapshot_t`, `brick6_sampler_runtime_get_health_snapshot()`, `brick6_sampler_runtime_multi_*`, `sample_stream_manager_*`, `track_runtime_*`.

**Modifications attendues.** Couvrir tous les scénarios obligatoires et ajouter des assertions de génération, format, budget 8, `VOICES`, `SPREAD` et libération. Reverse/ping-pong doivent soit être réellement supportés par un play plan Multi explicitement autorisé, soit rester refusés de manière déterministe et documentée ; ils ne doivent jamais être activés implicitement.

**Invariants.** Aucun warm-up, aucune dépendance au cache pour le nombre de voix, aucun owner/page orphelin, aucune régression Stream/RAM, cold start déterministe.

**Validations ciblées.** Multi mono une voix, mono plusieurs voix, stéréo plusieurs voix, `VOICES = 1` avec comportement monophonique et steal, `VOICES = 2/4/8`, deux pistes Multi partageant le budget global de 8, tentative d'une neuvième voix, réduction/augmentation de `VOICES` pendant des notes, notes superposées, Note Off indépendant, releases différentes, one-shot, loop, reverse, ping-pong, pitch, zones note/vélocité, sample partagé, steal conservant un placement stéréo cohérent, retrigger, panic, transport stop, cutoff/ADSR live, p-lock, sample terminé avant release, release avant sample, changement instrument, réutilisation key/génération, cold start, `SPREAD = 0`, spread faible/moyen/maximal, Multi mono avec spread nul, Multi mono avec spread actif et promotion stéréo, Multi stéréo avec spread, paramètres `VOICES/SPREAD` synth inchangés, filtres/VCA sans double application.

**Builds.** Builds autorisés uniquement `Release Low-Cost` et `Release Premium` ; ne jamais demander `TestPremium`.

**Critère de réussite.** Tous les scénarios ont un résultat attendu documenté et passent ; les compteurs de voix/owners/pages reviennent à zéro après arrêt complet ; la limite globale 8, la limite par piste `clamp(VOICES, 1, 8)` et les politiques de vol/refus restent déterministes ; le budget synth 16 n'est pas régressé.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

### Étape 9 — Nettoyage, mesure linker et documentation finale

**But.** Mesurer l'empreinte RAM via `sizeof` et linker, retirer les chemins morts et consigner les invariants finaux.

**Périmètre autorisé.** Sources Multi/mixer touchées, assertions `sizeof`, map/rapport mémoire, `docs/plan_multi_polyphony.md` et documentation directement concernée.

**Hors périmètre.** Nouvelle architecture générale du mixer, Sampler Group, hausse de polyphonie, cache, Stream/Multi mono finalisé, TestPremium.

**Fichiers et symboles.** `multi_voice_dsp_slot_t`, `g_sampler_multi_voice`, pool DSP Multi, `g_poly_filters_hot`, `mixer_process_external_poly_voice()`, `mixer_process()`, macros `AUDIO_HOT`/`AUDIO_WARM`.

**Modifications attendues.** Inscrire `sizeof` exact mono/stéréo, total 8 slots, delta RAM DTCM/D1/D2/SDRAM avant/après, capacité finale globale de 8 et capacité par piste `clamp(VOICES, 1, 8)`, suppression des chemins morts, recherches négatives (double filtre/VCA, 9e voix, second paramètre Multi, modification du budget synth) et documentation des dettes reverse/ping-pong si non activées. Aucune mesure CPU matérielle n'est demandée à l'agent ; la mesure IRQ mono/stéréo et selon `VOICES/SPREAD` sera effectuée sur matériel par l'utilisateur.

**Invariants.** Aucun changement fonctionnel hors contrat ; pas de push ; builds seulement Release Low-Cost/Premium.

**Validations ciblées.** Comparaison mono/stéréo, `VOICES = 1/2/4/8`, une piste à 8 voix, plusieurs pistes partageant les 8 slots, réduction/augmentation de `VOICES`, recherche d'une neuvième voix, `VOICES/SPREAD`, filtres OFF/biquad/EQ3, releases longues, steal, map/linker et inspection de l'empreinte RAM.

**Builds.** `Release Low-Cost` et `Release Premium` uniquement ; pas de `TestPremium`.

**Critère de réussite.** Les `sizeof` et la RAM linker avant/après sont consignés, le placement est justifié, la capacité finale est 8 slots globaux avec une limite par piste `clamp(VOICES, 1, 8)`, les chemins morts sont supprimés et aucun chemin paraphonique ou double filtre/VCA n'est laissé par inadvertance. La CPU IRQ reste une mesure matérielle utilisateur, hors obligation de l'agent.

**Retour attendu.** `1. Verdict 2. Patch 3. Tests/builds 4. Mémoire ou comportement 5. Dette restante 6. Commit`.

**Commit.** Commit local dédié, message précis, sans push.

## 10. Dettes repoussées

- reverse et ping-pong Multi : à activer uniquement avec un contrat de play plan et des contrôles validés ;
- budget Multi supérieur à 8 ou changement du contrat `clamp(VOICES, 1, 8)` : décision produit séparée ;
- généralisation complète du pool DSP au futur Sampler Group : préparer les helpers et l'ownership, sans mutualiser prématurément ;
- pan individuel par voix : hors contrat actuel ;
- effets par voix : hors périmètre ;
- refonte générale de `mixer.c` et extraction de tous ses helpers : seulement si la duplication mesurée devient un problème ;
- optimisation interpolation/SD/cache : hors périmètre ;
- migration historique de projets et changement de format : hors périmètre ;
- refonte p-lock/MOD, nouveau séquenceur, changement de pages/pré socles/fenêtres : hors périmètre.

## 11. Format obligatoire des retours d'étape

```text
1. Verdict
2. Patch
3. Tests/builds
4. Mémoire ou comportement
5. Dette restante
6. Commit
```

Le retour doit mentionner les builds réellement exécutés, leur variante (`Release Low-Cost` ou `Release Premium`), la taille des nouveaux slots, le nombre de voix actives testé et tout scénario non applicable. L'agent s'arrête après l'étape demandée.
