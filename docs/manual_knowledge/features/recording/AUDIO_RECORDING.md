# REC-01 — Audio Recorder et edition de prise

## Statut

CONFIRME — validation materielle OUI.

Audio REC est une modale a deux vues. Configuration : Arm OFF/REC/TRIG ; trigger NOW/THRESHOLD/PATTERN/THRESHOLD_PLAY ; longueur FREE ou 1..64 bars ; quantification NOW/BAR/PATTERN ; seuil dBFS ; LINE/MIC/USB ; routes des huit pistes ; OVERDUB. Les routes sont un etat de session, pas un routing de playback persistant.

Phases : IDLE, ARMED, WAIT_QUANT, RECORDING, STOPPING, TAKE_READY, REC_EDIT, SAVED, ERROR. Le bouton REC global notifie le modele. La capture AUDIO alimente un ring PCM ; STORAGE reserve, ecrit et finalise un WAV. Les triggers quantifies recoivent les callbacks transport.

La zone waveform montre le bus REC reel en direct pendant IDLE/ARMED/WAIT.
Au demarrage AUDIO effectif elle bascule sur les frames deja commitees sur SD,
construites progressivement. FREE et les longueurs fixes démarrent à la
résolution maximale puis compactent l'axe temporel par deux quand nécessaire;
la projection suit toujours la durée réellement acquise. STOPPING garde cette
vue et REC EDIT réutilise le même résumé final. RETURN ou la fin de SAVE remet
REC MODE sur le scope live sans détruire la génération REC publiée.

La waveform enregistrée utilise une échelle verticale pleine échelle fixe :
les différences de niveau restent visibles au lieu d'être remontées au maximum
à chaque rafraîchissement. SAVE et l'overview REC EDIT restent disponibles si
seule la projection de lecture STREAM échoue.

REC EDIT gere start/end, loop start/end, zoom vertical/horizontal, scroll et zero-cross. Audition utilise la prise trimmee ; Save publie le fichier final ; Assign charge et lie la prise a une piste compatible, eventuellement apres arret transport. Erreurs explicites : no route, sample active, SD busy/I/O, no take/slot, load, preview, overdub, stream.

Sources : `Inc/Storage/sample_capture.h`, `Src/Storage/SampleCapture/*`, `Src/UI/pages/AudioRec/*`, `Src/Recorder/audio_recorder_ring.c`, `Src/Storage/audio_recorder*.c`, `docs/architecture/recorder_sd.md`.
