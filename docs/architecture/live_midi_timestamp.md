# USB MIDI live timestamp contract

USB MIDI reception keeps the transport timestamp with the fixed 4-byte
USB-MIDI packet until the final channel-voice message reaches the keyboard
engine.

## Device

`midi_usb_rx_submit_from_isr()` captures TIM5 once for the received callback.
All packets in that callback keep that tick and receive a non-zero monotonic
`ingress_serial` in packet order. The bounded device RX queue has 128 packet
slots. A full queue rejects the packet and increments the existing RX-drop
diagnostics; no MIDI packet is processed in the USB ISR.

## Host

At bulk-transfer completion, the host captures one TIM5 tick for the transfer.
`USBH_MIDI_PushRx()` copies that tick to every 4-byte packet produced by the
transfer and assigns serials in the original packet order. Packets therefore
may share a tick and remain stably ordered. `midi_host_poll_bounded()` only
consumes the already timestamped queue; it is not the timestamp source. The
host RX queue has 64 packet slots and retains its existing bounded rejection
behavior.

The timestamp and serial cross the MIDI parser, `midi.c`, keyboard runtime,
keyboard engine, and NoteFx source command. Note-on, note-off, and note-on with
velocity zero use the same path. The audio owner performs TIM5-to-sample
conversion; exact timed-file sorting and block segmentation remain later-pass
responsibilities. MIDI output is unchanged.
