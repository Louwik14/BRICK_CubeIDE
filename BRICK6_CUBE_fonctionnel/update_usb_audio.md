# USB Audio Composite Update

## Ce qui a été fait
- Création de la classe composite `USBD_BRICK6_COMPOSITE` avec endpoints MIDI + Audio IN isochrone.
- Ajout d’un backend audio USB avec FIFO circulaire non bloquant.
- Descripteur USB composite (Audio Control + Audio Streaming IN + MIDI) ajouté.
- Initialisation USB mise à jour pour enregistrer la classe composite.
- Mise à jour du nombre d’interfaces USB.
- Topologie Audio Control corrigée pour exposer Line In/Line Out + deux interfaces Audio Streaming (IN/OUT).
- Endpoints Audio IN/OUT isochrones ajoutés avec tailles 384 bytes @48 kHz, 24 bits.
- Backend audio USB configuré pour underflow → silence, overflow → drop.

## Fichiers modifiés / ajoutés
- `App/usb_stack/usbd_brick6_composite.c`
- `App/usb_stack/usbd_brick6_composite.h`
- `App/usb_stack/usb_device.c`
- `App/usb_stack/usbd_desc.c`
- `App/usb_stack/usbd_desc.h`
- `App/usb_stack/usbd_conf.h`
- `App/audio/usb_audio_backend.c`
- `App/audio/usb_audio_backend.h`

## Reste à faire
- Relier le moteur audio pour pousser des frames réelles vers `usb_audio_backend_push_frames`.
- Valider l’énumération et la stabilité sur différents OS.
- Ajouter un vrai chemin de capture audio interne vers l’USB IN (si nécessaire).

## Points à risque
- Les descripteurs Audio/MIDI doivent rester cohérents avec le host (Windows/Mac/Linux).
- Les timings isochrones devront être validés avec le vrai flux audio (pas de feedback pour l’instant).
