# USB Audio + MIDI Update

## Fichiers modifiés / ajoutés
- App/usb_stack/usb_device.c
- App/usb_stack/usbd_conf.c
- App/usb_stack/usbd_conf.h
- App/usb_stack/usbd_desc.c
- App/Middlewares/ST/STM32_USB_Device_Library/Core/Inc/usbd_def.h
- App/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
- App/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
- App/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc/usbd_audio.h
- App/Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c
- App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Inc/usbd_midi.h
- App/Middlewares/ST/STM32_USB_Device_Library/Class/MIDI/Src/usbd_midi.c
- App/Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Inc/usbd_composite_builder.h (ajout)
- App/Middlewares/ST/STM32_USB_Device_Library/Class/CompositeBuilder/Src/usbd_composite_builder.c (ajout)
- Inc/usbd_audio_if.h (ajout)
- Src/usbd_audio_if.c
- Inc/usb_audio_backend.h (ajout)
- Src/usb_audio_backend.c (ajout)

## Changements effectués
- Passage en composite Audio + MIDI avec enregistrement des classes via l’infrastructure composite ST, et interface Audio enregistrée via `USBD_AUDIO_RegisterInterface`.
- Ajout d’un builder composite minimal pour assembler le descripteur configuré Audio + MIDI et renseigner les endpoints/interfaces utilisés.
- Ajout de la classe Audio ST officielle (copie de la librairie ST) et exposition des descripteurs nécessaires au composite.
- Adaptation du backend MIDI pour le composite (endpoints dédiés, stockage de l’état par classe, routing des endpoints).
- Stabilisation de `usbd_audio_if.c` en stubs sans dépendances BSP/SAI, avec silence et rejet des données.
- Ajout d’un backend audio interne (`usb_audio_backend.c/.h`) avec FIFO circulaire statique (underflow -> silence, overflow -> drop).

## Raisons
- Permettre l’énumération en périphérique composite Audio + MIDI en conservant la classe Audio ST officielle et le mécanisme composite ST.
- Préparer un backend audio interne sans dépendances matérielles ni allocation dynamique.
- Garantir que la pile USB et la classe MIDI restent fonctionnelles dans le contexte composite.
