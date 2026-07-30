# AUDIO TEST 2

`Settings > Test > Audio 2` est disponible uniquement dans les firmwares
`Debug` et `Test`. Les sources, buffers, textes UI et hooks correspondants sont
exclus de `Release` et `Premium`.

## Déroulement

- `PAGE 2` démarre la phase INTERNAL, puis confirme LINE et HEADPHONE.
- `PAGE 1` annule la phase en cours et restaure transport et playheads.
- `PAGE 3` rejoue LINE sans refaire INTERNAL.
- `PAGE 4` rejoue HEADPHONE sans refaire INTERNAL.
- `SETTINGS` annule proprement et quitte.

La phase INTERNAL crée et vérifie :

```text
/AUDIO_TEST2/REFERENCE.WAV
/AUDIO_TEST2/INTERNAL.WAV
/AUDIO_TEST2/MANIFEST.CSV
/AUDIO_TEST2/RUN.CSV
```

Les deux WAV sont stéréo PCM24, 48 kHz, 11 904 000 frames, soit 4 min 08 s.
Le manifeste contient 45 sections. Les CRC de `RUN.CSV` portent sur les seuls
octets PCM, sans l'en-tête WAV.

Le générateur entier déterministe remplace directement les bus MAIN et CUE dans
`audio_process_block_int32()`, avant `board_audio_pack_output()`. Engines,
mixer, effets, macro master, métronome et gains de production ne sont donc pas
parcourus. `INTERNAL.WAV` capture les mots PCM24 produits à ce seam. Pendant
LINE et HEADPHONE, aucun service FatFs propre au test n'est appelé.

Sur Premium, le même signal est envoyé aux slots MAIN et CUE pour couvrir les
routes line et casque réelles. Sur Low-cost, le codec reçoit son unique paire
stéréo de sortie : la distinction line/casque dépend donc du routage analogique.

## Enregistrement externe

Noms recommandés :

```text
BRICK6_LOW_LINE.wav
BRICK6_LOW_HEADPHONE.wav
BRICK6_PREMIUM_LINE.wav
BRICK6_PREMIUM_HEADPHONE.wav
```

Dans Audacity :

- fréquence projet et périphérique : 48 kHz ;
- enregistrement : 24 bits ou 32-bit float, deux canaux ;
- aucune normalisation, égalisation, suppression de bruit ou autre effet ;
- aucun contrôle automatique de gain ;
- gain d'entrée Scarlett fixe et assez bas pour laisser de la marge ;
- niveau casque réglé manuellement à un niveau sûr avant confirmation.

Le compte à rebours de trois secondes est silencieux. Démarrer Audacity avant
la confirmation. Ne pas couper les impulsions initiales ni le silence final.

## Outils PC

Recréation et vérification bit-à-bit :

```text
python tools/audio_test2_reference.py --output REFERENCE_PC.WAV \
  --manifest MANIFEST_PC.CSV --verify REFERENCE.WAV
```

Deux exécutions doivent afficher le même CRC. Analyse (nécessite NumPy) :

```text
python tools/audio_test2_analyze.py \
  --reference REFERENCE.WAV --internal INTERNAL.WAV \
  --line BRICK6_LOW_LINE.wav \
  --headphone BRICK6_LOW_HEADPHONE.wav \
  --manifest MANIFEST.CSV
```

`--loopback SCARLETT_LOOPBACK.wav` est optionnel. L'analyse ne corrige que le
décalage temporel et la dérive d'horloge linéaire. Elle n'applique ni gain,
normalisation ni égalisation.

## Limites

- La réponse, le bruit et la distorsion mesurés restent bornés par la Scarlett,
  son réglage de gain, ses câbles et sa calibration.
- Les avertissements de limite Scarlett sont génériques et non une calibration.
- Le volume casque analogique non pilotable n'est jamais modifié par le test.
- Un overrun SD invalide INTERNAL et impose de relancer la phase.
