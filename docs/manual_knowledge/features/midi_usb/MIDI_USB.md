# MIDI-01 — MIDI, USB, clock et transport

## Statut

PARTIEL — USB Device et logique confirmees ; DIN/Host/selection UI a valider.

Chaque entite active a canal 1..16 et source INTERNAL, EXTERNAL ou ALL. Une piste MIDI emet Program Change et 12 CC ; External ajoute gate ON/TRIG. Le moteur unifie Note On/Off, aftertouch, CC, program, pressure, pitchbend, system common/realtime et channel mode vers UART, USB ou BOTH.

USB Device TinyUSB fournit RX/TX MIDI avec queues bornees, compteurs de drop/high-watermark et reserve Note Off. Les messages entrants portent TIM5 et serial ingress. `midi_internal_receive_with_source` alimente notes/transport selon source. Le clock bridge distingue INTERNAL, EXTERNAL_MIDI, EXTERNAL_USB ; le master emet 24 PPQN et Start/Continue/Stop, le slave discipline le FSM SEQ.

Les commentaires publics marquent USB Host et DIN UART comme backends futurs/stubs alors que des sources existent ; leur atteignabilite carte n'est pas suffisamment prouvee. La page CFG MIDI couvre canal/source, mais l'UI de clock globale n'a pas ete trouvee dans Settings actuelle.

Sources : `Inc/MIDI/midi.h`, `Src/MIDI/{midi,midi_host,midi_clock_timer}.c`, `Src/Seq/{seq_clock_bridge,seq_transport_fsm}.c`, `Board/LowCost/UsbStack/*`, `Src/UI/pages/ui_page_template_cfg.c`.
