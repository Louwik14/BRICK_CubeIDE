# AFX/MIX — Effets, mixer, routing et Master

## Statut

CONFIRME — validation sonore requise.

Chaque piste admissible possede deux slots A/B avec MODEL, P1, P2, P3. Modeles catalogues : OFF, LOFI, FOLD, DRIVE, POINT, SUB, RING, SUB LIGHT, VIBE, DRIFT, XFADE, DJ EQ ; certains indices sont des trous `-`. Les labels/plages P1..P3 sont derives du modele. XFADE cible MASTER, une autre piste, LINE ou USB et refuse self-target ; courbe et cible sont controles types. DJ EQ initialise Low/Mid/High a 0 dB.

Audio FX expose FX A, FX B et ROUTING/SPATIAL ; position de filtre, ordre et modes spatiaux appartiennent a `audio_fx_control_state`. En GROUP, les models appartiennent au master et les children n'exposent que Level A/B ; XFADE est interdit aux pistes ordinaires incompatibles selon le catalogue.

MIX expose mute, niveau, pan et sends. Master 15 expose reverb, delay (Classic/Dual, divisions, feedback, spectral, modulation), compresseur (Off/Deluge/Brick, threshold, ratio, attack, release, makeup, mix, sidechain HPF et caracteres) et Mod FX. Les sends ont un mapping explicite, pas une hypothese d'IDs contigus.

P1..P3 FX sont p-lockables et modulables selon modele ; MODEL est structurel et non p-lockable. Routing/modele/valeurs persistent ; tails, buffers delay/reverb et etats DSP non.

Sources : `Src/UI/pages/ui_page_midi_fx.c`, `ui_page_template_mix.c`, `ui_page_template_tone.c`, `Src/Track/audio_fx_control_state.c`, `Src/Param/audio_fx_param_catalog.c`, `Src/Audio/fx_*`, `mixer.c`.
