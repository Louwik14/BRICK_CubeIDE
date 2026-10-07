# Z5 - Navigation et interaction UI

STEP 1..8 selectionnent les top-level. En GROUP, STEP 9..16 selectionnent les children 8..15. La disponibilite des ensembles vient du masque `track_runtime`: le master GROUP expose CFG, ENV, MOD, MIX et FX audio, mais ni SEQ, MIDI FX, TONE ni PLAY puisqu'il ne possede ni lane ni moteur de notes; son ENV edite le filtre post-somme (Cutoff, Resonance, Morph) et l'ENV3 commun. Un child expose la voix RAM mono, sa sequence/PLAY, ses MIDI FX, son ENV/TONE/MIX/FX et la vue filtree de la MOD partagee. MOD derive toujours son owner par `entity_topology`.

`SHIFT + STEP 16` ouvre le Master global sans changer la selection. Master expose reverb, delay et compresseur et ne possede ni sequence, mute, clipboard Track, Undo ni slot persistant.

`ui_set_hall_mode` est l'autorite de transition. MUTE et PATTERN possedent leurs sous-etats; SEQ est un gate de handler. Sortir force MUTE/PATTERN nettoie leur etat via ce point central.

Le chemin Hall direct met a jour modifiers, selection, mode et double-tap avant le drain de la queue UI. SHIFT+HALL precede TRACK_MOD+HALL. Un evenement consomme par un stage masque les suivants.

La page PATCH porte la grammaire locale canonique PAGE 1 RETURN, PAGE 2 LOAD,
PAGE 3 SAVE, PAGE 4 PREVIEW; SHIFT remplace la ligne par `- / RENAME / DELETE / -`.
Le premier element est toujours le slot virtuel `[ + NEW PATCH ]`: LOAD y appelle
l'initialisation produit et SAVE le Name/Edit de creation. Sur un fichier, LOAD
charge et SAVE ecrase; DELETE supprime bien le fichier apres confirmation.
Le catalogue utilise `ALL / FOCUS / SYN / SMP / DRM`, avec FOCUS par defaut et le
slot virtuel toujours visible. PREVIEW est momentane et utilise le moteur Preview
produit. Les messages transitoires restent rendus au-dessus du footer. Le
Name/Edit commun charge le nom pour Rename et place le curseur apres le dernier
caractere du nom charge ou genere.
Les browsers dynamiques de SETTINGS appliquent le meme contrat de separation
entre statut et footer.

Le contexte temporaire TRACK est resolu par `ui_hall_mode_track_overlay_active`,
utilise par le dispatch Hall et la projection LED. Il prime sur la page active
pour la selection des tracks, puis disparait au relachement de TRACK; la page
reprend alors sa projection propre. Sa duree de vie suit exclusivement l'etat
physique de TRACK: un changement de page, d'ensemble, de sous-page, de mode Hall
ou de track ne l'annule pas. MUTE et SHIFT restent prioritaires selon leur
contrat. MACRO est un overlay temporaire des modes Hall KBD et SEQ : son
entree memorise le mode Hall courant et toute sortie restaure exactement ce
mode. SHIFT + STEP 8 y entre. En LIVE, un tap alterne PRESSURE/TOGGLE apres la
fenetre de double tap et deux taps ouvrent ASSIGN sans appliquer la bascule du
premier tap. Depuis ASSIGN, un tap revient au dernier mode LIVE. Les 14 touches
blanches du clavier Hall pilotent les macros;
les dix noires passent par les raccourcis clavier SEQ existants. Les pads STEP
conservent leur handler SEQ, sans projection LED STEP/TRIG en MACRO. Chaque
scene LED reconstruit ses couches depuis zero, ce qui efface la projection du
mode precedent. Une navigation de page ou d'ensemble ferme MACRO et restaure
KBD ou SEQ independamment de la destination UI. Les gestes temporaires MACRO,
les flashes de valeur et la vue OLED sont nettoyes a cette transition. En
ASSIGN, maintenir une blanche puis tourner un encodeur cree ou met a jour la
valeur cible absolue du parametre selon les memes bornes, politiques de valeur
et owners que l'edition normale. SHIFT + encodeur efface cette cible. Le
relachement de la blanche ferme la capture sans modifier les autres locks.

Audio REC est une page modale Low-Cost, mais les boutons d'ensemble restent
navigables lorsqu'une destination est disponible. La navigation standard ferme
alors la page REC, restaure le mode/page de retour et ouvre l'ensemble demande;
le changement n'est pas traite comme une capture de bouton par REC. REC CFG
reste une page dediee au reglage de l'enregistrement.

Ordre contractuel du tick:

```text
track selection -> mute -> track hall gate -> transport -> settings
-> global shortcuts -> pattern -> sequence -> navigation -> page active
```

Les deltas encodeur utilisent un snapshot du contexte pris au debut du tick. Les modifications structurelles et restores appellent directement les owners Track; la mise a jour de contexte UI reste explicite via `ui_active_track_sync` et `ui_edit_context_sync`.

Les mutations structurelles de sequence (clear, paste et restore Track) passent
par `seq_runtime_on_track_pattern_change`: ce point invalide le scheduler si le
transport tourne et ferme toujours la capture NOTE/Undo et le geste STEP
(pending/held, cible et flash de longueur) qui projetaient l'ancien pattern.
Une sequence vide conserve son owner Track et redevient donc editable des le
premier appui; une selection de track ne sert pas d'invalidation implicite.

Le rendu p-lock possede deux adresses canoniques: les `param_id_t` utilisent le
feedback de `ui_param`, et les slots virtuels publient avec leur valeur un flag
`inverted`. PLAY derive ce flag de la presence effective du champ Voice/Step;
le renderer applique ensuite la meme convention de label inverse que pour les
parametres catalogues. Cette projection est strictement en lecture: elle ne
promeut jamais un appui STEP `pending` en geste `held`; seule l'interaction
encodeur peut effectuer cette promotion avant de creer ou modifier le champ
Voice/Step. Les pages virtuelles non p-lockables publient zero.
Les cartes MIDI FX cataloguees suivent le rendu Param commun: la valeur et le
bit d'inversion proviennent ensemble du p-lock du step tenu, puis le formatter
MIDI FX ne fait que nommer et mettre en forme cette valeur effective.

La troisieme sous-page TONE d'une track STREAM est `RANGE`: P1 adresse
`PARAM_SAMPLER_START`, P2 adresse `PARAM_SAMPLER_LENGTH` affiche comme `End`,
P3 et P4 restent libres. Ces deux controles utilisent le catalogue Param commun
(edition, p-lock et Matrix); ils ne constituent pas des controles UI speciaux.

La page MIDI FX expose directement quatre sous-pages fixes: GENERATOR, VOICER,
SCALER et TRIG. Chaque sous-page adresse quatre parametres connus; P4 porte le
bypass et le mode principal. GENERATOR adapte les labels P1/P2/P3 au mode ARP,
HOLD ou EUCLID sans effacer leurs valeurs. Son P4 n'est pas p-lockable. Il
n'existe plus de selection de type par slot ni de page ORDER.

MIDI FX et FX audio forment les deux pages du meme ensemble FX. Une nouvelle
activation de la commande FX alterne entre elles lorsque les deux sont
disponibles; chaque page conserve independamment sa sous-page locale. Le Hall
mode reste orthogonal a la page d'ensemble: le header derive toujours `SEQ`,
`KBD`, etc. du mode Hall effectif et n'affiche jamais `FX` du seul fait que la
page FX est ouverte.

Les clipboards transportent uniquement des etats logiques. Le bloc MIDI FX
copie les seize octets de la chaine fixe sans tri MODEL ni conversion de mode;
un collage External conserve l'entree demandee et echoue sur conflit.

Project Save est modal et reutilise le Name Editor generique. L'entree SAVE AS
ou SAVE TO est refusee tant que le transport est RUNNING ou START_PENDING; elle
ne demande jamais de STOP. Apres confirmation du nom, les inputs Settings restent
bloques jusqu'au resultat terminal du backend asynchrone.
