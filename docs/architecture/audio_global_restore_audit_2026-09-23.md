# Audit AUDIO_GLOBAL restore — 2026-09-23

Le rejet `PERSIST_CODEC_INVALID_ENTITY` au stage `AUDIO_GLOBAL` ne venait pas
du codec v12 ni du retrait des trois slots DJ EQ. Le premier retour faux etait
dans `param_global_control_restore()`, pour le slot 42 `PARAM_MODFX_DEPTH_B` :
la valeur CONTROL par defaut/persistee `123.0` et le modele MOD FX `OFF`
produisaient une commande AUDIO `123 / 127 = 0.9685039`, rejetee par
`param_spec_audio_command_value_is_valid()` dont la borne consumer est `0.93`.
La publication du bulk n'etait donc jamais tentee.

Le DTO et le wire v12 contiennent bien 51 floats dans l'ordre suivant : 2 sends,
8 bus compressor, 4 saturation, 7 reverb, 14 delay, 9 MOD FX, 4 compressor et
3 output. Cet ordre est identique dans le codec, le candidat Pattern, la table
CONTROL et le consumer AUDIO. Les anciens slots DJ EQ et la cardinalite 54 ne
sont plus presents.

La projection des deux profondeurs MOD FX convertit maintenant toute valeur
CONTROL `0..127` vers le domaine AUDIO `0..0.93`, quel que soit le modele. Les
bounds de commande sont partages par projection, validation et consumer. La
cardinalite 51 est une constante CONTROL unique, utilisee aussi par le codec,
avec assertions de taille et de cardinalite.

L'audit cible des restores similaires a couvert FILTER, VCA, MIX, Tone (dont
STREAM), Audio FX, LFO, ENV3/modulation, polyphonie et configuration globale de
track. Une seconde divergence a ete corrigee : `ENV3.retrigger_hard` etait bien
decode, valide et installe dans CONTROL, mais absent du bulk AUDIO. Le parametre
explicite `PARAM_ENV_RETRIG_MOD` ferme maintenant le chemin DTO -> CONTROL ->
AUDIO; sa table de restore est liee a la taille de l'etat par assertion.

Le build CMake `Release` passe avec LTO et section GC. Mesure : Flash 1 552 300
octets (84,59 %), DTCMRAM 128 640, RAM_D1 490 112, SRAM2_D2 130 496 et SDRAM
32 358 816 octets.
