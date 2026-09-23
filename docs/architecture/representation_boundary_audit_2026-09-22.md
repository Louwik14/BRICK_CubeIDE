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
2. Deux IDs internes obsoletes etaient admis par le validateur CONTROL alors
   qu'aucun producteur et aucun consumer AUDIO ne les implementaient. Ces
   anciennes entrees de grammaire et leur
   logique de snapshot ont ete retirees. CONTROL ne peut donc plus accepter une
   commande que AUDIO rejetterait.
3. AUDIO_GLOBAL encode exactement les 51 floats CONTROL. Les trois anciens
   slots DJ EQ globaux ont ete retires; les cardinalites de chaque sous-bloc et
   le total CONTROL/wire sont proteges a la compilation.
4. FILTER encode ses douze parametres actuels, sans les quatre anciens slots
   Drive/Decimator. L'etat CONTROL et sa validation ont ete compactes de meme.
5. L'ancien type Drum Analog, son payload Tone et sa conversion vers Drum MD
   ont ete retires du modele, du catalogue de cles et du codec.

Aucune autre divergence semantique prouvee n'a ete trouvee dans les frontieres
auditees. Les 71 ordinaux Param reserves ont ensuite ete retires: le catalogue
interne CONTROL/AUDIO/SEQ est dense et partage la meme autorite symbolique.
Les cles persistantes courantes restent independantes des enums C; aucun bump
du format B6CP v12 n'est requis. Les handles d'assets sont reconstruits au
runtime.

## Protections et cout

Les assertions ajoutent des contrats pour les capacites STORAGE/SEQ
(16 lanes, 64 steps, 8 PLAY, 32 p-locks), Note FX (slots, payload sans le model,
stride des IDs), modulation (3 LFO, 8 routes) et AUDIO_GLOBAL. Apres retrait du
bloc de routes Looper, le format B6CP v12 accepte un Pattern de 115 481 octets
maximum et un Project de 29 845 875 octets maximum; encode et decode appliquent
les memes enveloppes.
