# USB Audio Composite Update

## Ce qui a été fait
- Création de la classe composite `USBD_BRICK6_COMPOSITE` avec endpoints MIDI + Audio IN isochrone.
- Ajout d’un backend audio USB minimal (pop retourne 0 frame → silence).
- Descripteur USB composite (Audio Control + Audio Streaming IN + MIDI) ajouté.
- Initialisation USB mise à jour pour enregistrer la classe composite.
- Mise à jour du nombre d’interfaces USB.

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

## Points à risque
- Les descripteurs Audio/MIDI doivent rester cohérents avec le host (Windows/Mac/Linux).
- Les timings isochrones devront être validés avec le vrai flux audio (pas de feedback pour l’instant).
