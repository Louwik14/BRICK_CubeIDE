# GROUP child audio contract

Track 8 is the GROUP control and post-sum bus. Lanes 8..15 (UI 9..16)
are eight independent sampler children. Each child owns a distinct sampler
voice and mixer lane, including native mono/stereo format, filter, VCA,
level, pan, inserts and sends.

The mixer processes every child lane independently, then redirects its dry
post-insert output into the dedicated GROUP bus. The GROUP bus is processed
after all children and is the only GROUP dry route to MAIN/CUE. Parent mute
and parent audio parameters therefore apply after summation; they never
replace child state.

UI parameter pages resolve lane-owned values through `ui_get_active_lane()`.
Main-track configuration and topology operations remain based on
`ui_get_active_track()`.
