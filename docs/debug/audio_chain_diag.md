# Diagnostic temporaire de la chaîne audio PC vers codec

Signal attendu : PCM32 stéréo identique, sinus 1 kHz exact à 48 kHz,
-12 dBFS, pendant 30 secondes. La période est de 48 frames. Le diagnostic
compare chaque frame à celle d'il y a 48 frames après 100 ms de signal
au-dessus de 0,04 FS, séparément à chaque frontière. Les valeurs mémorisées
et les erreurs sont en Q24 signé (1 FS = 8 388 608). Seul l'historique des
48 dernières frames, puis 9 frames autour de la première anomalie de chaque
frontière, restent en RAM. Aucun bloc audio complet n'est enregistré.

Le symbole `g_audio_chain_diag` est `volatile` et conservé dans l'ELF
Release/LTO. `audio_chain_diag_reset()` est appelé au boot. Pour une nouvelle
capture pendant que le firmware tourne, remettre à zéro les 902 mots de la
structure par GDB, puis écrire `magic=0x41434431` et `version=1`. Ne pas
appeler la fonction depuis Black Magic : les registres FPU peuvent rendre
les appels GDB peu fiables. Le reset RAM doit être effectué quand la cible
est arrêtée.

| `stage[]` | Frontière observée | Seuil de rupture Q24 |
| --- | --- | ---: |
| 0 | PCM32 TinyUSB après lecture FIFO, avant conversion | 128 |
| 1 | float immédiatement après PCM32 → float | 128 |
| 2 | float après lecture du ring USB, y compris zéro sur sous-alimentation | 128 |
| 3 | USB float présenté à l'entrée du moteur/mixer | 128 |
| 4 | MAIN float après gain master et rendu du moniteur | 4096 |
| 5 | int24 après conversion de sortie | 4096 |
| 6 | demi-buffer TX au callback DMA, à l'entrée de la demi-zone suivante | 4096 |

Pour chaque stage, `samples_checked` compte les frames comparées,
`glitch_count` les frames au-dessus du seuil, `max_error` l'erreur maximale,
`first_glitch_tick` les millisecondes HAL, `first_glitch_block` l'appel du
checkpoint, `first_glitch_sample_index` l'index dans cet appel et
`first_glitch_stream_index` l'index cumulatif du stage. `blocks` et
`frames_seen` sont les compteurs locaux ; `armed` et `armed_frame` décrivent
la mise en observation. `snapshot_count` indique combien des neuf frames
`snapshot[0..8][L,R]` ont été remplies : quatre avant, la frame fautive,
quatre après. `history` est la fenêtre circulaire de 48 frames actuelle.

`pcm_float_mismatch_count` compte les échantillons où PCM32 et float divergent
de plus de 2 LSB Q24. `float_int24_mismatch_count` fait de même entre MAIN
float et int24, avec 3 LSB. `ring_underflow_count`, `ring_drop_frames` et
`usb_fifo_error_count` complètent le diagnostic du transport.

`tx_ndtr_begin/end` sont les compteurs DMA TX en mots 32 bits au début et à
la fin du rendu de `tx_last_half`. `tx_last_margin` est le nombre de mots
restant avant la prochaine consommation de cette demi-zone à la fin du
rendu ; `tx_min_margin` est son minimum observé. `tx_wrong_half_count` compte
les rendus commencés dans une demi-zone déjà consommée ;
`tx_deadline_miss_count` compte les rendus terminés trop tard ;
`tx_near_deadline_count` compte les rendus terminés à 8 mots ou moins de la
limite. `tx_dma_callback_count`, `tx_dma_last_half` et
`tx_dma_half_sequence_error_count` suivent la consommation TX. Les compteurs
`rx_callback_skipped_count` et `rx_callback_recovered_count` suivent les
callbacks RX écartés ou les phases récupérées.

Lire les stages dans l'ordre. Une rupture au stage 0 indique le contenu
USB/TinyUSB. Une première rupture au stage 1 indique la conversion d'entrée ;
au stage 2 le ring ; au stage 3 l'entrée moteur ; au stage 4 le DSP/mixer ;
au stage 5 le packing ; au stage 6 ou dans les compteurs TX la DMA/SAI ou
le timing. Les traitements volontaires (gain ou effets variables, métronome,
autres sources) peuvent rendre le MAIN non périodique et doivent être
désactivés pour ce test. Une erreur dans le codec analogique après SAI reste
invisible à ces checkpoints numériques.
