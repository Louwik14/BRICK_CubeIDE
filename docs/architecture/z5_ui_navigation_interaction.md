# Z5 - Navigation et interaction UI

STEP 1..8 selectionnent les top-level. En GROUP, STEP 9..16 selectionnent les children 8..15. La disponibilite des ensembles vient du masque `track_runtime`: le master GROUP expose CFG, ENV, MOD, MIX et FX audio, mais ni SEQ, MIDI FX, TONE ni PLAY puisqu'il ne possede ni lane ni moteur de notes; son ENV edite le filtre post-somme (Cutoff, Resonance, Morph) et l'ENV3 commun. Un child expose la voix RAM mono, sa sequence/PLAY, ses MIDI FX, son ENV/TONE/MIX/FX et la vue filtree de la MOD partagee. MOD derive toujours son owner par `entity_topology`.

`SHIFT + STEP 16` ouvre le Master global sans changer la selection. Master expose reverb, delay et compresseur et ne possede ni sequence, mute, clipboard Track, Undo ni slot persistant.

`ui_set_hall_mode` est l'autorite de transition. MUTE et PATTERN possedent leurs sous-etats; SEQ est un gate de handler. Sortir force MUTE/PATTERN nettoie leur etat via ce point central.

Le chemin Hall direct met a jour modifiers, selection, mode et double-tap avant le drain de la queue UI. SHIFT+HALL precede TRACK_MOD+HALL. Un evenement consomme par un stage masque les suivants.

La page PATCH porte la grammaire locale canonique PAGE 1 SAVE, PAGE 2 LOAD,
PAGE 3 RENAME, PAGE 4 CLEAR. LOAD soumet en une fois le slot visible et le masque
de targets; CLEAR demande une confirmation locale puis remet uniquement le Patch
live de la track courante a son etat Init. CLEAR ne supprime jamais le fichier du
slot et ne change pas la selection du browser. Le double-tap Hall n'est plus une
commande Save: il ouvre le contexte PATCH, dont le Save est porte par PAGE 1.
L'etat vide d'une liste filtree est rendu dans la zone de contenu sous la forme
`NO PATCH`; il ne remplace jamais le footer PAGE 1..4, qui reste la navigation
canonique de PATCH. Les messages transitoires sont rendus au-dessus du footer.
Ils ont une echeance propre; seuls une operation en cours et la confirmation
`CLEAR?` constituent un etat persistant. Le Name/Edit commun charge le nom pour
un Rename, ouvre un Save de nouveau Patch sur un buffer vide et place le curseur
sur la terminaison, apres le dernier caractere d'un nom charge ou genere.
Les browsers dynamiques de SETTINGS appliquent le meme contrat de separation
entre statut et footer.

Le contexte temporaire TRACK est resolu par `ui_hall_mode_track_overlay_active`,
utilise par le dispatch Hall et la projection LED. Il prime sur la page active
pour la selection des tracks, puis disparait au relachement de TRACK; la page
reprend alors sa projection propre. Les overlays MUTE, MACRO et SHIFT restent
prioritaires selon leur contrat.

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
Les cartes MIDI FX cataloguées suivent le rendu Param commun: la valeur et le
bit d'inversion proviennent ensemble du p-lock du step tenu, puis le formatter
MIDI FX ne fait que nommer et mettre en forme cette valeur effective.

Les clipboards transportent uniquement des etats logiques. Un collage MIDI FX applique MODEL avant ses parametres; un collage External conserve l'entree demandee et echoue sur conflit.

La selection MODEL d'une chaine MIDI FX est une vue filtree du catalogue canonique explicite: OFF reste toujours present, le modele courant reste valide pour son slot et les modeles deja occupes par les autres slots sont seuls retires. Les trous et ordinaux d'enum ne definissent jamais ce catalogue. Les positions de cette vue ne sont jamais utilisees comme valeurs MODEL; chaque detent est remappe vers l'enum canonique avant le commit CONTROL.

Project Save est modal et reutilise le Name Editor generique. L'entree SAVE AS
ou SAVE TO est refusee tant que le transport est RUNNING ou START_PENDING; elle
ne demande jamais de STOP. Apres confirmation du nom, les inputs Settings restent
bloques jusqu'au resultat terminal du backend asynchrone.
