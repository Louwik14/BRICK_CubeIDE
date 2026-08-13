# Z6 — Modèle persistant canonique CONTROL

`persistent_control_model.h` définit les DTO logiques de Project, Pattern et
Patch. Ces types ne sont ni des snapshots runtime, ni une ABI disque. La future
sérialisation encode chaque champ explicitement et n'écrit jamais ces objets au
moyen de `sizeof`.

Le Pattern représente les seize identités canoniques : huit entités top-level,
dont le master GROUP 7, et les children 8 à 15. Chaque entité possède sa config,
ses paramètres de base, son mute propre, sa référence d'asset, sa séquence et,
si sa capacité l'autorise, ses Note FX. La validation impose huit items PLAY au
maximum pour top-level/master, un seul pour un child, aucun Note FX master et
des p-locks master strictement locaux.

Les helpers purs `persist_control_entity_role`,
`persist_control_entity_play_limit`, `persist_control_entity_allows_note_fx` et
`persist_control_entity_is_mod_owner` portent ces cardinalités sans dépendre de
la topology runtime. La future capture/application doit refuser tout DTO qui ne
respecte pas ces invariants.

La modulation GROUP est un objet unique appartenant au master. Elle contient
les sources/opérateurs et les huit routes complètes. Une destination est le
couple logique `entity_id + parameter_key`; elle ne contient aucune cible mixer
ou moteur. Le routing Pattern est également exprimé entre entités logiques.

Les paramètres, familles, types et genres d'assets utilisent des clés produit
stables. Une clé de paramètre combine un namespace sémantique et un code stable;
elle n'est jamais obtenue par cast de `param_id_t`. Les valeurs restent typées
selon leur sens, notamment `float32` lorsque la valeur CONTROL est un float.
Les familles, types, sources MIDI, horloges, modèles Note FX et sources MOD
possèdent dès cette passe des valeurs produit explicites. Un p-lock désigne
directement sa `parameter_key`; aucun ordinal de set du séquenceur n'entre dans
son identité persistante.

Une référence d'asset contient seulement un identifiant local logique, un genre
et un chemin borné. Les indices backend/global/cache/RAM et les états de
chargement sont reconstruits au chargement et restent hors du modèle.

Le Project contient le working Pattern, la sélection active, le manifeste
d'assets et les macros/scènes. Sa banque est un flux de
`persist_control_pattern_record_t`: cette représentation couvre les 256
Patterns sans imposer une image statique maximale en RAM. Le Patch contient
uniquement nom, famille/type, paramètres logiques et référence d'asset.

Sont explicitement exclus : bindings et générations, instances moteur, cibles
mixer, voix physiques, caches/target-caches AUDIO, pointeurs et buffers, état
DSP, smoothing, phases et enveloppes runtime, playhead, transport actif, notes
actives, admission/stealing, mute effectif hérité, bus GROUP physique et état de
sélection/navigation UI.
