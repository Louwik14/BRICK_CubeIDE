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

`REPLACE FROM SD` est une macro stricte `DELETE -> EMPTY -> LOAD`. DELETE coupe
immediatement toutes les voix de la ressource, annule les seeks live, attend la
fence AUDIO et la disparition des leases, puis libere cache et Stream Manager.
Le LOAD cooperatif ne publie qu'apres prefill complet. Il n'existe ni ancien
asset musical conserve, ni rollback, ni drainage naturel; un echec laisse le
slot vide. Une note encore tenue n'est pas retriggee et ne rearme ni VCA ni
filtre: seul le prochain NOTE_ON reel utilise la nouvelle ressource.

La selection sampler est un etat explicite `OFF | ASSET(logical_slot)`, commun
au catalogue CONTROL, au selecteur UI, au DTO PreparedAudio et a la commande
CONTROL vers AUDIO. `OFF` est l'entree zero; S0 est le premier asset et ne sert
jamais de sentinelle. La commande AUDIO encode distinctement `OFF` et
`ASSET(runtime_id)`, y compris pour le runtime 0. Toute transition OFF vers
asset, premier chargement compris, publie le binding et valide le runtime sans
recreer les renderers tenus. La transition vers `OFF` retire voix et lease sans
recreer de voix. Une reference persistante introuvable reste un asset selectionne
mais indisponible; sa projection AUDIO effective est `OFF`, ce qui la distingue
d'un OFF choisi par l'utilisateur.

Les samples et zones Multi occupent des slots globaux disperses. Chaque
instrument enumere explicitement ses IDs; chaque zone reference directement son
`multi_sample_id`. DELETE rend exactement ces slots au pool apres fence. Les
IDs restent stables pendant READY et leur reutilisation cree obligatoirement un
nouvel epoch Page Cache. Il n'existe plus de plage `first + index`, de
fragmentation externe ni de compaction runtime.

`Sync Len` restaure le contrat historique `Off / 1 bar / 2 bars / 4 bars /
Auto`. Les longueurs explicites calent la region START/LENGTH sur la duree
musicale cible au tempo projet; Auto derive le ratio de `Src BPM`. `Stretch=Off`
neutralise le calage, `Speed` applique un varispeed qui change aussi la hauteur,
et `Shifter` corrige la hauteur. Ces reglages passifs sont pris en compte au
prochain depart/retrigger et n'ajoutent ni quantification de depart ni I/O SD
dans AUDIO.

Huit slots Shifter musicaux sont attribues dynamiquement aux tracks STREAM/GROUP
actives. Ils ne sont ni reserves par track ni ajoutes au budget des huit
VoiceReaders musicaux partage avec Multi. Leurs huit historiques stereo de
64 KiB forment une arene contigue de 512 KiB en SDRAM cacheable; les petits
etats et scratchs restent en D1.

Le reset d'un slot Shifter est logique: l'index d'ecriture et le nombre de
frames valides repartent de zero. Pendant le remplissage, toute interpolation
visant une frame plus ancienne que l'historique valide lit zero. Aucun
effacement des 64 KiB n'est donc execute au NOTE_ON, au retrigger ou au stealing
dans l'IRQ AUDIO; apres 8192 frames, l'ancien contenu a ete totalement remplace.

La comparaison des symboles Release de cette refonte est la suivante. Les
colonnes ne comptent que les objets Shifter/Preview modifies, pas les autres
occupants des regions:

| Region | Avant | Apres | Delta |
|---|---:|---:|---:|
| DTCM | 0 | 0 | 0 |
| D1 (slots, scratchs, Preview) | 69 080 | 5 332 | -63 748 |
| D2 | 0 | 0 | 0 |
| D3 | 0 | 0 | 0 |
| SDRAM cacheable historique | 131 072 | 524 288 | +393 216 |
| SDRAM Recorder Ring | 32 768 | 0 | -32 768 |
| SDRAM Recorder non-cacheable | 98 304 | 0 | -98 304 |
| SDRAM, total | 262 144 | 524 288 | +262 144 |
| RAM, total | 331 224 | 529 620 | +198 396 |

L'etat d'un slot reste de 612 octets (Shifter 96, deux scratchs de 256,
ownership/padding); huit slots occupent 4 896 octets. Le runtime Preview passe
de 1 096 a 436 octets et son historique de 65 536 octets disparait. Le MAP
Release apres refonte mesure `.audio_history_sdram=606 208`, dont exactement
524 288 octets pour `g_sampler_clip_shifter_history`; DTCM/D2/D3 n'ont aucun
delta attribuable a cette refonte.

La mesure historique a 48 kHz d'un Shifter a -12 demi-tons faisait passer la
charge IRQ AUDIO de 4,8 % a 6,8 % (environ +2 points pour une voix). Elle ne
constitue pas une preuve pour huit voix: une extrapolation lineaire donnerait
environ +16 points, auxquels peuvent s'ajouter les conflits de lignes D-cache
et la contention SDRAM avec le Streamer. L'arene est exclusivement lue/ecrite
par le CPU, donc aucune maintenance D-cache n'est requise. Le NOTE_ON groupe est
desormais borne a l'initialisation du petit etat; le risque residuel a fermer
sur cible est le pic IRQ/SDRAM de huit Shifters actifs, en particulier a -12 st.

Reverse et ping-pong tournent dans le VoiceReader canonique. Reverse consomme
les pages de la fin vers le debut, tandis que le backend conserve des lectures
SD physiques croissantes et peut remplir 2/3/4 slots en un burst. Aucun buffer
64 KiB, cache secondaire ou moteur RAM n'est ajoute. Multi conserve son
booleen forward `LOOP ON until release`: au NOTE_OFF il sort de la loop et lit
la tail jusqu'a SAMPLE END.

Recorder et `REC_SOURCE` assignent et relisent leurs takes via le Streamer.
Looper/Overdub conservent leur ring et leurs gros writes asynchrones. Preview
sample utilise le VoiceReader reserve Preview et le meme Page Cache, mais un
runtime minimal: lecture one-shot du sample complet a vitesse/hauteur d'origine,
sans Shifter, historique, pitch/stretch, modulation, DSP de voix ni FX Preview.
PLAY/STOP restent inchanges et les previews FM, Prism et autres moteurs ne sont
pas concernees. Multi conserve ses voix polyphoniques sur le meme Page Cache et
le meme transport physique.

Le codec courant est la version 20. Il ne reconnait aucun format anterieur et
ne contient ni type moteur alternatif, ni migration, ni alias sampler.
