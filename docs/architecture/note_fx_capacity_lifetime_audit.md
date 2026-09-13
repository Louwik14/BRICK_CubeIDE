# NoteFx: transitions, capacities et lifetimes

## Ancien modele et cause commune

L'ancien terminal utilisait directement `occurrence_id` comme `output_id`.
`control_music_output` simulait correctement chaque steal, mais rangeait ensuite
les actions dans des buckets STOP, START et RETRIGGER distincts. Une suite
`START A, STOP A, START B` devenait `STOP A, START A, START B` dans la FIFO. Sur
une piste mono, Harmony, Euclid, Gate ou un revoice pouvaient donc presenter un
second START a AUDIO alors que le premier etait encore HELD. Gate LEGATO pouvait
en outre remplacer l'id uniquement dans le ledger CONTROL. Ces deux divergences
expliquaient les fatals `AUDIO_PARAMETER_MAPPING_FAILED`.

L'ancienne reservation future additionnait des constantes par modele sans
definir ce qui arrivait aux projections Gate/Echo supersedees. Le test 9^4
recalculait ces constantes mais ne simulait ni les lifetimes, ni l'accumulation.

## Pipeline courant

```text
source semantic event
-> S1 -> S2 -> S3 -> S4
-> intent {semantic_event_id, source, generation, note, velocity, date}
-> control_music_output
-> transition ordonnee {output_handle, START|STOP|RETRIGGER, date}
-> FIFO CONTROL->AUDIO
-> mapping physique AUDIO
```

NoteFx possede les sources HELD, les vues HELD des generateurs/revoicers et la
queue future. Probability, Groove, Gate et Echo ne dupliquent aucun HELD. La
phase ARP/Euclid est derivee du temps musical canonique. NoteFx ne cree aucun
handle AUDIO.
`control_music_output` est l'unique owner des sorties actives, du stealing et
de l'allocation de handle. AUDIO applique les transitions et conserve seulement
le mapping physique et les tails RELEASE.

`semantic_event_id` est une identite musicale. Elle peut etre derivee de la
causalite d'un FX, mais la recherche CONTROL utilise aussi source et generation.
`output_handle` est un entier non nul alloue par CONTROL, unique parmi tous les
handles actifs et independant des hash/counters NoteFx. Seul ce handle traverse
l'ABI AUDIO. Les observers de mort recoivent l'identite semantique.

## Ordre et LEGATO

Une fenetre contient un bucket par sample. Chaque insertion recoit un numero
d'ordre commun aux producteurs internes et externes; le merge FIFO conserve ce
numero. A date egale, la regle est donc l'ordre exact des decisions CONTROL. Un
steal atomique reste `STOP victime, START entrant`; deux steals successifs
restent `STOP A, START B, STOP B, START C`.

RETRIGGER publie OFF puis ON adjacents avec le meme handle. LEGATO utilise cette
meme transition explicite: sa semantique actuelle est une reprise coherente,
pas un changement de ledger invisible. CONTROL et AUDIO gardent toujours le
meme handle actif.

## Futurs et admission

La queue globale conserve 320 evenements. Gate et Echo marquent explicitement
leurs projections temporelles. Pour une meme cle
`{track,slot,destination,note,kind,repeat}`, la projection la plus recente
supersede l'ancienne: le terminal ne peut de toute facon posseder qu'un lifetime
vivant pour ce pitch. Le debit ROLL augmente donc le nombre de remplacements,
pas le nombre de slots persistants.

La borne est derivee de cette representation runtime unique:

- Gate: un STOP futur par pitch a la frontiere;
- Echo: `pitches x 2 kinds x REPEATS`;
- ARP/Euclid: un OFF courant par pitch temporel;
- candidats immediats: `8 x 4 = 32` au maximum pour Harmonizer;
- admission: huit candidats ON apres chaque transformateur immediat et avant
  tout transformateur temporel aval;
- Echo: 8 lifetimes admis x 2 repeats x ON/OFF = 32 futures par piste;
- Gate/generateur: au plus 8 deadlines terminales additionnelles par piste;
- global: huit pistes polyphoniques equivalentes x 40 = 320 futures (la
  topologie GROUP remplace une piste huit voix par huit children mono).

Il n'existe plus de `max_future_pending`, de `max_delay_divisions` arbitraire ou
de calcul de fanout compose. Toute chaine valide par famille est acceptee, puis
les candidats sont tronques deterministement a huit avant de devenir des
lifetimes ou des futures.

## Groove, ordre et revoice

Groove derive sa phase de la grille musicale absolue au sixieme de step, grille
commune aux divisions binaires et ternaires. Aucun hash d'identite n'intervient.
`ARP -> Groove` varie donc timing et accent de pulse en pulse. `Groove -> ARP`
ne groove que l'ancre/velocity entree dans l'ARP; cette asymetrie est la
semantique normale de la chaine S1 -> S4.

Un futur produit par plusieurs slots reprend au stage suivant et porte un masque
de dependances ainsi que leurs quatre versions locales compactees sur quatre
bits chacune. Une mutation purge d'abord uniquement les futurs portant son bit,
puis incremente la version du slot; aucun compteur global de chaine ne subsiste.

Un TYPE change collecte la matiere HELD a la premiere frontiere modifiee, ferme
seulement les sorties de ses sources causales, retire ces seules sources des
vues aval, puis rejoue la matiere a cette frontiere. Un tweak CHORD/HARMONIZER
applique le meme revoice local. Les slots amont et leurs generateurs survivent.
Une deadline terminale encore pendante est projetee en duree restante avant le
replay: un Gate amont n'est ni perdu, ni redemarre a sa duree initiale.
Gate/Echo invalident leurs propres futurs et les vues aval qui portent leur
dependance, sans reset de track. Probability/Groove sont FORWARD_ONLY. Un reset
global reste reserve a panic, reset transport incompatible ou destruction de
route/entite.

L'etat UI/persistence est AUTHORITATIVE. La configuration `applied` est l'etat
runtime actif derive; l'override est une projection PREPARED superposee avant
validation/admission, jamais une seconde autorite. L'etat est installe seulement
apres reservation et enqueue de la configuration complete. Si la command ring
refuse, reservations et etat canonique restent anciens.

## Capacites et validation

| Ressource | Borne |
|---|---:|
| source HELD | 8/piste |
| HELD generateur/revoice | 8/slot concerne/piste |
| buffers A/B | 32 evenements |
| future | 320 global |
| command/live queue | 31 chacune |
| outputs logiques / mapping AUDIO | 8/entite |
| staging interne | 384 actions/horizon |
| staging externe | 128 actions/horizon |

Le staging interne 384 reste necessaire: 128 NOTE_ON source peuvent tomber
dans un horizon de 64 frames et demander chacun STOP+START, soit 256 actions;
les generateurs NoteFx peuvent aligner 64 sorties demandant aussi STOP+START,
soit 128 actions independantes. Le staging externe 128 couvre 64 transitions
externes avec remplacement. La conversion maximale est donc
`2 x (384 + 128) = 1024` commandes NOTE. Avec les autres producteurs, la preuve
FIFO reste 3548 commandes et la puissance de deux statique demeure 4096.

RAM statique PASS 4 (octets):

| Zone | Avant | Apres | Delta |
|---|---:|---:|---:|
| future SRAM2 | 20480 | 12800 | -7680 |
| work buffers | 2560 | 2560 | 0 |
| slot runtime / HELD | 13824 max | 13824 max | 0 |
| source HELD | 5120 | 5120 | 0 |
| terminal ledgers actif+prepare | 6144 | 6144 | 0 |
| staging interne/externe | 11792 | 11792 | 0 |
| config/version runtime | 912 | 848 | -64 |

Le linker confirme SRAM2 `119648 -> 111968` (-7680 octets) et RAM_D1
`481056 -> 480992` (-64 octets). Aucun candidat rejete ne devient persistent.
Le round final retire aussi `next_sample` et compacte les versions de slot;
RAM_D1 passe a 480608 octets (-384), SRAM2 reste a 111968 octets.

Les refus previsibles sont tous en amont de la mutation: schema/famille invalide,
command ring pleine, ou neuvieme source HELD. `batch full`, `HELD full`, future
pleine, refus terminal, `NOTE_FX_PIPELINE_PROCESS_FAILED` et
`BRICK_FATAL_MUSIC_STAGING_CAPACITY` ne sont plus atteignables par une operation
produit valide; ils restent des sentinelles de corruption, de violation du
preflight ou de bug interne. Les versions obsoletes sont compactees au changement
de leur owner et ne peuvent donc pas consommer artificiellement les 320 futures.

La regression host compile et execute le moteur C pour verifier les HELD locaux,
la liberation terminale et la composition des dependances. Elle enumere aussi les 10000 chaines candidates, filtre les familles
produit valides et simule des ledgers CONTROL/AUDIO
pour Harmony quatre voix sur mono, Euclid polyphonique, Gate RETRIG et LEGATO.
Elle exerce aussi 1000 occurrences ROLL sur huit pistes avec Gate et Echo,
verifie la compaction future, la phase Groove temporelle, le cutover/revoice local
et la transaction state/enqueue. Les builds M7 et firmware complet restent les
preuves compilees; les tests hardware demeurent requis pour la sonorite LEGATO,
les cutovers en charge et les xruns.

## Verdict

Avec les besoins produit actuels: **OUI MAIS PLUS SIMPLE**. Les quatre slots,
les buffers bornes et les etats compacts restent utiles. Les kind-buckets, les
handles derives des FX, le remap LEGATO invisible, les constantes futures
historiques et le chemin `state puis enqueue` ont ete supprimes.
