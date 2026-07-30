# BRICK6 Musical Patch Bank

64 mono-track P1 patches for slots 064-127. Copy the BRICK directory to the SD-card root.

The filename carries the slot only; B6P payloads contain no slot number, so files may be renamed to another valid Pxxxx.B6P slot.

Wave is intentionally excluded: the current Patch payload stores Wave table indices but patch_v1_capture_sampler_asset() captures portable asset paths only for Sampler RAM/STREAM/MULTI. A Wave patch would therefore depend on an arbitrary preloaded wavetable slot.

## Patch list

| Slot | Patch | Engine | Category | Intention |
|---:|---|---|---|---|
| 64 | Velvet Dawn | Stack | Pad | Warm morphing analog pad |
| 65 | Dark Harbor | Prism | Pad | Dark sub and sine pad with slow motion |
| 66 | Glass Veil | Prism | Pad | Glassy bell partials suspended in air |
| 67 | Amber Poly | Stack | Pad | Warm triple analog chordless bed |
| 68 | Cloud Choir | Prism | Pad | Breathy vowel choir pad |
| 69 | Slow Aurora | Deluge | Pad | Analog saw pad with a slow opening halo |
| 70 | Blue Distance | Stack | Pad | Wide swarm and octave atmosphere |
| 71 | Soft Circuit | Deluge | Pad | Muted analog-square pulse pad |
| 72 | Frozen Lake | Prism | Pad | Icy spectral line and bell pad |
| 73 | Copper Air | Stack | Pad | Organic trimorph pad with a soft overtone |
| 74 | Moon Current | Deluge | Pad | Pure triangle ambient current |
| 75 | Analog Mist | Prism | Pad | Swarm and sine-triangle analog mist |
| 76 | Rubber Pick | Stack | Pluck | Round sub pluck with elastic snap |
| 77 | FM Droplet | Prism | Pluck | Clean FM water-drop pluck |
| 78 | Bright Peg | Deluge | Pluck | Bright saw peg with tight filter strike |
| 79 | Wood Tick | Stack | Pluck | Dry organic wooden tick |
| 80 | Crystal Pin | Prism | Pluck | Tiny crystal and ring-mod pin |
| 81 | Soft Pizz | Prism | Pluck | Soft sine pizzicato |
| 82 | Sync Chip | Stack | Pluck | Percussive square-sync chip |
| 83 | FM Reed | Prism | Pluck | Feedback-FM reed pluck |
| 84 | Mellow Dot | Deluge | Pluck | Gentle triangle dot |
| 85 | Vowel Pick | Prism | Pluck | Short articulate vowel pick |
| 86 | Satin Lead | Prism | Lead | Soft singing sine-triangle lead |
| 87 | Fat Mono | Stack | Lead | Fat three-oscillator analog mono lead |
| 88 | Nasal Wire | Prism | Lead | Nasal focused lead with wire edge |
| 89 | Sync Razor | Stack | Lead | Hard sync-like razor lead |
| 90 | Honey Saw | Deluge | Lead | Warm analog saw lead |
| 91 | Acid Voice | Prism | Lead | Resonant vocal sync lead |
| 92 | Tri Burner | Stack | Lead | Folded triangle lead with FM heat |
| 93 | Digi Fang | Prism | Lead | Aggressive digital modulation lead |
| 94 | Pulse Talk | Deluge | Lead | Expressive analog pulse talker |
| 95 | Morph Scream | Stack | Lead | Morphing aggressive solo voice |
| 96 | Deep Sub | Deluge | Bass | Pure deep sine sub |
| 97 | Rubber Sub | Stack | Bass | Rubbery sub with a folded upper edge |
| 98 | Night Reese | Stack | Bass | Slowly beating dark Reese bass |
| 99 | Acid Core | Prism | Bass | Tight resonant acid core |
| 100 | FM Thud | Prism | Bass | Punchy FM bass thud |
| 101 | Saw Floor | Deluge | Bass | Solid analog saw floor |
| 102 | Growl Root | Stack | Bass | Feedback growl anchored by sub |
| 103 | Hollow Bass | Prism | Bass | Hollow formant bass |
| 104 | Square Weight | Deluge | Bass | Heavy analog square bass |
| 105 | Folded Low | Stack | Bass | Folded low bass with triangle bite |
| 106 | Dream Keys | Prism | Keys | Dreamy sine keys with bell dust |
| 107 | Organic Tines | Stack | Keys | Organic trimorph electric tines |
| 108 | Digital EP | Prism | Keys | Controlled FM electric piano |
| 109 | Soft Organ | Deluge | Keys | Simple mellow triangle organ |
| 110 | Reed Keys | Prism | Keys | Breathy reed keyboard |
| 111 | Fold Clav | Stack | Keys | Folded clavinet snap |
| 112 | Toy Keys | Prism | Keys | Playful toy keyboard |
| 113 | Warm Combo | Stack | Keys | Warm compact combo keyboard |
| 114 | Silver Bell | Prism | Bell | Clear silver bell |
| 115 | Wood Mallet | Stack | Bell | Rounded wooden mallet |
| 116 | FM Chime | Prism | Bell | Bright two-ratio FM chime |
| 117 | Glass Marimba | Stack | Bell | Glassy marimba strike |
| 118 | Temple Bowl | Prism | Bell | Dark resonant temple bowl |
| 119 | Soft Glock | Prism | Bell | Soft compact glockenspiel |
| 120 | Polar Drone | Stack | Texture | Slow polar swarm drone |
| 121 | Dust Field | Prism | Texture | Cloud and filtered-noise dust field |
| 122 | Machine Fog | Deluge | Texture | Slow industrial pulse fog |
| 123 | Deep Current | Stack | Texture | Low evolving current drone |
| 124 | Broken Halo | Prism | Experimental | Granular halo with digital fractures |
| 125 | Elastic Metal | Stack | Experimental | Playable elastic metallic resonance |
| 126 | Phase Garden | Deluge | Experimental | Musical phase-reset square garden |
| 127 | Morphic Reed | Stack | Experimental | Unusual but melodic morphing reed |

## Validation

Generated and validated against the current branch headers: B6PT magic, version 1, 56-byte packed header, 3848-byte PatchSaveV1 payload, DJB2-XOR checksum, P1 width, Synth family and engine type, finite/ranged persisted floats, and engine-compatible Matrix destinations. The ARM Cortex-M7 compiler also checks every persistent size and offset used by the serializer.

Regenerate from the repository root with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\patch_bank\generate_musical_patch_bank.ps1
```
