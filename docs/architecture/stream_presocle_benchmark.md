# Benchmark STREAM : dimensionnement du presocle

Le firmware Release mesure automatiquement la taille minimale de donnees source
deja presentes en RAM qui permet a 8 voix STREAM cold de demarrer ensemble sans
starvation a x4. Le KPI principal est
`minimum_continuation_margin_us`; le critere candidat est
`starvation_count == 0 && minimum_continuation_margin_us >= 0` sur 10 000
batches.

## Sweep automatique

Une seule image enchaine sept campagnes :

```text
16384, 8192, 4096, 2048, 1024, 512, 256 octets
```

Chaque taille execute son warm-up normal puis 10 000 batches. Le Page Cache
n'est jamais vide artificiellement entre deux tailles; la selection conserve
le controle cold miss de chaque batch. Seuls les compteurs, histogrammes,
readers et etats transitoires du benchmark sont reinitialises. Le sweep reste
fixe a 8 voix et x4 par des gardes de compilation.

L'architecture reste figee : page 16 KiB, 32 chunks progressifs de 512 octets,
un CMD18 et un CMD12 par page, IDMA double-buffer, SDMMC1 priorite 1, AUDIO
priorite 2, bloc AUDIO 32 frames et SDCLK 50 MHz.

## Modele mesure

Chaque voix recoit son propre buffer FLOAT32 stereo, bit pour bit issu de la
position source precedant la page continuation. Au trigger AUDIO commun, le
VoiceReader est lie directement au debut de cette page cold : sa lease publie
immediatement le besoin dans le chemin produit normal, sans prechargement ni
scheduler de benchmark.

```text
presocle RAM -> VoiceReader -> lease -> STORAGE -> Stream Manager
              -> Page Cache -> mapping -> scheduler -> SDMMC/IDMA
```

AUDIO avance reellement de 1 ou 4 frames source par frame de sortie. A x4, il
execute `sample_voice_reader_render_pitch_forward`; un chunk de 512 octets ne
couvre que 16 frames de sortie. Le watermark progressif reste controle pendant
toute la page, donc une starvation apres chunk0 est detectee.

Le dernier frame lu dans le presocle et le premier frame de la page sont
verifies a leur position source. Le presocle, les frames source de continuation
consommes et les 32 chunks sont controles. Les compteurs
`presocle_transition_mismatch_count`, `presocle_data_mismatch_count`,
`continuation_data_mismatch_count` et `chunk_data_mismatch_count` doivent rester
nuls.

## Wrap DWT et marge

Un timestamp DWT peut legalement valoir zero. Chaque frontiere optionnelle a
un drapeau de validite distinct; aucune mesure ne traite plus zero comme une
absence. Toutes les durees utilisent une soustraction `uint32_t` modulo 2^32.
Le second faux arret provenait des sentinelles zero encore presentes dans les
probes need/storage/manager/reserve/resolve et les frontieres physiques, que la
premiere correction n'avait pas couvertes.

Pour chaque voix, `queue[0..7]` expose page cible, request publish, backend
submit, debut physique, chunk0, epuisement du presocle, starvation et duree.
Les temps non signes sont relatifs au trigger commun. La marge signee vaut :

```text
presocle_exhaust_time - first_chunk_available_time
```

Elle est positive si la continuation arrive avant epuisement, nulle a la
limite et negative sinon. Le resultat expose minimum, P50, P90, P99 et P99.9,
plus le minimum de presocle restant a l'arrivee de chunk0.

## IRQ et RAM

Le resultat recopie rearm moyen/max, handler IDMABTC moyen/max, intervalle
IDMABTC min/moyen/max, deadline misses et preemptions AUDIO par SD. La mesure
CPU AUDIO retire les cycles des IRQ SD imbriquees; `audio_wall_latency_max`
conserve la latence murale, preemptions incluses.

`presocle_ram_per_sample_64_slices_bytes` reporte `presocle_bytes * 64`, sans
allocation produit : 256 B donnent 16 KiB, 512 B 32 KiB, 1 KiB 64 KiB, 2 KiB
128 KiB, 4 KiB 256 KiB, 8 KiB 512 KiB et 16 KiB 1 MiB.

Validation d'une image candidate :

```text
progress = 10000, done = 1, error = 0
chunk_data_mismatch_count = 0
presocle_transition_mismatch_count = 0
generation_mismatch_count = 0
duplicate_chunk_publish_count = 0
out_of_order_chunk_publish_count = 0
rearm_deadline_miss_count = 0
starvation_count = 0
minimum_continuation_margin_us >= 0
```

Les sept resultats compacts restent dans `g_stream_presocle_sweep.result`.
Chaque entree contient son PASS/FAIL, ses marges, starvations, controles
d'integrite, charge AUDIO, maxima SD et RAM equivalente pour 64 slices. Le
sweep continue apres un FAIL et ne publie `done = 1` qu'apres la septieme
campagne. Les champs de synthese sont
`smallest_zero_starvation_presocle_bytes` et
`first_failing_presocle_bytes`.

Dump GDB :

```gdb
shell cls
set pagination off
set print pretty on
p g_stream_presocle_sweep
p g_stream_presocle_sweep.smallest_zero_starvation_presocle_bytes
p g_stream_presocle_sweep.first_failing_presocle_bytes
```

Aucun resultat materiel n'est deduit du build seul.
