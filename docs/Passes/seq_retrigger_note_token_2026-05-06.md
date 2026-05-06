# Seq retrigger same-note token - 2026-05-06

## Cause racine

Le scheduler planifiait les `NOTE_OFF` par couple `track/note` seulement. Un second trig du meme pitch pouvait armer une nouvelle occurrence avant la fin planifiee de la precedente; le `NOTE_OFF` ancien etait alors applique sans identite d'occurrence et coupait la note recente.

## Patch

- `seq_play_scheduler` attribue maintenant un `event_token` a chaque couple `NOTE_ON`/`NOTE_OFF` planifie.
- L'application aval conserve le token actif par `track/note`.
- Un `NOTE_OFF` n'est applique que si son token correspond encore a l'occurrence active.
- Un retrig de meme pitch ferme explicitement l'occurrence precedente avant d'armer le nouveau token.

## Verification manuelle legere

Scenario:
1. Track moteur ou MIDI avec deux steps consecutifs actifs.
2. Step 0: `NOTE=60`, `VEL>0`, `LEN=2`.
3. Step 1: `NOTE=60`, `VEL>0`, `LEN=1`.
4. Lancer le transport.

Attendu:
- deux attaques distinctes sont entendues;
- la fin du step 0 n'eteint pas le step 1;
- le `NOTE_OFF` du step 1 eteint la note.

Audit voisins:
- `LEN` reste clamp a `1..64` et converti en samples avec minimum 1 sample.
- Aucune logique `tie`/`slide` n'est presente dans le scheduler PLAY audite.
- `seq_play_scheduler_clear()` vide la queue et les tokens actifs au stop/reset.
