# Z4 - Sequence, clock, Note FX et evenements live

`seq_model` contient seize lanes de 64 steps avec un pool de 512 p-locks par lane. Les lanes `0..7` sont top-level; `8..15` sont actives avec GROUP 7. Top-level/master portent jusqu'a huit PLAY par step, un child un seul. Seules les lanes actives sont jouees.

Chaque PLAY porte NOTE, VELOCITY, LENGTH et MICROTIMING avec masque de presence; une valeur absente herite de la base de lane. ROLL reste structurel. L'edition multi-step est atomique et une edition du playhead exige le gardien REC.

Trois slots MIDI FX S1..S3 precedent un terminal explicite. Le terminal separe admissions internes et MIDI, ferme avant d'ouvrir au meme sample et conserve les retries Off. Le MIDI ne traverse pas l'admission des moteurs AUDIO.

Undo/Redo conserve huit transactions structurelles. No-op n'est pas capture, une nouvelle branche purge Redo, Copy ne cree pas de transaction et Paste pre-valide le pool avant mutation atomique. Pattern/Project reussis invalident l'historique.

## Horodatage live

Hall, USB MIDI Device, USB MIDI Host et encodeurs capturent TIM5 a l'ingestion avec un `ingress_serial` monotone. AUDIO convertit contre son ancrage SAI coherent et applique une garde fixe de 64 samples. La valeur effective n'est calculee qu'une fois.

Hall publie un evenement fixe de 16 octets dans une FIFO bornee; Device conserve 128 paquets et Host 64. Les files rejettent deterministement le plus recent a saturation et incrementent leurs diagnostics.

La file live AUDIO contient 31 occurrences fixes, triees par `(sample_time, ingress_serial)`. Une echeance future reste en attente; une echeance tardive est clampee au premier sample modifiable. Les deadlines live, sequence et Note FX segmentent le meme bloc audio. Aucun audio deja rendu n'est reecrit.

Panic CC120/123 utilise une commande prioritaire separee et idempotente, invalide les anciennes epochs, ferme les admissions et retente toute fermeture refusee. Le Live Recording reutilise exactement l'heure effective entendue; la quantification ne modifie que la representation enregistree.
