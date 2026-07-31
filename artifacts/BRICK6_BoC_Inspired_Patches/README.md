# BRICK6 BoC-Inspired Patch Bank

64 original mono-track P1 patches for slots 128-191. Copy the BRICK directory to the SD-card root.

The filename carries the slot only; B6P payloads contain no slot number, so files may be renamed to another valid Pxxxx.B6P slot.

Wave is intentionally excluded: the current Patch payload stores Wave table indices but patch_v1_capture_sampler_asset() captures portable asset paths only for Sampler RAM/STREAM/MULTI. A Wave patch would therefore depend on an arbitrary preloaded wavetable slot.

## Patch list

| Slot | Patch | Engine | Category | Intention |
|---:|---|---|---|---|
| 128 | SUN FADE | Stack | Keys | Warm faded keyboard with restrained tape drift |
| 129 | DUST KEY | Prism | Keys | Dusty educational-film keyboard |
| 130 | COLD EDU | Deluge | Keys | Plain cold triangle classroom synth |
| 131 | TAPE TINES | Prism | Keys | Soft FM tines recorded on aging tape |
| 132 | PALE COMBO | Stack | Organ | Pale compact organ with quiet movement |
| 133 | FILM ORGAN | Deluge | Organ | Muted analog-square documentary organ |
| 134 | SMALL CHOIR | Prism | Organ | Small synthetic choir with human fragility |
| 135 | FADED STR | Stack | Strings | Faded mono string-machine voice |
| 136 | SCHOOL STR | Prism | Strings | Simple school-film string ensemble |
| 137 | HAZE STR | Deluge | Strings | Hazy analog saw string line |
| 138 | WORN KEYS | Stack | Keys | Worn morph keyboard with soft edges |
| 139 | GLASS EDU | Prism | Keys | Naive glass keyboard from an old science film |
| 140 | MOSS EP | Stack | Keys | Mossy folded electric piano |
| 141 | CHILD KEY | Stack | Keys | Innocent toy keyboard with a faint uneasy edge |
| 142 | PALE TAPE | Stack | Pad | Pale slow pad with subtle tape instability |
| 143 | OLD FILM | Prism | Pad | Dark warm pad for an old educational film |
| 144 | AMBER FOG | Deluge | Pad | Amber analog fog with gentle motion |
| 145 | GREY SKY | Prism | Pad | Mostly stable grey swarm pad |
| 146 | WARM STATIC | Stack | Pad | Warm static-like swarm without excess noise |
| 147 | FRAGILE AIR | Prism | Pad | Fragile breath and distant bell air |
| 148 | COLD SUN | Deluge | Pad | Cold triangle pad with restrained pitch drift |
| 149 | MOSS VEIL | Stack | Pad | Soft moss-covered evolving veil |
| 150 | NIGHT EDU | Prism | Pad | Nasal night-school pad with quiet tension |
| 151 | SLEEP MAP | Stack | Pad | Very slow map-like ambient bed |
| 152 | SIMPLE ARC | Stack | Lead | Simple memorable analog arc lead |
| 153 | EDU FLUTE | Prism | Lead | Synthetic classroom flute with delayed vibrato |
| 154 | SUN NEEDLE | Deluge | Lead | Thin warm analog needle melody |
| 155 | NASAL STAR | Stack | Lead | Nasal star-like lead with mild unease |
| 156 | MEMORY LINE | Prism | Lead | Faded memorable mono melody voice |
| 157 | PALE WHISTLE | Prism | Lead | Pale synthetic whistle with bell breath |
| 158 | EMPTY ROAD | Deluge | Lead | Lonely plain triangle roadside lead |
| 159 | TINY SIGNAL | Stack | Lead | Childlike signal tone with subtle wrongness |
| 160 | MOSS BASS | Stack | Bass | Round dark moss-covered sub bass |
| 161 | ROUND FLOOR | Deluge | Bass | Clean round floor-shaking sine bass |
| 162 | ACID LESSON | Prism | Bass | Muted acid lesson with controlled resonance |
| 163 | TAPE REESE | Stack | Bass | Subtle beating Reese recorded to tape |
| 164 | COLD PULSE | Deluge | Bass | Cold analog-square pulse bass |
| 165 | DULL ROOT | Prism | Bass | Dull sub-root bass with faded harmonics |
| 166 | DARK SAW | Deluge | Bass | Dark analog saw bass with slow wear |
| 167 | LOW THREAT | Stack | Bass | Low feedback threat anchored by a sub |
| 168 | TOY PEG | Stack | Pluck | Dry naive toy peg |
| 169 | DUST PLUCK | Prism | Pluck | Dusty soft pizzicato pluck |
| 170 | SOFT CLICK | Deluge | Pluck | Soft innocent triangle click |
| 171 | TINY DROP | Prism | Pluck | Tiny FM droplet for childlike motifs |
| 172 | WOOD FRAME | Stack | Pluck | Muted wooden-frame pluck |
| 173 | OLD REED | Prism | Pluck | Short old reed pluck with tape wear |
| 174 | SCHOOL BELL | Stack | Bell | Small school bell with softened edges |
| 175 | PALE MALLET | Prism | Bell | Pale mallet from an old classroom recording |
| 176 | FILM GLOCK | Prism | Bell | Faded documentary glockenspiel |
| 177 | WOOD CHIME | Stack | Bell | Warm compact wooden chime |
| 178 | TAPE BOWL | Prism | Bell | Dark tape-aged bowl resonance |
| 179 | CHILD CHIME | Prism | Bell | Sweet toy chime that turns faintly uncanny |
| 180 | TAPE ROOM | Stack | Texture | Slow empty tape-room atmosphere |
| 181 | OLD WEATHER | Prism | Texture | Aged weather-film cloud texture |
| 182 | EDU HUM | Deluge | Texture | Low educational projector-like hum |
| 183 | GREEN DRONE | Stack | Texture | Deep green slow-moving drone |
| 184 | DUST CLOUD | Prism | Texture | Irregular spectral dust cloud |
| 185 | COLD MOTOR | Deluge | Texture | Cold slow motor pulse for an old soundtrack |
| 186 | SLOW MACHINE | Stack | Texture | Slow mechanical square and ring texture |
| 187 | MOSS SIGNAL | Stack | Texture | Fragile mossy signal drone |
| 188 | BENT LESSON | Stack | Experimental | Bent but playable educational motif voice |
| 189 | BROKEN TOY | Prism | Experimental | Broken digital toy that remains melodic |
| 190 | LOST NUMBER | Deluge | Experimental | Unsteady square counting tone |
| 191 | SMALL PANIC | Stack | Experimental | Musical low panic cue with restrained instability |

## Validation

Generated and validated against the current branch headers: B6PT magic, version 1, 56-byte packed header, 3848-byte PatchSaveV1 payload, DJB2-XOR checksum, P1 width, Synth family and engine type, finite/ranged persisted floats, and engine-compatible Matrix destinations. The ARM Cortex-M7 compiler also checks every persistent size and offset used by the serializer.

Regenerate from the repository root with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\patch_bank\generate_musical_patch_bank.ps1 -Bank BoC
```
