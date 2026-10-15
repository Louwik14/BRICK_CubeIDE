# Limites et comportements particuliers

- 8 pistes top-level ; 16 identites totales avec 7 children GROUP et Master 15. GROUP seulement sur top-level 7 ; children forces SAMPLER/RAM mono.
- Maximum 8 pistes STREAM/GROUP avec Shifter (`BRICK6_MAX_CLIP_TRACKS`), dans la limite commune des 8 VoiceReaders musicaux partages avec Multi. External arbitre un nombre borne d'entrees physiques et interdit le partage conflictuel.
- Sequence : 64 steps, 4 pages de 16 ; 32 locks par step ; pool global par lane de 512 locks ; PLAY jusqu'a 8 voix.
- Roll : Off puis 1/20, 1/24, 1/32, 1/40, 1/48, 1/64 sur grille interne 60 points.
- Projects : 16 slots. Patterns : 256 slots par projet. Noms : 32 caracteres, espace/alphanumerique/`_`/`-`, trim des espaces externes.
- Un seul Pattern vivant et un seul candidat recall ; Working n'est pas une banque RAM.
- Save Project/Save As refuse transport RUNNING ou START_PENDING sans lancer STOP.
- Les operations SD partagent un scheduler cooperatif ; une transaction ou requete RT active rend les autres NOT_NOW.
- Project Load apres `T_FORWARD` est forward-only : perte media = ancien projet non restaure.
- STREAM accepte source POOL ou REC. Les edits Start/Length sont throttles a 12 Hz dans le chemin live. Les formats WAV non compatibles exigent conversion et peuvent imposer arret audio.
- Les selections d'asset ne sont ni p-lockables ni modulables. MODEL Audio FX est p-lockable? Non : seuls P1/P2/P3 le sont. Generator P4 MIDI FX n'est pas p-lockable.
- Master 15 n'a ni sequence, mute, clipboard Track, Undo, slot persistant ni sortie notes.
- DIN/USB Host comportent des portions stubs dans l'API MIDI ; voir `UNCERTAINTIES.md` avant promesse utilisateur.
- SHIFT+PLAY appelle directement l'entree ROM DFU : combinaison systeme destructive pour la session courante, a documenter explicitement avant diffusion utilisateur.
- La preuve code ne couvre pas les niveaux analogiques, latences percues, debouncing reel, calibration physique, qualite sonore, comportement carte SD marginal ou compatibilite avec chaque peripherique USB/MIDI.
