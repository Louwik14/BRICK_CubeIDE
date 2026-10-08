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

## Validation multi-voix et lectures chainees

Le premier sweep materiel s'arretait pendant la premiere batch de warm-up.
Deux des huit requetes avaient ete preparees par le block device puis lancees
par `sdmmc_async_transport_chain_next`. Ce chemin oubliait le caractere
progressif de la requete et appelait le transport avec
`progressive_requested = 0`. Ces deux pages devenaient READY et etaient
bit-perfect apres l'invalidation finale, mais ne publiaient ni chunk0 ni les 32
watermarks. Les six autres pages expliquent les 192 IDMABTC et publications;
les huit validations finales expliquent les 256 chunks bit-perfect.

Le descripteur prepare transporte maintenant explicitement le flag progressif.
Une batch de huit pages 16 KiB doit verifier, hors reset de campagne :

```text
cmd18 = dataend = cmd12 = 8
idmabtc = chunk_publish = 256
final_chunk_idmabtc = 8
chunk_bit_perfect = 256
```

La validation `fail_step = 11` publie desormais
`error_snapshot.validation_voice_index` et
`error_snapshot.validation_missing_mask`. Les bits `0x0004`, `0x0800` et
`0x1000` signifient chunk0 absent, nombre de chunks incomplet et masque de
chunks incomplet. Le run fautif reunissait ces trois conditions (`0x1804`) sur
les lectures chainees non progressives.

Les compteurs produit et histogrammes excluent volontairement le warm-up. Dans
le run fautif, `warmup_pages_ready` restait a zero parce que la validation
precedait son incrementation; aucune batch mesuree n'avait commence. Le
snapshot `queue[0..7]` est maintenant alimente aux frontieres reelles (trigger,
lease, submit, debut physique, chunk0 et consommation du presocle), y compris
pendant le warm-up, et n'est plus construit en fin de batch.

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
