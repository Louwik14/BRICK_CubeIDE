# NFX-01 — MIDI FX

## Statut

CONFIRME.

## Chaine et acces

MIDI FX est une chaine fixe `GENERATOR -> VOICER -> SCALER -> TRIG`, quatre champs par stage, accessible par l'ensemble FX lorsque la lane peut emettre des notes. PAGE 1..4 choisit le stage ; P4 est son mode/bypass principal.

Generator : OFF, ARP, HOLD, EUCLID ; P1..P3 changent de label/semantique selon le mode sans effacer leurs valeurs. Euclid longueur 1..64, default 16, pulses default 4. Voicer offre types/modes, spreads et inversions. Scaler : OFF distinct de CHROMATIC, gammes Major/Natural Minor/Dorian/Mixolydian/Pentatoniques, transpose/key et politiques de sticking. TRIG applique chance, lot GROUP/POLY/division et keep OFF/LOOP/division.

Les transformations s'appliquent aux notes live et sequence avant le terminal harmonique Master. Les 16 octets canoniques sont persistants et copiables. Tous les champs admis sont p-lockables sauf le mode Generator P4 ; aucun ordre de slots configurable n'existe.

## Sources

`Inc/NoteFx/note_fx_contract.h`, `Src/NoteFx/{note_fx_state,note_fx_engine,note_fx_euclid}.c`, `Src/UI/pages/ui_page_midi_fx.c`.
