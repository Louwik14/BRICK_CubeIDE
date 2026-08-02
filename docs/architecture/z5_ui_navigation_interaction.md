# Z5 — Navigation et interaction UI

La piste active est toujours un index `0..7`. STEP 1 à 8 sont ses seules touches de sélection. STEP 9 à 16 sont contextuels et ne créent aucune identité logique.

Les ensembles de piste `CFG`, `ENV`, `TONE`, `MOD`, `MIX`, `PLAY` et `MIDI FX` projettent le moteur du slot actif. Les pages indisponibles sont filtrées par capacités, sans branches Play/Special.

`SHIFT + STEP 16` ouvre prioritairement la page Master globale, y compris sur Low-Cost, sans modifier la piste active. Cette page expose reverb, delay et compresseur; elle n'a ni séquence, mute, clipboard de piste ou Undo. Settings reste accessible par son bouton dédié.

Les clipboards Track/Page/Ensemble transportent des états de slots. Un collage Track conserve exactement l'entrée External demandée et échoue en cas de conflit au lieu de déplacer ou normaliser un propriétaire.
