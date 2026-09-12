# Z4 - Sequence, clock, Note FX et evenements live

`seq_model` contient seize lanes de 64 steps avec un pool de 512 p-locks par lane. Les lanes `0..7` sont top-level; `8..15` sont actives avec GROUP 7. Top-level/master portent jusqu'a huit PLAY par step, un child un seul. Seules les lanes actives sont jouees.

Chaque PLAY porte NOTE, VELOCITY, LENGTH et MICROTIMING avec masque de presence; une valeur absente herite de la base de lane. ROLL reste structurel. L'edition multi-step est atomique et une edition du playhead exige le gardien REC.

Le timestamp PLAY est resolu une seule fois dans le scheduler CONTROL a partir de la boundary nominale de lane. Les occurrences impaires de la lane recoivent le SWING (`0%` droit, `100%` = retard d'un demi-step de lane); cette phase alternee ne depend pas du rebouclage du pattern. Le MICROTIMING est ensuite converti sur la cadence de base et reduit symetriquement par QUANT (`0%` conserve, `100%` annule). ROLL derive tous ses retriggers de cette origine finale; Note FX recoit donc un timestamp deja definitif. Les sources du step suivant sont ouvertes a la boundary precedente pour couvrir le MICROTIMING negatif sans logique temporelle AUDIO. Un ledger d'emission par grid point distingue une occurrence seulement observee d'une occurrence materialisee: la premiere, si elle devient tardive, est clampee au premier sample encore publiable et emise exactement une fois; la seconde n'est jamais rejouee.

CONTROL conserve le futur musical sous forme de sources actives et de deadlines. Sa superloop se cadence sans appel AUDIO: TIM12 avance le tick musical interne et CONTROL convertit directement la timeline TIM5 absolue en samples. Le curseur de publication est borne par le dernier sample effectivement termine par AUDIO, publie dans le contrat FIFO partage; l'horizon glissant ne depasse donc pas 64 frames de ce consumer. Si AUDIO n'avance plus, CONTROL n'empile pas de fenetres supplementaires, meme si TIM5 continue de progresser. Les timestamps des commandes live restent resolus depuis TIM5 et le plancher CONTROL_RT, sans changer la phase musicale nominale. Avant chaque admission, le curseur candidat est resolu contre le plancher global CONTROL_RT; une avance intervenue depuis la fenetre precedente ne peut donc pas rendre le nouvel horizon retrograde. L'admission CONTROL_RT reste stricte et refuse encore tout producteur qui presente une date obsolete. Les actions finales du scheduler, des expirations/steals/fermetures et de Note FX sont reunies dans les buckets de cette fenetre puis emises par sample croissant; l'ordre de decouverte interne ne peut donc pas violer la monotonie de la FIFO. Toute fermeture demandee entre deux fenetres ouvre le meme bucket CONTROL au premier sample encore publiable; aucun STOP ne contourne la finalisation. `g_seq_play_events`, les futurs Off longue duree et la pre-expansion ROLL n'existent plus.

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

LENGTH est une deadline CONTROL de la source scheduler. Le scheduler reste
l'autorite de sa fin canonique. `control_music_output` arbitre l'admission
terminale et le stealing puis notifie le scheduler de l'occurrence logique
remplacee. Pour `(track, destination, pitch)`, il ferme l'activation HELD
courante avant le nouveau START au meme sample; l'ancien OFF supersede devient
un no-op. ROLL reste reference a l'origine du PLAY, utilise exactement 60
points de grille representables dans son masque `uint64_t`, puis chaque
occurrence traverse les quatre slots MIDI FX. NOTE, VELOCITY, MICROTIMING et
ROLL live n'affectent que les occurrences non publiees.

La cible de capture NOTE d'edition appartient au contexte de track selectionne. Un changement de track ferme la transaction de la cible sans oublier les occurrences NOTE_ON deja capturees: leurs NOTE_OFF restent consommes, mais aucune note ulterieure ne peut modifier l'ancienne track.

Quatre slots MIDI FX S1..S4, chacun avec la meme grammaire
TYPE/PARAM1/PARAM2/PARAM3 et integralement p-lockable/persistee avec Pattern et
Project, precedent un terminal CONTROL explicite. Les evenements ROLL, produits
par le scheduler avant la chaine, passent dans deux buffers ping-pong bornes et
une boucle commune aux quatre slots. Ils portent une identite source scheduler,
un `occurrence_id` d'activation, un `group_id` de correlation sans ownership et
la generation de chaine. Le pipeline CONTROL conserve desormais, separement
des sorties terminales, jusqu'a huit sources musicales HELD par track. ARP et
EUCLID possedent leur phase, leur prochaine deadline et leur projection locale
de ces pitches; chaque frontiere de slot conserve ses entrees HELD. CHORD et
HARMONIZER les utilisent pour revoicer, et un cutover TYPE reprend directement
a la premiere frontiere modifiee sans rejouer les effets amont. La borne
logique admise avant ces FX est huit
pitches distincts par track, ou la borne inferieure prouvee par la chaine.
AUDIO ne connait ni PLAY, ni ROLL, ni ARP, ni EUCLID.

Un changement parametrique ne change plus la generation de chaine, ne ferme
plus l'entite et ne purge plus la future queue. ARP conserve phase et prochaine
deadline: MODE/OCTAVE sont lus a la prochaine occurrence et RATE fixe la duree
et la deadline suivante depuis cette occurrence. EUCLID conserve de meme sa
deadline; LENGTH/PULSES reconstruisent le masque a la prochaine decision et la
phase est projetee modulo la nouvelle longueur. Probability, Gate et Groove
lisent l'etat effectif lorsqu'une occurrence atteint leur slot. Une decision
Probability, une fin Gate ou une projection Groove deja materialisee n'est pas
rejouee. CHORD et HARMONIZER ferment seulement les sorties causees par leurs
entrees encore HELD, puis republient l'ancienne matiere avec le nouveau voicing
au meme premier sample CONTROL modifiable; STOP precede START dans le bucket.

PASS 3 ajoute six transformateurs a la meme chaine ordonnee: PROBABILITY
(CHANCE/CONDITION/LOT), GATE (LENGTH/VARIATION/MODE), GROOVE
(TYPE/TIMING/VELOCITY), ECHO (TIME/REPEATS/DECAY), HARMONIZER
(TYPE/SPREAD/INVERT) et CHORD (SHIFT/SPREAD/INVERT). `group_id` reste une
correlation: Probability prend une decision commune, Groove applique une phase
commune, Echo derive un groupe enfant par repetition, Harmonizer produit les
voix d'un meme groupe et Chord conserve le groupe polyphonique. Il ne porte
aucun lifecycle. Harmonizer est le transformateur mono vers poly; Chord deplace
diatoniquement et revoice un groupe polyphonique selon la gamme KBD. Le terminal
conserve l'unicite HELD `(track,destination,pitch)` et traite LEGATO comme une
reprise sans attaque, RETRIG comme OFF puis ON.

Echo n'a pas de queue locale. Ses repetitions, bornees a deux apres l'original,
entrent dans la future queue centrale triee avec un `occurrence_id` enfant, le
`source_token` conserve et `resume_slot=N+1`; elles ne retraversent jamais les
slots precedents. La meme regle couvre les fins datees de Gate. La tete de file
est consommee par date, puis OFF avant ON a date egale. Un cutover, STOP/PANIC,
mute, remplacement Pattern ou Project ferme les derives, purge ces entrees et
reset les deadlines ARP/EUCLID. Un unmute ne restaure aucun futur ancien.
TIME/REPEATS/DECAY sont donc captures lors de la materialisation de chaque
repetition Echo: une repetition deja en queue reste immuable, tandis que la
prochaine occurrence qui entre dans Echo utilise l'etat courant.

Un changement TYPE est le seul changement MIDI FX live structurel: les sorties
de la track sont fermees, les futurs de l'ancienne structure sont purges, les
runtimes du premier slot modifie jusqu'a S4 sont reconstruits et la generation
de chaine avance. Les slots temporels situes avant le cutover conservent ainsi
phase et deadline. Le
ledger de sources HELD survit a cette operation et ses sources sont reevaluees
immediatement depuis S1 dans la nouvelle chaine; TYPE A->B, TYPE->OFF et
OFF->TYPE n'attendent donc ni reloop ni nouveau NOTE_ON. Les resets explicites
(mute/cutover/Panic/restore Project) effacent au contraire ce ledger. BASE et
TEMP/p-lock convergent vers ce meme owner: application et restore TEMP suivent
les regles parametriques ou structurelles du parametre effectif, sans chemin
de teardown propre aux p-locks.

L'admission centrale est l'unique preuve de chaine. Avant installation de
l'etat, elle multiplie les `instant_fanout` et `temporal_fanout` des quatre
slots, verifie chaque stage contre les buffers A/B de 32 evenements, reserve le
futur global dans 512 entrees et reserve les actions de toutes les tracks contre
les 256 actions terminales par horizon. Le facteur ROLL maximal est inclus dans
les quatre actions source par voix. Le staging live de 128 actions impose un
fanout compose maximal de quatre; une chaine Harmonizer/Echo, dans les deux
ordres, est donc refusee proprement au lieu d'etre acceptee puis tronquee. Deux
Harmonizer, deux Echo et EUCLID/Echo sans reservation future disponible sont
egalement refuses. Un child GROUP mono n'admet pas de fanout superieur a un,
afin que son activation ulterieure ne puisse contourner la preuve globale. La
limite source devient `floor(8/fanout)` et vaut huit sans expansion, deux pour
Harmonizer ou Echo.

Les buffers A/B et la future queue restent bornes; le ledger source ajoute au
plus huit `note_event_t` par track, sans allocation dynamique. Les templates
Groove et harmonie restent en FLASH. L'insertion future est bornee a 512 deplacements;
la consommation ordonnee lit la tete. Le pipeline reste
`O(4 x evenements admis)`; les traitements de groupe et deduplications portent
au plus sur huit notes.

Undo/Redo conserve huit transactions structurelles. No-op n'est pas capture, une nouvelle branche purge Redo, Copy ne cree pas de transaction et Paste pre-valide le pool avant mutation atomique. Pattern/Project reussis invalident l'historique.

## Horodatage live

Hall, USB MIDI Device, USB MIDI Host et encodeurs capturent TIM5 a l'ingestion avec un `ingress_serial` monotone. CONTROL convertit directement cette capture et applique une garde fixe de 64 samples. La valeur effective n'est calculee qu'une fois.

Hall publie un evenement fixe de 16 octets dans une FIFO bornee; Device conserve 128 paquets et Host 64. Les files rejettent deterministement le plus recent a saturation et incrementent leurs diagnostics.

La file live CONTROL contient 31 occurrences fixes, triees par `(sample_time, ingress_serial)`. Une echeance future reste en attente; une echeance tardive est clampee au premier sample modifiable. SEQ, Hall, MIDI et Note FX convergent ensuite vers le meme contrat musical final. Aucun audio deja rendu n'est reecrit.

Panic CC120/123 publie PANIC dans la FIFO fonctionnelle unique. L'ordre FIFO place la fermeture avant les commandes suivantes sans generation, queue prioritaire ou purge laterale. Le Live Recording conserve le MICROTIMING brut et l'heure effectivement entendue; QUANT reste une projection non destructive du resolver PLAY.

## Commit d'horizon

La construction d'un horizon ne commence que si les 1827 places du burst
contractuel sont disponibles. Si AUDIO n'a pas encore libere cette capacite,
CONTROL quitte la passe sans modifier son curseur ni aucun ledger; l'admission
reprend lors d'une passe cooperative ulterieure. Les refus de forme, de plancher
ou d'etat restent fatals. Collecte, Note FX et sorties terminales restent
invisibles jusqu'au commit FIFO unique. Le curseur
`g_seq_runtime_control_sample_cursor` avance seulement apres ce commit; une
pression FIFO temporaire ne consomme donc aucun etat producteur ni aucune
fenetre musicale.
