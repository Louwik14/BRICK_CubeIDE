# Z5 - Navigation et interaction UI

La selection UI distingue la piste top-level active `0..7` de la lane
selectionnee. En contexte GROUP, STEP 9 a 16 selectionnent les huit identities
children `8..15`.

CFG, ENV, TONE, MIX et PLAY utilisent la lane selectionnee. MOD derive son
owner de `entity_topology`: depuis un child il edite l'ensemble partage du
GROUP master. Le master expose TONE, ENV, MOD et MIX, masque PLAY et ne propose
jamais MIDI FX. Les pages indisponibles sont filtrees par les capacites runtime.

`SHIFT + STEP 16` ouvre la page Master globale sans modifier la selection.
Cette page expose reverb, delay et compresseur; elle n'a ni sequence, mute,
clipboard de piste ou Undo.

Les clipboards transportent des etats logiques, jamais le runtime. Copier le
GROUP master capture le master et ses huit children; copier un child reste une
operation locale. Un collage MIDI FX applique MODEL avant ses parametres. Un
collage Track conserve l'entree External demandee et echoue en cas de conflit.
