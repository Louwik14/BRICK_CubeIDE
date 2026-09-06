# Audit USB — current findings V3

Date de la re-audit : 2026-09-06

Audit de l’etat courant du repository, en lecture seule. Aucun patch source et
aucun build execute.

Contrat audite : `USB_SERVICE` est l’owner unique de TinyUSB et du transport
USB. Le service doit etre reveille par HOST_FLAG EXTI, FUSB302 EXTI, MIDI
RX/TX, completions HCD, TinyUSB `call_after`, deadline VBUS et evenements UAC2.
Il ne doit exister aucun fallback periodique.

### VERDICT

**NEW FINDINGS**

### NEW FINDINGS

#### USB-01 — Device MIDI RX perd des paquets lorsque le handoff CONTROL est plein

`tud_midi_rx_cb()` vide le FIFO TinyUSB jusqu’a epuisement
(`Board/UsbStack/usb_device.c:396-402`). Chaque paquet est ensuite pousse dans
la file RX de 128 paquets consommee par CONTROL. Quand cette file est pleine,
`midi_usb_rx_submit_from_isr()` compte et abandonne le paquet
(`Src/MIDI/midi.c:455-478`, `Src/MIDI/midi.c:2022-2053`).

CONTROL ne traite que 16 paquets par reveil (`Src/MIDI/midi.c:776-798`). Une
rafale ou un retard CONTROL permet donc a USB_SERVICE de consommer les
paquets restants du FIFO TinyUSB alors que le handoff est sature. Les paquets
abandonnés ne restent ensuite dans aucun backlog et aucun re-wake ne peut les
recuperer.

Bug reel demontre : perte fonctionnelle d’evenements MIDI Device RX sous
charge/retard CONTROL. La correction doit rester locale au handoff : ne pas
consommer au-dela de la capacite disponible, ou conserver le reste et
re-veiller CONTROL tant qu’il reste du travail. Aucun matching endpoint/device
n’est necessaire.

#### USB-02 — Une erreur FUSB302/I2C peut monopoliser USB_SERVICE

La conservation de `irq_pending` jusqu’a la fin correcte de la transaction est
bien presente (`Board/UsbStack/fusb302.c:425-449`). Mais ce latch est aussi
teste comme travail immediatement runnable
(`Board/Generated/Src/freertos.c:188-210`).

Sur erreur I2C, `usb_role_manager_process()` laisse donc `irq_pending` a un et
retourne (`Board/UsbStack/usb_role_manager.c:171-179`). La boucle
`StartUsbTask` repart sans attendre (`Board/Generated/Src/freertos.c:398-412`).
Une panne persistante, ou un `INT_N` maintenu bas, repete alors les lectures
I2C, avec jusqu’a 10 ms de timeout par tentative.

Bug reel demontre : USB_SERVICE peut consommer le CPU en boucle et retarder
indefiniment les autres evenements USB pendant une panne FUSB/I2C. La gestion
doit rester locale au FUSB : conserver le latch, mais sortir de l’etat
runnable permanent via une recovery bornee, par exemple une seule deadline de
retry. Cette deadline ne doit pas devenir un polling periodique general.

#### USB-03 — Le reordonnancement/rollback de la file Device TX est perdant

`midi_usb_try_flush_internal()` retire plusieurs paquets, puis remet en tete
les paquets non acceptes par TinyUSB (`Src/MIDI/midi.c:573-646`). Chaque
operation de file est protegee individuellement, mais le burst n’est pas
reserve atomiquement. Un producteur CONTROL ou temps reel peut remplir les
places liberees avant le `push_front`
(`Src/MIDI/midi.c:349-440`, `Src/MIDI/midi.c:834-937`).

Le cas existe lors d’une acceptation partielle TinyUSB : `push_front` peut
echouer et le paquet est explicitement droppe. Le meme risque existe lorsqu’un
paquet non temps reel est retire pour respecter l’ordre d’un burst temps reel
(`Src/MIDI/midi.c:603-610`) : l’echec du `push_front` est ignore.

Bug reel demontre : perte de messages Device TX sous backpressure partielle ou
concurrence de production, independamment du matching endpoint/device. La
correction doit seulement rendre la reservation/rollback de ce burst non
perdante, ou lui reserver une capacite equivalente.

### PINAILLAGE REJETÉ

- **Ownership TinyUSB :** les pumps `tud_task_ext()` et `tuh_task_ext()` sont
  appeles par le chemin USB_SERVICE. Les IRQ TinyUSB publient des evenements;
  les callbacks TinyUSB s’executent dans le pump. Aucun appel concurrent
  effectif hors owner n’a ete confirme.
- **Device TX attendu :** les producteurs mettent en file, positionnent le
  travail differe et reveillent USB_SERVICE; `tud_midi_n_packet_write_n()` est
  appele dans `midi_usb_service_poll()`. Le rollback perdant est traite en
  USB-03, sans ajouter de systeme de matching.
- **Host RX :** la lecture est bornee; le backlog TinyUSB est re-evalue et
  USB_SERVICE est reveille tant que la file d’evenements CONTROL a de la place.
  Lorsque CONTROL est pleine, la lecture est retenue jusqu’a liberation.
- **Host TX/backpressure :** l’echec de `tuh_midi_write_flush()` met le
  transport en attente d’une completion HCD; le hook HCD efface cette attente
  et reveille USB_SERVICE. Le reveil sur toute completion peut causer une
  tentative superflue, mais aucun bug utilisateur/CPU ne demontre la necessite
  d’un matching endpoint/device.
- **HCD et TinyUSB events :** les completions sont mises en file par TinyUSB
  et publiees via `tuh_event_hook_cb()`; USB_SERVICE pompe la file et continue
  tant qu’elle n’est pas vide.
- **HOST_FLAG :** les fronts EXTI sont latched, reveillent USB_SERVICE et sont
  consommes par le role manager. Aucun polling periodique du GPIO n’a ete
  trouve.
- **FUSB302 nominal :** apres transaction reussie et `INT_N` relache,
  `irq_pending` est efface; s’il reste actif, il est conserve. Le defaut
  retenu est uniquement la boucle en cas d’erreur persistante (USB-02).
- **`call_after` et VBUS :** les deadlines reelles sont exposees au timeout
  USB_SERVICE. Sans travail ni deadline, le wait est `osWaitForever`; aucun
  fallback periodique n’a ete trouve.
- **UAC2 :** les callbacks UAC2 et les acces TinyUSB restent dans le pump et
  `usb_audio_transport_process()` de USB_SERVICE. Les transferts actifs sont
  rearmes par les completions TinyUSB; l’absence de wake direct depuis le ring
  audio ne demontre ni perte independante ni boucle CPU.
- **Workers paralleles :** CONTROL, le chemin audio hard et les ISR touchent
  les files/rings ou publient des wakes, sans prendre le pump TinyUSB. Le
  helper public `midi_host_send()` n’a aucun appelant courant; son observation
  de montage n’est donc pas un conflit d’ownership effectif.
- Les bornages de burst, compteurs de drop explicites, retries supplementaires
  sans impact, et controles d’API non appeles hors owner sont rejetes lorsqu’ils
  ne demontrent pas un bug utilisateur ou CPU distinct.

### DOCUMENT

- Aucun patch source.
- Aucun build.
- Documentation mise a jour : ce fichier uniquement.
