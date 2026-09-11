# Z4 - Sequence, clock, Note FX et evenements live

`seq_model` contient seize lanes de 64 steps avec un pool de 512 p-locks par lane. Les lanes `0..7` sont top-level; `8..15` sont actives avec GROUP 7. Top-level/master portent jusqu'a huit PLAY par step, un child un seul. Seules les lanes actives sont jouees.

Chaque PLAY porte NOTE, VELOCITY, LENGTH et MICROTIMING avec masque de presence; une valeur absente herite de la base de lane. ROLL reste structurel. L'edition multi-step est atomique et une edition du playhead exige le gardien REC.

Le timestamp PLAY est resolu une seule fois dans le scheduler CONTROL a partir de la boundary nominale de lane. Les occurrences impaires de la lane recoivent le SWING (`0%` droit, `100%` = retard d'un demi-step de lane); cette phase alternee ne depend pas du rebouclage du pattern. Le MICROTIMING est ensuite converti sur la cadence de base et reduit symetriquement par QUANT (`0%` conserve, `100%` annule). ROLL derive tous ses retriggers de cette origine finale; Note FX recoit donc un timestamp deja definitif. Les sources du step suivant sont ouvertes a la boundary precedente pour couvrir le MICROTIMING negatif sans logique temporelle AUDIO. Un ledger d'emission par grid point distingue une occurrence seulement observee d'une occurrence materialisee: la premiere, si elle devient tardive, est clampee au premier sample encore publiable et emise exactement une fois; la seconde n'est jamais rejouee.

CONTROL conserve le futur musical sous forme de sources actives et de deadlines. Sa superloop se cadence sans appel AUDIO: TIM12 avance le tick musical interne et CONTROL convertit directement la timeline TIM5 absolue en samples. Elle maintient un horizon glissant de 64 frames en avant du temps physique connu. Les actions finales du scheduler, des expirations/steals/fermetures et de Note FX sont reunies dans les buckets de cette fenetre puis emises par sample croissant; l'ordre de decouverte interne ne peut donc pas violer la monotonie de la FIFO. Toute fermeture demandee entre deux fenetres ouvre le meme bucket CONTROL au premier sample encore publiable; aucun STOP ne contourne la finalisation. `g_seq_play_events`, les futurs Off longue duree et la pre-expansion ROLL n'existent plus.

La boundary conserve sa date nominale pour la phase musicale. Si CONTROL reprend
apres cette date, ses restaurations et applications de p-lock convergent au
premier sample de l'horizon encore modifiable; elles ne republient jamais une
date anterieure a la fenetre CONTROL_RT. Les restores de lifecycle recoivent de
meme explicitement la date de transition, sans reutiliser une ancienne date de
step.

L'admission AUDIO d'un p-lock est decidee par l'autorite temporaire du registry
(`param_registry_track_temp_is_applicable`), commune a l'application et au
restore. Elle couvre TONE, ENV, MIX, Audio FX et les LFO; les MIDI FX gardent
leur terminal CONTROL dedie.

La preuve de capacite produit fixe 64 voix emettrices dans les deux topologies, 128 NOTE_ON au plus par horizon (deux sources adjacentes) et 256 actions terminales au plus. Le ledger scheduler accepte les trois generations de sources prouvees afin de conserver leurs deadlines jusqu'au verdict terminal; il ne borne plus une track a sa polyphonie et ne choisit plus de victime. Le preflight de fenetre valide horizon, sources, occurrences, staging et contrat FIFO avant toute avance musicale; un depassement interne residuel est enregistre par le fatal central avec message, fichier, ligne et fonction, en conservant code, entite, contexte, demande et capacite.

Un Pattern arme borne cet horizon au sample exact de sa prochaine boundary de
lane. CONTROL applique alors le nouveau Pattern avant de construire la fenetre
suivante; aucune NOTE, restauration de p-lock ou occurrence ROLL de l'ancien
Pattern ne peut franchir cette boundary.

LENGTH est une deadline CONTROL associee a l'output actif. Le scheduler propose les actions datees sans appliquer de quota. `control_music_output` arbitre seul l'admission et le stealing, ajoute le STOP de la victime avant le START au meme sample, puis notifie les proprietaires apres commit, y compris lorsqu'un output cree et vole dans la meme fenetre n'avait jamais rejoint le ledger precedent. L'occurrence scheduler morte et tout STOP imminent correspondant sont invalides. Une extension ne fait que repousser une deadline encore vivante. ROLL reste reference a l'origine du PLAY et ne materialise que ses retriggers imminents. NOTE, VELOCITY, MICROTIMING et ROLL live n'affectent que les occurrences non publiees.

La cible de capture NOTE d'edition appartient au contexte de track selectionne. Un changement de track ferme la transaction de la cible sans oublier les occurrences NOTE_ON deja capturees: leurs NOTE_OFF restent consommes, mais aucune note ulterieure ne peut modifier l'ancienne track.

Trois slots MIDI FX S1..S3 precedent un terminal CONTROL explicite. A chaque boundary, restaurations puis overrides MIDI FX sont appliques directement par CONTROL avant la note. Le ledger source ARP/LIVE conserve uniquement les sources musicalement actives et l'ownership necessaire aux sorties Note FX. Les reservations On/Off, quotas de demi-buffer, pending closures et retries Off ont ete retires. Le terminal transforme ses actions en NOTE ON/OFF finales; un retrigger est toujours OFF puis ON au meme sample. AUDIO ne connait ni PLAY, ni ROLL, ni ARP, ni EUCLID et ne refuse pas normalement une NOTE legale.

Undo/Redo conserve huit transactions structurelles. No-op n'est pas capture, une nouvelle branche purge Redo, Copy ne cree pas de transaction et Paste pre-valide le pool avant mutation atomique. Pattern/Project reussis invalident l'historique.

## Horodatage live

Hall, USB MIDI Device, USB MIDI Host et encodeurs capturent TIM5 a l'ingestion avec un `ingress_serial` monotone. CONTROL convertit directement cette capture et applique une garde fixe de 64 samples. La valeur effective n'est calculee qu'une fois.

Hall publie un evenement fixe de 16 octets dans une FIFO bornee; Device conserve 128 paquets et Host 64. Les files rejettent deterministement le plus recent a saturation et incrementent leurs diagnostics.

La file live CONTROL contient 31 occurrences fixes, triees par `(sample_time, ingress_serial)`. Une echeance future reste en attente; une echeance tardive est clampee au premier sample modifiable. SEQ, Hall, MIDI et Note FX convergent ensuite vers le meme contrat musical final. Aucun audio deja rendu n'est reecrit.

Panic CC120/123 publie PANIC dans la FIFO fonctionnelle unique. L'ordre FIFO place la fermeture avant les commandes suivantes sans generation, queue prioritaire ou purge laterale. Le Live Recording conserve le MICROTIMING brut et l'heure effectivement entendue; QUANT reste une projection non destructive du resolver PLAY.

## Commit d'horizon

La construction d'un horizon ne commence que si les 1827 places du burst
contractuel sont disponibles. Collecte, Note FX et sorties terminales restent
invisibles jusqu'au commit FIFO unique. Le curseur
`g_seq_runtime_control_sample_cursor` avance seulement apres ce commit; un refus
de reservation declenche le fatal d'invariant et ne consomme aucun etat
producteur ni aucune fenetre musicale.
