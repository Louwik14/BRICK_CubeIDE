# Inventaire fonctionnel

Regle de comptage : une fonctionnalite est un comportement utilisateur autonome ayant un acces, un etat ou un resultat distinct ; les variantes de moteur sont comptees separement, mais pas chaque parametre. Les 24 entrees ci-dessous sont les unites de couverture. Les sous-fonctions restent decrites dans les fiches.

| ID | Fonction | Statut | Fiche |
|---|---|---|---|
| CTRL-01 | Surface, modifiers et priorite des handlers | CONFIRME | [controles](PHYSICAL_CONTROLS.md) |
| UI-01 | Navigation ensembles/sous-pages/modales | CONFIRME | [navigation](UI_NAVIGATION.md) |
| TRACK-01 | 8 top-level, GROUP, 7 children et Master 15 | CONFIRME | [pistes](features/system/TRACKS.md) |
| SYN-PRISM | Moteur Prism | CONFIRME | [synthese](features/synthesis/ENGINES.md) |
| SYN-STACK | Moteur Stack | CONFIRME | [synthese](features/synthesis/ENGINES.md) |
| SYN-WAVE | Moteur Wavetable | CONFIRME | [synthese](features/synthesis/ENGINES.md) |
| SYN-FM | Moteur FM/DX7 et import SysEx | PARTIEL | [synthese](features/synthesis/ENGINES.md) |
| SYN-ACID | Moteur Acid | CONFIRME | [synthese](features/synthesis/ENGINES.md) |
| DRUM-MD | Moteur Drum MD | CONFIRME | [synthese](features/synthesis/ENGINES.md) |
| SMP-STREAM | Sampler STREAM/Classic et source REC | CONFIRME | [sampler](features/sampler/SAMPLER.md) |
| SMP-MULTI | Instruments MULTI | CONFIRME | [sampler](features/sampler/SAMPLER.md) |
| SEQ-01 | Sequence, pages, longueur, direction, division | CONFIRME | [sequenceur](features/sequencer/SEQUENCER.md) |
| SEQ-02 | PLAY, p-locks, roll, microtiming, live record | CONFIRME | [sequenceur](features/sequencer/SEQUENCER.md) |
| NFX-01 | Generator, Voicer, Scaler, TRIG | CONFIRME | [MIDI FX](features/midi_fx/MIDI_FX.md) |
| MOD-01 | 3 LFO, ENV3, Matrix, Multi/Slew | CONFIRME | [modulation](features/modulation/MODULATION.md) |
| AFX-01 | Deux slots FX, routing, effets et XFADE | CONFIRME | [effets](features/audio_fx/AUDIO_FX.md) |
| MIX-01 | Mixer, sends et Master reverb/delay/comp | CONFIRME | [effets](features/audio_fx/AUDIO_FX.md) |
| SCENE-01 | 14 Scenes PRESSURE/TOGGLE/ASSIGN | CONFIRME | [Scenes](features/modulation/SCENES.md) |
| PERSIST-01 | Patterns, Working et recall quantifie | CONFIRME | [persistence](features/projects/PERSISTENCE.md) |
| PERSIST-02 | Projects New/Load/Save/Save As | CONFIRME | [persistence](features/projects/PERSISTENCE.md) |
| PERSIST-03 | Patches Save/Load/Rename/Delete/Preview | CONFIRME | [persistence](features/projects/PERSISTENCE.md) |
| ASSET-01 | Browsers Sample/Wavetable/Multi, conversion et preview | PARTIEL | [assets](features/sampler/ASSETS.md) |
| REC-01 | Audio REC, trim, audition, save et assign | CONFIRME | [recording](features/recording/AUDIO_RECORDING.md) |
| MIDI-01 | MIDI DIN/USB, clock, transport et routage | PARTIEL | [MIDI/USB](features/midi_usb/MIDI_USB.md) |

Les parametres et pages sont comptes separement dans [audit/COVERAGE.md](audit/COVERAGE.md).
