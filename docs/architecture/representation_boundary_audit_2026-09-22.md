# Audit des frontieres STORAGE / CONTROL / AUDIO / SEQ

Audit realise sur le HEAD `fcc1d5cd9` et le worktree courant du 22 septembre
2026.

## Perimetre verifie

- STORAGE vers CONTROL: codecs Pattern, Project, Patch, AUDIO_GLOBAL, Tone/FM,
  Filter/VCA/Mix/Audio FX, polyphonie, Note FX, modulation/LFO, sequences,
  PLAY, p-locks, topologie, routes et references Sample/Wavetable/Multi.
- Candidats Pattern/Project vers CONTROL: validation, capacites, owners,
  installation et publication transactionnelle.
- CONTROL vers AUDIO: grammaire `control_audio_command`, PROGRAM, PARAM,
  snapshot durable, engines/types/flags/voix et dispatch des IDs internes.
- CONTROL vers SEQ: lanes, steps, PLAY, locks, Note FX, timing, traversal,
  generation et publication.
- Tables paralleles: Param IDs/spec/registry, cles persistantes, catalogs de
  types/engines, Note FX, LFO et sources de modulation.

## Divergences et corrections

1. Le descriptor Param des trois modeles Note FX declarait encore une plage
   `0..8` alors que CONTROL, STORAGE, les labels, SEQ et le moteur AUDIO ne
   possedent plus que sept modeles (`0..6`). La borne est maintenant derivee de
   `NOTE_FX_MODEL_COUNT`; la cardinalite des labels et la disposition contigue
   des IDs sont verrouillees par assertions compile-time.
2. Deux IDs internes Looper etaient admis par le validateur CONTROL alors
   qu'aucun producteur et aucun consumer AUDIO ne les implementaient. Ces
   anciennes entrees de grammaire (`LOOPER_ROUTE`, `LOOPER_PLAY_AUTO`) et leur
   logique de snapshot ont ete retirees. CONTROL ne peut donc plus accepter une
   commande que AUDIO rejetterait.
3. Le mapping AUDIO_GLOBAL v4 reste volontairement asymetrique: 54 floats sur
   le wire, 51 floats CONTROL et trois slots DJ EQ legacy consommes/ignores.
   Les cardinalites de chaque sous-bloc, le total runtime et le total wire sont
   maintenant proteges a la compilation; aucun padding mort n'a ete ajoute au
   runtime.

Aucune autre divergence semantique prouvee n'a ete trouvee dans les frontieres
auditees. Les asymetries volontaires conservees sont les slots AUDIO_GLOBAL
legacy, les cles stables independantes des enums C, les tombstones Param/Track,
et les handles d'assets reconstruits au runtime.

## Protections et cout

Les assertions ajoutent des contrats pour les capacites STORAGE/SEQ
(16 lanes, 64 steps, 8 PLAY, 32 p-locks), Note FX (slots, payload sans le model,
stride des IDs), modulation (3 LFO, 8 routes) et AUDIO_GLOBAL. Elles n'ajoutent
aucun cout RAM/CPU/Flash. Le retrait des branches mortes reduit le build mesure
de 96 octets de Flash; les regions RAM sont inchangees.

Le build CMake preset `Release` avec LTO et section GC passe. Mesure finale:
Flash 1 564 788 octets (85,27 %), DTCMRAM 128 640, RAM_D1 490 400,
SRAM2_D2 130 496, SDRAM 32 383 712 octets.
