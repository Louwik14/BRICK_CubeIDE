# MIDI Clock IRQ path

TIM3 priority 0 uses a fractional musical deadline. At 120 BPM the nominal
F8 interval is 20.833333 ms. Each due interrupt publishes one F8 to the
ordered USB MIDI queue and pends OTG_FS priority 1. The USB IRQ passes the
packet to TinyUSB and the endpoint. USB SOF and the main loop do not gate F8.

The clock profile records missed ticks, maximum IRQ lateness, publication
delay, USB ready delay, endpoint submit and completion delay, drops, and
backlog. USB IRQ duration uses CPU cycles: minimum, maximum, count, and
total. Divide total by count for the mean.
