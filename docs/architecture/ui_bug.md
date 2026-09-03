# Auditer régression UI historique

> # BRICK — AUDIT RÉGRESSION UI — WIDGETS CUSTOM / PLAY / TRACK CFG / FAMILY-SPECIFIC UI
>
> Audit uniquement.
>
> Aucun patch.
> Aucun build lourd.
> Aucune nouvelle documentation.
>
> ## Symptômes utilisateur
>
> Depuis les gros refactors / mono-target / clean architecture :
>
> - plusieurs widgets custom ont disparu ;
> - l’ensemble/page `PLAY` ne présente plus certaines UI custom attendues ;
> - l’ensemble `TRACK CFG` a perdu les widgets spécifiques qui dépendaient :
>   - de la family de track ;
>   - du type de track ;
>   - éventuellement du moteur / sous-type ;
> - l’UI paraît avoir été aplatie vers des widgets génériques ;
> - il peut exister d’autres régressions visuelles ou fonctionnelles du même genre.
>
> L’UI historique BRICK n’est PAS à redessiner.
>
> Le but est :
>
> > retrouver exactement ce qui existait avant les refactors et identifier ce qui a disparu, été bypassé, remplacé ou rendu inaccessible.
>
> ---
>
> # 1. CONTRAT
>
> Ne propose aucun nouveau design.
>
> Ne remplace pas les widgets custom par des widgets génériques “plus simples”.
>
> Ne considère pas qu’un widget supprimé était inutile simplement parce que le code actuel compile.
>
> L’UI historique validée est la référence.
>
> ---
>
> # 2. COMPARER AVANT / APRÈS LES GROS REFACTORS
>
> Retrouver le dernier état fonctionnel avant les refactors ayant pu toucher :
>
> - UI routing ;
> - page templates ;
> - track family/type dispatch ;
> - param registry ;
> - track runtime ;
> - mono-target ;
> - CONTROL/UI split ;
> - cleanup dead code.
>
> Comparer avec l’état actuel.
>
> Chercher particulièrement autour des commits/refactors qui ont :
>
> - factorisé les pages ;
> - généricisé les widgets ;
> - supprimé des `switch(family/type)` ;
> - déplacé des param descriptors ;
> - supprimé des callbacks/renderers spécialisés ;
> - changé `track_runtime` / `param_registry`;
> - modifié les page templates ;
> - supprimé des fichiers considérés legacy/dead.
>
> ---
>
> # 3. AUDIT DE L’ENSEMBLE PLAY
>
> Reconstituer l’UI historique de `PLAY`.
>
> Lister :
>
> - pages ;
> - groups ;
> - widgets ;
> - variations selon track family/type ;
> - widgets custom ;
> - conditions d’affichage ;
> - paramètres pilotés ;
> - handlers encodeurs/boutons correspondants.
>
> Puis comparer à l’état actuel.
>
> Pour chaque élément disparu :
>
> 1. nom/fichier historique ;
> 2. condition d’apparition ;
> 3. rôle ;
> 4. paramètre/état affiché ;
> 5. état actuel :
>    - supprimé ;
>    - renderer mort ;
>    - condition jamais vraie ;
>    - remplacé par générique ;
>    - callsite perdu ;
>    - param metadata perdue ;
>    - autre.
>
> ---
>
> # 4. AUDIT TRACK CFG
>
> Priorité élevée.
>
> Retrouver l’ancien comportement de l’ensemble `TRACK CFG`.
>
> Il existait des widgets spécifiques selon :
>
> - track family ;
> - track type ;
> - moteur ;
> - capacités réelles de la track.
>
> Reconstituer la matrice historique complète.
>
> Exemple de sortie attendue :
>
> | Family | Type | Page/group CFG | Widgets custom historiques | État actuel |
> | ------ | ---- | -------------- | -------------------------- | ----------- |
>
> Inclure toutes les familles/types actuels.
>
> Ne te limite pas à SYNTH.
>
> Chercher :
>
> - SAMPLE ;
> - STREAM ;
> - MULTI ;
> - DRUM ;
> - SYNTH ;
> - GROUP ;
> - EXT/MIDI si applicable ;
> - autres familles présentes.
>
> ---
>
> # 5. CHERCHER LES WIDGETS CUSTOM DISPARUS
>
> Recherche FROM SCRATCH dans le code/historique :
>
> - custom widget ;
> - renderer spécialisé ;
> - draw helper ;
> - page-specific widget ;
> - family-specific renderer ;
> - type-specific renderer ;
> - engine-specific UI ;
> - enum widget type ;
> - callback render ;
> - conditional layout.
>
> Chercher notamment les fonctions/fichiers devenus :
>
> - non référencés ;
> - supprimés ;
> - remplacés ;
> - derrière une condition impossible ;
> - non enregistrés dans la page actuelle.
>
> Pour chaque widget historique custom :
>
> - encore présent et accessible ;
> - présent mais dead ;
> - supprimé ;
> - remplacé ;
> - comportement perdu.
>
> ---
>
> # 6. PAGE TEMPLATE / DISPATCH
>
> Auditer les couches qui choisissent quoi afficher.
>
> Chercher :
>
> - `switch (family)`
> - `switch (type)`
> - track capabilities ;
> - page template selection ;
> - widget descriptor generation ;
> - param group generation ;
> - conditional page group ;
> - renderer dispatch.
>
> Question centrale :
>
> > le refactor a-t-il remplacé une sélection UI spécifique par une projection générique qui ne connaît plus suffisamment la family/type ?
>
> Identifier le premier point où l’information family/type est perdue ou ignorée.
>
> ---
>
> # 7. PARAM REGISTRY / METADATA
>
> Certains widgets custom peuvent avoir disparu parce que les métadonnées permettant de les sélectionner ont été supprimées pendant le clean Param.
>
> Auditer :
>
> - metadata UI ;
> - param rules ;
> - display type ;
> - widget type ;
> - ranges ;
> - enums ;
> - family/type ownership ;
> - page/group assignment.
>
> Comparer avant/après.
>
> Ne restaure pas de metadata morte sans démontrer qu’elle servait réellement l’UI historique.
>
> ---
>
> # 8. RENDERERS ENCORE PRÉSENTS MAIS INACCESSIBLES
>
> Chercher les renderers/widgets spécialisés toujours compilés mais sans callsite.
>
> Pour chacun :
>
> - ancien caller ;
> - caller actuel ;
> - raison de la rupture ;
> - symbole toujours linké ou non.
>
> Classer :
>
> - `DEAD BY REGRESSION`
> - `LEGITIMATELY REMOVED`
> - `STILL ACTIVE`
>
> ---
>
> # 9. AUTRES RÉGRESSIONS UI À Ratisser
>
> Ne te limite pas à PLAY et TRACK CFG.
>
> Comparer les ensembles/pages principaux :
>
> - TONE ;
> - ENV ;
> - PLAY ;
> - MOD ;
> - MIX ;
> - CFG ;
> - TRACK CFG ;
> - REC ;
> - FX ;
> - FILTER ;
> - keyboard ;
> - seq ;
> - settings ;
> - browser ;
> - waveform ;
> - preview ;
> - calibration ;
> - project/pattern/patch.
>
> Chercher :
>
> - widget custom disparu ;
> - page group disparu ;
> - mauvais widget générique ;
> - mauvais ordre ;
> - valeur/label perdu ;
> - élément family-specific devenu commun ;
> - élément type-specific devenu inaccessible.
>
> Ne rapporte pas les différences purement cosmétiques sans perte fonctionnelle ou de design validé.
>
> ---
>
> # 10. UI LOGIC VS RENDER ONLY
>
> Pour chaque régression :
>
> déterminer si le problème vient de :
>
> ### A. Rendering uniquement
>
> La donnée/param existe encore mais le bon widget n’est plus utilisé.
>
> ### B. Page/dispatch
>
> Le renderer existe mais la page ne le sélectionne plus.
>
> ### C. Metadata
>
> L’information permettant de choisir le widget a disparu.
>
> ### D. Param/runtime
>
> Le param/état lui-même n’est plus exposé.
>
> ### E. Legacy supprimé légitimement
>
> Ancien widget réellement obsolète.
>
> Je veux cette classification.
>
> ---
>
> # 11. NE PAS CONFONDRE AVEC LE LAG UI
>
> Le lag UI est un chantier séparé.
>
> Ici on audite :
>
> > disparition / aplatissement / régression de structure et widgets.
>
> Ne cherche pas à optimiser la task UI dans ce prompt.
>
> ---
>
> # 12. BUILD/ELF
>
> Pas de build lourd.
>
> Mais vérifier si utile :
>
> - fichiers renderers réellement compilés ;
> - symboles linkés ;
> - source lists ;
> - fichiers supprimés du CMake.
>
> Le simple fait qu’un fichier soit présent dans le repo ne prouve pas qu’il est actif.
>
> ---
>
> # 13. RETOUR ATTENDU
>
> Je veux :
>
> 1. état historique de l’ensemble PLAY ;
> 2. état actuel PLAY ;
> 3. régressions PLAY ;
> 4. matrice historique TRACK CFG par family/type ;
> 5. matrice actuelle TRACK CFG ;
> 6. widgets custom disparus ;
> 7. widgets présents mais inaccessibles ;
> 8. fichiers/renderers supprimés ;
> 9. point exact du refactor responsable ;
> 10. metadata UI éventuellement perdue ;
> 11. autres régressions UI trouvées hors PLAY/CFG ;
> 12. classification pour chaque problème :
>     - `RENDER REGRESSION`
>     - `DISPATCH REGRESSION`
>     - `METADATA REGRESSION`
>     - `PARAM EXPOSURE REGRESSION`
>     - `LEGITIMATE REMOVAL`
>     - `AMBIGUOUS`
> 13. liste des zones UI inspectées et jugées intactes.
>
> Terminer par :
>
> `UI CUSTOM WORKFLOW — REGRESSION MAP COMPLETE`
>
> ou
>
> `UI CUSTOM WORKFLOW — HISTORICAL PROOF INCOMPLETE`
>
> Ne patch rien.

<details><summary>2 previous messages</summary>

> <details><summary>Ran 30 commands</summary>
>
> - Ran `git status --short; git log --oneline --decorate -40; rg --files -g '!Debug/**' -g '!Release/**' | rg '(^|/)(UI|Inc/UI|Src/UI|docs|CMake|Makefile|.*track.*|.*param.*)' | Select-Object -First 300`
> - Ran `Get-ChildItem Src/UI -Recurse -File | Select-Object FullName; Get-ChildItem Inc/UI -Recurse -File | Select-Object FullName; rg -n -i "PLAY|TRACK CFG|TRACK_CFG|track cfg|family|type|renderer|widget|page|group|custom|draw" Src/UI Inc/UI CMakeLists.txt | Select-Object -First 500`
> - Ran `git log --all --date=short --format="%h %ad %s" -- Src/UI Inc/UI CMakeLists.txt | Select-Object -First 160; git log --all --oneline --grep='UI\|ui\|widget\|template\|family\|param\|mono\|refactor\|clean\|legacy' -i | Select-Object -First 200`
> - Ran `git log --all --date=short --format="%h %ad %s" -- Src/UI/pages Src/UI/ui_renderer_template.c Src/UI/ui_widgets.c Inc/UI/ui_page.h Inc/UI/ui_widgets.h | Select-Object -First 250; git log --all --name-status --format="--- %h %s" -- Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_cfg.c Src/UI/Renderer Src/UI/ui_renderer_template.c Src/UI/ui_widgets.c | Select-Object -First 500`
> - Ran `$files=@('Src/UI/pages/ui_page_template_play.c','Src/UI/pages/ui_page_template_cfg.c','Src/UI/ui_template_page.c','Src/UI/ui_renderer_template.c','Src/UI/Renderer/ui_renderer_config_widgets.inc','Src/UI/Renderer/ui_renderer_sampler_wavetable.inc','Src/UI/Renderer/ui_renderer_synth_widgets.inc','Inc/UI/ui_template_page.h','Inc/UI/ui_renderer_template.h','Inc/UI/ui_page.h','Inc/Track/track_types.h','Src/Track/track_catalog.c','Inc/Track/track_catalog.h'); foreach($f in $files){ if(Test-Path $f){ Write-Output "===== $f ====="; Get-Content $f } }`
> - Ran `rg -n "^([a-zA-Z_][^;{]+\{)|UI_TEMPLATE_CUSTOM_WIDGET|ui_renderer_template_(draw|is|resolve|prepare|filter|spectral|lfo|fm)|custom_widget|widget_picker|param_bank" Src/UI/Renderer Src/UI/ui_renderer_template.c Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_cfg.c Inc/UI/ui_template_page.h | Select-Object -First 1000`
> - Ran `git diff --stat 962ce8965 d379ea2b2 -- Src/UI Inc/UI CMakeLists.txt; git diff --stat 2b3272a68 d379ea2b2 -- Src/UI Inc/UI CMakeLists.txt; git diff --stat 7faf318be d379ea2b2 -- Src/UI Inc/UI CMakeLists.txt`
> - Ran `git diff --find-renames 2b3272a68 0b5747bf9 -- Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_cfg.c Src/UI/ui_renderer_template.c Inc/UI/ui_template_page.h Src/UI/Renderer; git diff --find-renames 2b3272a68 0b5747bf9 --stat -- Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_cfg.c Src/UI/ui_renderer_template.c Inc/UI/ui_template_page.h Src/UI/Renderer`
> - Ran `git blame -L 1,380 HEAD -- Src/UI/pages/ui_page_template_cfg.c; git blame -L 1,230 HEAD -- Src/UI/pages/ui_page_template_play.c; git blame -L 1,130 HEAD -- Inc/UI/ui_template_page.h; git blame -L 1,680 HEAD -- Src/UI/Renderer/ui_renderer_config_widgets.inc | Select-Object -First 220`
> - Ran ``foreach($rev in @('2b3272a68','0b5747bf9','9a4a173ba','e7bdd57b8','c220b4d35','d379ea2b2')){ Write-Output "===== $rev CFG picker ====="; git show "$rev`:Src/UI/pages/ui_page_template_cfg.c" 2>$null | Select-String -Context 0,80 -Pattern 'ui_page_template_cfg_pick_custom_widget|ui_page_template_cfg_pick_widget|g_ui_template_cfg_' | Select-Object -First 180; Write-Output "===== $rev PLAY ====="; git show "$rev`:Src/UI/pages/ui_page_template_play.c" 2>$null | Select-Object -First 260 }``
> - Ran `git log --all --date=short --format="%h %ad %s" -S 'return UI_TEMPLATE_CUSTOM_WIDGET_NONE' -- Src/UI/pages/ui_page_template_cfg.c Src/UI/pages/ui_page_template_play.c; git log --all --date=short --format="%h %ad %s" -S 'PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT' -- Src/UI/pages/ui_page_template_play.c; git log --all --date=short --format="%h %ad %s" -S 'PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE' -- Src/UI/pages/ui_page_template_cfg.c; git log --all --date=short --format="%h %ad %s" -S 'ui_page_template_play_pick_custom_widget' -- Src/UI/pages/ui_page_template_play.c`
> - Ran `git show --format=fuller --stat e7bdd57b8; git show --format= --unified=25 e7bdd57b8 -- Src/UI/pages/ui_page_template_cfg.c Src/UI/pages/ui_page_template_play.c Src/UI/ui_renderer_template.c Inc/UI/ui_template_page.h Src/UI/Renderer | Select-Object -First 1200`
> - Ran `git diff e7bdd57b8^ e7bdd57b8 -- Src/UI/pages/ui_page_template_play.c Src/UI/Renderer/ui_renderer_config_widgets.inc Src/UI/Renderer/ui_renderer_synth_widgets.inc Src/UI/Renderer/ui_renderer_filter_adsr.inc Src/UI/Renderer/ui_renderer_mod_widgets.inc Src/UI/Renderer/ui_renderer_sampler_wavetable.inc Src/UI/Renderer/ui_renderer_chrome.inc Src/UI/Renderer/ui_renderer_formatting.inc | Select-Object -First 1800`
> - Ran `$f='Src/UI/Renderer/ui_renderer_synth_widgets.inc'; $lines=Get-Content $f; $start=240; $end=480; for($i=$start;$i -le $end;$i++){('{0,4}: {1}' -f ($i+1),$lines[$i])}; Write-Output '===== template page ====='; $lines=Get-Content Src/UI/ui_template_page.c; for($i=1;$i -le $lines.Count;$i++){ if($lines[$i-1] -match 'param_bank|PARAM_COUNT|virtual_slot|custom_widget|family_resolver|resolve') { $a=[Math]::Max(1,$i-12);$b=[Math]::Min($lines.Count,$i+22); for($j=$a;$j -le $b;$j++){('{0,4}: {1}' -f $j,$lines[$j-1])}; Write-Output '---'} }`
> - Ran `$f='Src/UI/ui_template_page.c'; $l=Get-Content $f; for($i=1;$i -le $l.Count;$i++){ if($l[$i-1] -match 'registry|family_register|family_resolve\('){$a=[Math]::Max(1,$i-15);$b=[Math]::Min($l.Count,$i+45);for($j=$a;$j -le $b;$j++){('{0,4}: {1}' -f $j,$l[$j-1])};Write-Output '---'}}; Write-Output '===== current CFG full relevant lines ====='; $l=Get-Content Src/UI/pages/ui_page_template_cfg.c; for($i=40;$i -le 240;$i++){('{0,4}: {1}' -f ($i+1),$l[$i])}`
> - Ran `git rev-parse e7bdd57b8^; git show -s --format='%h %ad %s' --date=iso e7bdd57b8^; git show -s --format='%h %ad %s' --date=iso 0b5747bf9; git show -s --format='%h %ad %s' --date=iso 2b3272a68; git show -s --format='%h %ad %s' --date=iso 962ce8965; git log --all --oneline --ancestry-path e7bdd57b8^..d379ea2b2 -- Src/UI/pages/ui_page_template_cfg.c Src/UI/pages/ui_page_template_play.c`
> - Ran `git diff e7bdd57b8^ e7bdd57b8 -- Src/UI/pages Src/UI/Renderer Src/UI/ui_renderer_template.c Inc/UI/ui_template_page.h Inc/UI/ui_param.h Inc/Param/param_ids.h Inc/Param/param_registry.h Src/Param/param_spec_catalog.inc | Select-String -Pattern '^diff |^[-+](?![-+])' | Select-Object -First 2500`
> - Ran `$l=Get-Content Src/UI/pages/ui_page_template_cfg.c; for($i=235;$i -le $l.Count;$i++){('{0,4}: {1}' -f $i,$l[$i-1])}; Write-Output '===== current mod ====='; $l=Get-Content Src/UI/pages/ui_page_template_mod.c; for($i=1;$i -le $l.Count;$i++){('{0,4}: {1}' -f $i,$l[$i-1])}; Write-Output '===== current env ====='; $l=Get-Content Src/UI/pages/ui_page_template_env.c; for($i=1;$i -le $l.Count;$i++){('{0,4}: {1}' -f $i,$l[$i-1])}`
> - Ran ``foreach($rev in @('f34aa0ef0','e7bdd57b8','d379ea2b2')){ Write-Output "===== $rev MOD ====="; git show "$rev`:Src/UI/pages/ui_page_template_mod.c" 2>$null | Select-String -Pattern 'family|param_bank|pick_custom|pick_widget|virtual_slot|custom_widget_picker|PARAM_MOD|PARAM_LFO|handle_event' -Context 1,2 | Select-Object -First 300; Write-Output "===== $rev TONE ====="; git show "$rev`:Src/UI/pages/ui_page_template_tone.c" 2>$null | Select-String -Pattern 'param_bank|custom_widget|pick|virtual|family_title|nav_labels' -Context 1,2 | Select-Object -First 250; Write-Output "===== $rev ENV ====="; git show "$rev`:Src/UI/pages/ui_page_template_env.c" 2>$null | Select-String -Pattern 'param_bank|custom_widget|pick|family_title|nav_labels' -Context 1,2 | Select-Object -First 250 }``
> - Ran `git diff -U2 e7bdd57b8^ e7bdd57b8 -- Src/UI/pages/ui_page_template_mod.c Src/UI/pages/ui_page_template_tone.c Src/UI/pages/ui_page_template_env.c Src/UI/pages/ui_page_template_seq.c Src/UI/pages/ui_page_template_keyboard.c Src/UI/pages/ui_page_template_mix.c Src/UI/pages/ui_page_template_midi_fx.c Src/UI/pages/ui_page_audio_rec.c | Select-Object -First 2400`
> - Ran `git diff -U1 e7bdd57b8^ e7bdd57b8 -- Src/UI/pages/ui_page_template_tone.c | Select-String -Pattern '^@@|^[-+].*(custom|CUSTOM|pick|WAVE|FM|SAMPLER|LOOPER|param_bank|virtual|UI_TEMPLATE)' | Select-Object -First 1000; git diff -U1 e7bdd57b8^ e7bdd57b8 -- Src/UI/pages/ui_page_audio_rec.c Src/UI/pages/ui_page_settings.c Src/UI/pages/ui_page_midi_fx.c Src/UI/pages/ui_page_patch_assign.c | Select-String -Pattern '^diff |^@@|^[-+].*(draw|render|widget|page|param|custom|virtual|label)' | Select-Object -First 1000`
> - Ran `rg -n "pick_custom|custom_widget_picker|UI_TEMPLATE_CUSTOM_WIDGET|param_bank|virtual_slot|param_text|resolve_family" Src/UI/pages/ui_page_template_tone.c Src/UI/pages/ui_page_midi_fx.c Src/UI/pages/ui_page_template_keyboard.c Src/UI/pages/ui_page_template_seq.c Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_cfg.c Src/UI/pages/ui_page_template_mix.c`
> - Ran `git diff --name-status --diff-filter=D f34aa0ef0 d379ea2b2 -- Src/UI Inc/UI | Sort-Object; git diff --name-status --diff-filter=D 962ce8965 d379ea2b2 -- Src/UI Inc/UI | Sort-Object; git diff --name-status --diff-filter=D e7bdd57b8^ e7bdd57b8 -- Src/UI Inc/UI | Sort-Object; git diff --name-status --diff-filter=A e7bdd57b8^ e7bdd57b8 -- Src/UI Inc/UI | Sort-Object`
> - Ran `Write-Output '===== current widgets ====='; Get-Content Src/UI/ui_widgets.c; Get-Content Inc/UI/ui_widgets.h; Write-Output '===== historical widgets refs ====='; foreach($rev in @('f34aa0ef0','e7bdd57b8','d379ea2b2')){Write-Output "--$rev"; git ls-tree -r --name-only $rev Src/UI Inc/UI | rg 'widget|render|page' | Select-Object -First 200}`
> - Ran `rg -n "Src/UI|ui_renderer|ui_page_template|ui_widgets|ui_algo|pages" CMakeLists.txt cmake Board Src/UI -g '*.cmake' -g 'CMakeLists.txt' -g '*.c' | Select-Object -First 1200; rg -n "ui_renderer_template_draw|ui_renderer_template_draw_custom_track_cfg|ui_renderer_template_draw_play_note_text|ui_renderer_template_draw_matrix|ui_renderer_template_draw_custom_adsr|ui_renderer_template_draw_filter|ui_renderer_template_draw_lfo|ui_renderer_template_draw_wave|ui_renderer_template_draw_audio_fx_group" Src/UI Inc/UI | Select-Object -First 800`
> - Ran `$kinds=(Get-Content Inc/UI/ui_template_page.h | Select-String 'UI_TEMPLATE_CUSTOM_WIDGET_' | ForEach-Object { if($_ -match 'UI_TEMPLATE_CUSTOM_WIDGET_[A-Z0-9_]+'){ $Matches[0] } }); foreach($k in $kinds){$hits=(rg -n --glob '*.c' --glob '*.inc' $k Src/UI Inc/UI); Write-Output "===== $k ====="; if($hits){$hits}else{'NONE'}}`
> - Ran ``$revs=@('f34aa0ef0','d379ea2b2'); foreach($rev in $revs){Write-Output "===== $rev MIDI FX relevant ====="; $l=(git show "$rev`:Src/UI/pages/ui_page_midi_fx.c" 2>$null); $i=0; foreach($line in $l){$i++; if($line -match 'param_bank|custom_widget|param_text|virtual_slot|AUDIO_FX_|ROUTING|SPATIAL|FILTER POS|MODE'){Write-Output ("{0,4}: {1}" -f $i,$line)}}}``
> - Ran `git diff --stat e7bdd57b8 d379ea2b2 -- Src/UI Inc/UI CMakeLists.txt; git diff -U1 e7bdd57b8 d379ea2b2 -- Src/UI/pages/ui_page_template_cfg.c Src/UI/pages/ui_page_template_play.c Src/UI/pages/ui_page_template_tone.c Src/UI/pages/ui_page_template_env.c Src/UI/pages/ui_page_template_mod.c Src/UI/pages/ui_page_midi_fx.c Src/UI/ui_template_page.c Src/UI/ui_renderer_template.c Src/UI/Renderer | Select-Object -First 2600`
> - Ran `git log --all --date=short --format="%h %ad %s" -S 'ui_page_debug_hall' -- CMakeLists.txt Src/UI Inc/UI; git log --all --date=short --format="%h %ad %s" -S 'ui_core_clipboard' -- CMakeLists.txt Src/UI Inc/UI; git log --all --date=short --format="%h %ad %s" -S 'ui_page_param_test' -- CMakeLists.txt Src/UI Inc/UI; git show --format=fuller --stat d379ea2b2; git show --format=fuller --stat c220b4d35 | Select-Object -First 250`
> - Ran `git show --format=fuller --stat --no-renames d379ea2b2 | Select-Object -First 220; git show --format=fuller --stat --no-renames c220b4d35 | Select-Object -First 220; git log --all --oneline -- Src/UI/pages/ui_page_debug_hall.c; git log --all --oneline -- Src/UI/ui_core_clipboard.c`
>
> </details>
>
> Context compaction
>
> Source: automatic
> Status: completed
</details>

Verdict : audit concluant. Régressions UI custom confirmées, principalement introduites par `e7bdd57b8` (« PASS 4 »), entre l’état historique `f34aa0ef0` et l’état actuel `d379ea2b2`.

Aucun patch, aucun build lourd, aucune documentation modifiée.

## 1. PLAY

| Élément | Historique | Actuel | Diagnostic |
|---|---|---|---|
| Pages | PLAY 1/2, PLAY 2/2 | Identiques | Structure conservée |
| Voix | V1 à V8 | V1 à V8 | Structure conservée |
| Condition | Capacité séquenceur, seconde page si capacité > 4 | Même condition | Fonctionnellement conservé |
| Paramètres | `PARAM_SEQ_PLAY_Vx_NOTE/VEL/LEN/MICTIM` | Slots virtuels `PARAM_COUNT` | Métadonnées supprimées |
| Widget note | `UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE` sur le slot 0 | Aucun custom picker | Disparu |
| Valeurs note | Nom MIDI (`C-3`, etc.) | Valeur numérique brute | Aplatissement visuel |
| Velocity zéro | Affichage `OFF` historique | Valeur numérique | Régression de rendu |
| Édition | Résolution historique du contexte track/voice | Contrôle direct `seq_edit` | Fonction conservée |

Régressions PLAY :

- `PLAY_NOTE` est toujours implémenté dans le renderer mais aucun callsite actuel ne le sélectionne.
- `ui_page_template_play.c` remplit désormais tous les groupes avec `PARAM_COUNT`.
- Le renderer tombe sur `virtual_slot_text()` puis dessine un widget générique.
- Classification : `DISPATCH REGRESSION`, `METADATA REGRESSION`, `RENDER REGRESSION`.
- Aucune `PARAM EXPOSURE REGRESSION` démontrée : note, vélocité, longueur et microtiming restent éditables.

Preuves principales : [ui_page_template_play.c](./Src/UI/pages/ui_page_template_play.c), [ui_renderer_config_widgets.inc](./Src/UI/Renderer/ui_renderer_config_widgets.inc).

## 2. TRACK CFG — matrice historique / actuelle

| Family | Type | CFG historique | Widgets custom historiques | État actuel |
|---|---|---|---|---|
| OFF | NONE | TRACK / MIDI | TRACK + INACTIVE | Custom inaccessible, slots génériques |
| SYNTH | PRISM | TRACK / MIDI | TRACK + TYPE PRISM | Family conservée, custom perdu |
| SYNTH | WAVE | TRACK / MIDI | TRACK + TYPE WAVE | Family conservée, custom perdu |
| SYNTH | STACK | TRACK / MIDI | TRACK + TYPE STACK | Family conservée, custom perdu |
| SYNTH | FM | TRACK / MIDI | TRACK + TYPE FM | Family conservée, custom perdu |
| SAMPLER | RAM | TRACK / MIDI | TRACK + TYPE RAM | Custom perdu |
| SAMPLER | STREAM | TRACK / MIDI | TRACK + TYPE STREAM | Custom perdu |
| SAMPLER | LOOPER | TRACK / MIDI | TRACK + TYPE LOOP | Custom perdu |
| SAMPLER | MULTI | TRACK / MIDI | TRACK + TYPE + VOICES + SPREAD | Voix/spread exposés, custom TRACK/TYPE perdu |
| SAMPLER | GROUP | TRACK / MIDI | TRACK + TYPE GROUP | Custom perdu |
| DRUM | DRUM_MD | TRACK / MIDI | TRACK + TYPE DRUM | Custom perdu |
| DRUM | DRUM_BD_ANALOG | TRACK / MIDI | TRACK + TYPE BD ANALOG | Custom perdu |
| MIDI | MIDI | TRACK / MIDI | TRACK + TYPE MIDI + MIDI CH/SOURCE | Custom perdu |
| EXTERNAL | EXTERNAL | TRACK / MIDI | TRACK + TYPE EXT + INPUT | Input conservé, custom TRACK/TYPE perdu |

Historique :

- `PARAM_CFG_TRACK`
- `PARAM_CFG_TRACK_TYPE`
- `PARAM_CFG_POLY_VOICES`
- `PARAM_CFG_POLY_SPREAD`
- `PARAM_EXTERNAL_INPUT`
- `PARAM_CFG_MIDI_CH`
- `PARAM_CFG_MIDI_SRC`

Actuel :

- la majorité des slots sont `PARAM_COUNT` ;
- les valeurs sont produites par `virtual_slot_text()` ;
- `ui_page_template_cfg_pick_custom_widget()` retourne systématiquement `UI_TEMPLATE_CUSTOM_WIDGET_NONE` ;
- les handlers directs de family/type/input/polyphonie subsistent.

Régressions TRACK CFG :

- `TRACK_CFG_TRACK` inaccessible ;
- `TRACK_CFG_TYPE` inaccessible ;
- `TRACK_CFG_INACTIVE` inaccessible ;
- `TRACK_CFG_MIDI_CHANNEL` inaccessible ;
- `TRACK_CFG_MIDI_SOURCE` inaccessible ;
- icônes et labels family/type remplacés par des widgets génériques ;
- métadonnées `PARAM_CFG_*` supprimées ou remplacées par des slots virtuels.

Classification : `DISPATCH REGRESSION`, `RENDER REGRESSION`, `METADATA REGRESSION`.

Aucune perte certaine de paramètre fonctionnel n’est démontrée : les handlers directs continuent d’exposer les contrôles.

## 3. Widgets custom disparus ou inaccessibles

### Présents mais inaccessibles par régression

- `PLAY_NOTE`
- `TRACK_CFG_TRACK`
- `TRACK_CFG_TYPE`
- `TRACK_CFG_INACTIVE`
- `TRACK_CFG_MIDI_CHANNEL`
- `TRACK_CFG_MIDI_SOURCE`
- `MATRIX_SLOT`
- `MATRIX_SOURCE`
- `LFO_DEST`
- `LFO_DEPTH` pour MATRIX/MULTI/SLEW

Les renderers correspondants sont encore dans :

- `ui_renderer_config_widgets.inc`
- `ui_renderer_mod_widgets.inc`
- `ui_renderer_synth_widgets.inc`

Ils sont inclus dans l’unité monolithique [ui_renderer_template.c](./Src/UI/ui_renderer_template.c), mais les pickers actuels ne les sélectionnent plus. Leur présence dans le dépôt ne prouve donc pas leur accessibilité UI.

### Encore actifs

- ADSR FILTER/VCA/ENV3 ;
- FILTER TYPE / CURVE GROUP ;
- AUDIO FX GROUP ;
- LFO RATE ;
- LFO SHAPE/PHASE GROUP ;
- STACK WAVEFORM ;
- WAVE WAVETABLE ;
- FM PITCH EG GROUP.

## 4. MOD — régression hors PLAY/CFG

Historique :

- MATRIX : slot, source, destination, depth ;
- MULTI/SLEW : sources et amounts spécialisés ;
- widgets `MATRIX_SLOT`, `MATRIX_SOURCE`, `LFO_DEST`, `LFO_DEPTH` ;
- flèches de liaison visuelles.

Actuel :

- groupes MATRIX/MULTI/SLEW virtualisés ;
- valeurs toujours éditables ;
- widgets custom non sélectionnés ;
- `ui_renderer_template_draw_mod_link_arrows()` devenu no-op ;
- depth rendu avec une logique générique différente.

Classification : `RENDER REGRESSION`, `DISPATCH REGRESSION`, `METADATA REGRESSION`.

## 5. Autres pages inspectées

| Zone | Résultat |
|---|---|
| TONE | Templates family/type toujours présents ; widgets WAVE, STACK et FM actifs |
| ENV | ADSR/filter custom actifs |
| FILTER | Aucun custom historique démontré comme disparu |
| MIX | Aucune perte structurelle démontrée |
| FX | Graphes et `AUDIO_FX_GROUP` actifs ; routing virtualisé, rendu générique — `AMBIGUOUS` |
| REC | Contrôles virtualisés mais fonctionnels ; ancienne sélection tempo supprimée — `AMBIGUOUS` |
| SEQ | Contrôles virtualisés, édition conservée ; métadonnées historiques supprimées — `AMBIGUOUS` |
| KEYBOARD | Projection virtuelle fonctionnelle ; aucune perte custom démontrée — `AMBIGUOUS` |
| Settings/browser/project/pattern/patch | Aucune perte custom structurelle démontrée |
| Waveform/preview/calibration | Aucune perte custom structurelle démontrée |

## 6. Premier point responsable

Le premier point de rupture est `e7bdd57b8` :

- remplacement des banques de paramètres réelles par `PARAM_COUNT` ;
- suppression des IDs et descriptors `PARAM_CFG_*`, `PARAM_SEQ_PLAY_*`, `PARAM_MOD_*`, etc. ;
- suppression des custom pickers PLAY/CFG/MOD ;
- remplacement par des slots virtuels et des widgets génériques ;
- suppression de formats spécialisés : noms de notes, `OFF`, labels family/type, marqueurs MIDI ;
- suppression des flèches MOD.

Le resolver global conserve encore `family/type`, mais la page CFG enregistre une projection générique et ne transmet plus assez d’information au dispatch custom. La perte n’est donc pas principalement dans `ui_template_family_resolve()`, mais dans la génération des pages et la disparition des métadonnées/pickers.

## 7. Fichiers supprimés

Fichiers supprimés dans les refactors :

- pages debug/low-cost/param-test ;
- `control_audio_visual.c` ;
- ancien clipboard UI ;
- bridges navigation/runtime UI.

Aucune suppression d’un fichier principal de renderer PLAY ou TRACK CFG n’a été trouvée. Les renderers custom existent encore ; leurs points d’entrée ont disparu ou sont neutralisés.

Classification de ces suppressions : `LEGITIMATE REMOVAL` pour les pages de test/debug, sans preuve qu’elles appartenaient au workflow UI validé.

## 8. Synthèse par classification

- `RENDER REGRESSION` : PLAY note, TRACK CFG family/type/MIDI, MOD matrix/slew/multi.
- `DISPATCH REGRESSION` : custom pickers supprimés ou retournant `NONE`.
- `METADATA REGRESSION` : descriptors et IDs historiques supprimés.
- `PARAM EXPOSURE REGRESSION` : aucune confirmée sur PLAY/CFG ; les contrôles directs subsistent.
- `LEGITIMATE REMOVAL` : pages et outils debug/test.
- `AMBIGUOUS` : REC, SEQ, KEYBOARD et routing FX, virtualisés mais sans preuve complète de design historique validé.

Aucun fichier hors zone n’a été modifié.
Aucune documentation mise à jour : aucune mise à jour doc requise.

UI CUSTOM WORKFLOW — REGRESSION MAP COMPLETE
