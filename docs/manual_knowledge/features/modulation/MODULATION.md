# MOD-01 — LFO, ENV3 et Matrix

## Statut

CONFIRME.

Trois LFO par owner : Rate libre/synchronise, formes SIN/TRI/SAW/SQR/RND et variantes positives/reverse, Trig FREE/TRIG/HOLD/ONE et variantes poly, Phase 0..360 deg. ENV3 est ADSR. MATRIX choisit slot, source, destination et profondeur ; MULTI/SLEW sont des controles structures supplementaires.

Une destination est `{entity,param}` admise par le registre. Le calcul est `clamp(base effective + somme(source * depth normalisee * plage), min, max)`. Depth zero, disable ou source NONE retire la route active sans oublier destination/base. Le premier bind publie la configuration LFO complete. GROUP centralise owner/LFO/ENV3/operateurs sur le master ; une vue child filtre les destinations communes et locales.

Les routes/configurations persistent ; phases, plans compiles et caches ne persistent pas. Matrix se compose apres p-lock et Scene. Les selecteurs structurels, assets et MODEL FM ne sont pas admis sauf endpoints explicitement declares.

Sources : `Src/Mod/mod_*`, `Src/Mod/Matrix/*`, `Src/UI/pages/ui_page_template_mod.c`, `Src/Param/param_registry.c`.
