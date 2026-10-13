# Sampler canonique

Le playback sampler BRICK utilise une seule architecture:

```text
Classic / Group / Multi
        -> VoiceReader
        -> Page Cache logique 16 KiB
        -> publication progressive 512 B
        -> fastpath physique 16 / 32 / 48 / 64 KiB
```

Classic et les children du Group partagent le meme runtime STREAM, le meme
cache, le meme DSP et les memes quatre pages TONE:

1. `PLAY`: Sample, Mode, Start, Length.
2. `LOOP`: Gain, Tune, Loop Start, Slice Count, avec waveform, markers et
   playhead communs.
3. `SOURCE`: Source, Src BPM, Stretch, Sync Len.
4. `STR`: Grain, Heads, Window, Dispersion.

`START` et `END` pilotent directement le plan Streamer. Les changements live
restent limites a 12 Hz, latest-wins, avec application immediate sur cache hit
et seek asynchrone sur cible froide. Pitch, loop, slices, p-locks et Matrix
emploient les parametres canoniques du Streamer. La waveform est resolue par
`waveform_service` a partir de l'identite audio et du nombre de frames; elle ne
depend d'aucun backend de chargement particulier.

Le nom produit `Group RAM` peut subsister dans un workflow, mais il ne designe
aucune architecture memoire: ses children sont des Classic STREAM. Une source
deja residente peut etre exploitee par le Streamer comme optimisation; elle ne
cree pas de moteur distinct.

Classic et Group retrouvent l'unique parametre historique `Mode`, avec les
labels et l'ordre exacts `ONE`, `REV.ONE`, `LOOP`, `PINGPONG`, `H.ONE`,
`H.LOOP`, `H.PINGPONG`. Les modes sans `H` suivent le GATE: NOTE_ON lance ou
redeclenche et NOTE_OFF libere la voix. Les modes `H.*` suivent HOLD: NOTE_OFF
est ignore; NOTE_ON remplace la lecture courante, et STOP/PANIC restent
autoritaires. `H.LOOP` est une loop forward hold. Le ping-pong est porte par
`PINGPONG`/`H.PINGPONG` et alterne
entre START et la borne derivee `START + LENGTH`; la loop forward reprend a LOOP START. Le mode est capture
au NOTE_ON, P-lockable, et exclu de la Matrix/modulation continue.

`Sync Len` restaure le contrat historique `Off / 1 bar / 2 bars / 4 bars /
Auto`. Les longueurs explicites calent la region START/LENGTH sur la duree
musicale cible au tempo projet; Auto derive le ratio de `Src BPM`. `Stretch=Off`
neutralise le calage, `Speed` applique un varispeed qui change aussi la hauteur,
et `Shifter` corrige la hauteur. Ces reglages passifs sont pris en compte au
prochain depart/retrigger et n'ajoutent ni quantification de depart ni I/O SD
dans AUDIO.

Reverse et ping-pong tournent dans le VoiceReader canonique. Reverse consomme
les pages de la fin vers le debut, tandis que le backend conserve des lectures
SD physiques croissantes et peut remplir 2/3/4 slots en un burst. Aucun buffer
64 KiB, cache secondaire ou moteur RAM n'est ajoute. Multi conserve son
booleen forward `LOOP ON until release`: au NOTE_OFF il sort de la loop et lit
la tail jusqu'a SAMPLE END.

Recorder et `REC_SOURCE` assignent et relisent leurs takes via le Streamer.
Looper/Overdub conservent leur ring et leurs gros writes asynchrones. Preview
utilise le meme runtime sampler STREAM; Multi conserve ses voix polyphoniques
sur le meme Page Cache et le meme transport physique.

Le codec courant est la version 17. Il ne reconnait aucun format anterieur et
ne contient ni type moteur alternatif, ni migration, ni alias sampler.
