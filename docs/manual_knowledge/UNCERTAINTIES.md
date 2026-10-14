# Incertitudes et validations materielles

| ID | Statut | Question/test requis | Sources |
|---|---|---|---|
| U-01 | PARTIEL | Confirmer que la cible Release employee bien la facade LowCost et relever les libelles serigraphies correspondant aux IDs logiques. | `CMakeLists.txt`, `Board/LowCost/Src/board_controls_lowcost.c` |
| U-02 | PARTIEL | Verifier sens, acceleration et poussoirs des quatre encodeurs sur hardware. | `Drivers/Drv_app/Src/encoders.c`, `Inc/Board/board_controls.h` |
| U-03 | PARTIEL | Mesurer seuils/velocity du Hall et valider calibration HALL KBD/HALL VEL. | `Src/App/Hall/*`, `Src/UI/pages/ui_page_calibration.c` |
| U-04 | PARTIEL | Tester import DX7 SysEx depuis le support reel ; le decodeur/import existe, l'entree utilisateur complete n'est pas prouvee par une page active. | `Src/Import/dx7_import.c`, `dx7_sysex.c`, `docs/architecture/dx7_import.md` |
| U-05 | PARTIEL | Identifier la commande UI actuelle des modes clock/destination ; les APIs et persistence existent, mais aucune page Settings explicite n'a ete trouvee. | `Src/MIDI/midi.c`, `Src/Seq/seq_clock_bridge.c`, `Src/UI/pages/Settings/*` |
| U-06 | PARTIEL | Confirmer DIN UART et USB Host sur la carte : commentaires d'API les decrivent comme stubs alors que USB Device TinyUSB est cable. | `Inc/MIDI/midi.h`, `Src/MIDI/midi_host.c`, `Board/LowCost/UsbStack/*` |
| U-07 | PARTIEL | Tester debit simultane STREAM + Recorder et messages d'erreur/underrun sur SD cible. | `docs/architecture/stream_rec_perf_baseline.md`, `recorder_sd.md` |
| U-08 | PARTIEL | Tester audition Patch/Sample au relachement et lors d'un changement de page ou retrait SD. | browsers UI, `Src/Storage/sd_preview.c`, `patch_preview.c` |
| U-09 | NON VERIFIE | Les boutons encodeur sont declares mais aucun comportement utilisateur transversal n'a ete trouve ; verifier absence de geste cache specifique carte. | `buttons_ids.h`, recherche des `BTN_ENCODER_*_PUSH` |
| U-10 | PARTIEL | Confirmer rendu/son des modeles FX marques `-` et du modele Prism `????`; le catalogue conserve des trous/labels non finalises. | `param_registry_catalog.c`, `audio_fx_param_catalog.c` |

Aucune fonctionnalite n'est declaree testee sur materiel par cette base.
