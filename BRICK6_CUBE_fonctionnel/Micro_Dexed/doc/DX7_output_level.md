From dronus. Maybe this helps to get a better SNR?

But polyphony is, using 16bit DAC. As 16 notes may playing simulttaneously, the peak sample value is actually 16 times that of a single oscillator. Hence the Dexed code scales the output down in some fashion, there is a explicit division by 16 at dexed.cpp:148 to cope for the 16x polyphony, and maybe some further attenuation depending on the algorithm to cope with the parallel carrier outputs.

That means playing only one note, only 12 of 16bit amplitude would be used, this can be clearly seen if audio is recorded via USB and all volume and carrier output levels are at 100.

At this point, were already down to the original DX7 12bit quality.

But it is getting worse: The original DX7 has an additional discrete 4 bit DAC for volume adjustments, that may handle carrier output level or algorithm attenuation level. If this is the case, we can expect usage of the whole 12bit range for monophonic single carrier waveforms.

As the original DX7 uses time multiplex (all voices are played one after another in every 60kHz sample interval), every voice may get 12bit by its own.

So it may turn out that Dexed's mangled 16bit output give a worse SNR than the original '12-bit' DX7 system (that is in fact a 16-times 12 bit mantissa 4 bit exponent floating point DAC).

Also MicroDexed operate at a lower sample rate (44.1kHz) in respect to the DX7 (57kHz).

I guess that is why the original Dexed choses a 24bit implementation, which may give reasonable SNR to pass the DX7 quality. But that would need further 24bit processing (like amplification or compression) before conversion to 16bit to get a full range 16bit signal.