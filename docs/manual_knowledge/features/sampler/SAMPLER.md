# SMP — Sampler STREAM et MULTI

## Statut

CONFIRME — validation SD/audio requise.

## STREAM

Le type STREAM utilise le seul Streamer. Source POOL lit un asset Classic charge ; source REC lit la prise Recorder. Modes : ONE, REV.ONE, LOOP, PINGPONG, H.ONE, H.LOOP, H.PINGPONG. TONE expose asset, mode, gain, tune, Start, Length, Loop Start, Slice Count Off/2/4/8/16/32/64, BPM source 40..300, Sync Off/1/2/4 bars/Auto, Stretch Off/Speed/Shifter, grain et reglages Shifter.

Start/Length sont live, bornes ensemble et projetes vers un plan froid ; publication throttlee 12 Hz. Reverse et ping-pong appartiennent au lecteur ; STREAM emploie des pages 16 KiB rendues visibles progressivement par blocs 512 B.

## MULTI

Un Multi est prepare depuis un dossier/index de zones. TONE expose asset, gain et loop. Le loader scanne/convertit/indexe puis publie un instrument READY ; la projection AUDIO est obligatoire avant READY. Les notes resolvent les zones par key/velocity ; l'asset retire coupe l'admission et les references restent administrables.

## Persistance et interactions

La selection est une reference stable `{kind,path}`, jamais un slot runtime. Project/Pattern/Patch peuvent la transporter. P-lock et Matrix s'appliquent aux Params sonores, pas au selecteur asset. Maximum quatre pistes clip STREAM.

## Sources

`Src/Audio/Engines/Sampler/*`, `Src/Sampler/sample_*`, `multi_sample_*`, `Src/Storage/wav_*`, `Src/Param/param_spec_catalog.inc`, `docs/architecture/{sampler_streamer,sample_streaming,stream_need_contract,stream_start_length_live}.md`.
