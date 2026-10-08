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

1. `PLAY`: Sample, Mode, Start, End.
2. `LOOP`: Gain, Tune, Loop Start, Slice Count, avec waveform, markers et
   playhead communs.
3. `SOURCE`: Source, Src BPM, Stretch, emplacement libre.
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

Reverse et ping-pong ne font pas partie du sampler courant. Ils sont retires
jusqu'a une implementation forward/backward native dans VoiceReader et le Page
Cache, sans moteur de secours. Les seuls modes sont `ONE`, `LOOP`, `H.ONE` et
`H.LOOP`.

Recorder et `REC_SOURCE` assignent et relisent leurs takes via le Streamer.
Looper/Overdub conservent leur ring et leurs gros writes asynchrones. Preview
utilise le meme runtime sampler STREAM; Multi conserve ses voix polyphoniques
sur le meme Page Cache et le meme transport physique.

Le codec courant est la version 16. Il ne reconnait aucun format anterieur et
ne contient ni type moteur alternatif, ni migration, ni alias sampler.
