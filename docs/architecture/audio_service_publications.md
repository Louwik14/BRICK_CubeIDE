# Publications AUDIO vers services cooperatifs

Toutes les publications s'executent sur le meme Cortex-M7. Elles separent une
IRQ d'une superloop, pas deux processeurs.

## Publications conservees

- `control_audio_fifo.tail` libere les cases SPSC; il ne confirme aucun etat
  musical.
- Les leases STREAM protegent les pages encore lisibles par les voix AUDIO.
- Le niveau REC et son trigger forment une publication bidirectionnelle bornee
  AUDIO/CONTROL.
- Les waveforms audio et synth utilisent des snapshots seqlockes; le double
  buffer audio permet la capture progressive d'une frame.
- Le diagnostic Audio publie l'etat boot/erreur et la charge CPU moyenne.
- Le playhead RAM publie un snapshot seqlocke pour l'UI.

Ces objets sont en memoire M7 cacheable sauf les rings dont le placement
non-cacheable est justifie par une frontiere IRQ distincte. `DMB`, seqlocks et
compteurs SPSC restent necessaires parce qu'une IRQ peut interrompre la
superloop.

## Horloge et ownership

TIM5 est l'horloge media canonique. Le M7 initialise son extension 64 bits et
possede l'IRQ d'overflow. CONTROL, SEQ et AUDIO lisent cette meme timeline.
Aucune mailbox, image secondaire ou publication d'horloge inter-coeur
n'existe.

Les tokens, generations, `registration_epoch` et `media_epoch` conserves
protegent les lifecycles de ressource, les sessions Recorder et les
completions I/O tardives.
