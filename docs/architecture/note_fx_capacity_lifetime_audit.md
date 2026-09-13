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

NoteFx possede les sources HELD, les frontieres HELD de slots, les phases
ARP/Euclid et la queue future. Il ne cree aucun handle AUDIO.
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

La queue globale conserve 512 evenements. Gate et Echo marquent explicitement
leurs projections temporelles. Pour une meme cle
`{track,slot,destination,note,kind,repeat}`, la projection la plus recente
supersede l'ancienne: le terminal ne peut de toute facon posseder qu'un lifetime
vivant pour ce pitch. Le debit ROLL augmente donc le nombre de remplacements,
pas le nombre de slots persistants.

La reservation est derivee de cette representation runtime unique:

- Gate: un STOP futur par pitch a la frontiere;
- Echo: `pitches x 2 kinds x REPEATS`;
- ARP/Euclid: un OFF courant par pitch temporel;
- fanout amont: applique au nombre de pitches de la frontiere;
- scratch: produit instantane/temporel maximal inferieur ou egal a 32;
- terminal: fanout compose inferieur ou egal a quatre;
- global: somme des reservations de toutes les pistes inferieure ou egale a
  512, avec 256 actions source et 128 actions temporelles par horizon.

Il n'existe plus de `max_future_pending` ou `max_delay_divisions` arbitraire par
modele. Admission, remplacement runtime et regression host emploient le meme
nombre de cles temporelles.

## Groove, ordre et revoice

Groove derive sa phase de la grille musicale absolue au sixieme de step, grille
commune aux divisions binaires et ternaires. Aucun hash d'identite n'intervient.
`ARP -> Groove` varie donc timing et accent de pulse en pulse. `Groove -> ARP`
ne groove que l'ancre/velocity entree dans l'ARP; cette asymetrie est la
semantique normale de la chaine S1 -> S4.

Un futur produit par le slot N reprend a N+1 et porte `(owner_slot,
owner_version)`. Un TYPE change collecte la matiere HELD a la premiere
frontiere modifiee, ferme seulement les sorties de ses sources causales, purge
leurs futurs downstream, reset cette frontiere et son downstream, puis rejoue
la matiere.
Un tweak CHORD/HARMONIZER collecte ses sources, ferme les sorties causales,
purge seulement leurs futurs downstream, reset le slot de revoice et ses
dependances, puis rejoue. Les slots amont et leurs phases independantes
survivent.

L'etat UI/persistence est AUTHORITATIVE. La configuration `applied` est l'etat
runtime actif derive; l'override est une projection PREPARED superposee avant
validation/admission, jamais une seconde autorite. L'etat est installe seulement
apres reservation et enqueue de la configuration complete. Si la command ring
refuse, reservations et etat canonique restent anciens.

## Capacites et validation

| Ressource | Borne |
|---|---:|
| source HELD | 8/piste |
| slot HELD | 8/slot/piste |
| buffers A/B | 32 evenements |
| future | 512 global |
| command/live queue | 31 chacune |
| outputs logiques / mapping AUDIO | 8/entite |
| staging interne | 384 actions/horizon |
| staging externe | 128 actions/horizon |

La regression host enumere les 6561 chaines et simule des ledgers CONTROL/AUDIO
pour Harmony quatre voix sur mono, Euclid polyphonique, Gate RETRIG et LEGATO.
Elle exerce aussi 1000 occurrences ROLL sur huit pistes avec Gate et Echo,
verifie la compaction future, la phase Groove temporelle, le reset/purge revoice
et la transaction state/enqueue. Les builds M7 et firmware complet restent les
preuves compilees; les tests hardware demeurent requis pour la sonorite LEGATO,
les cutovers en charge et les xruns.

## Verdict

Avec les besoins produit actuels: **OUI MAIS PLUS SIMPLE**. Les quatre slots,
les buffers bornes et les etats compacts restent utiles. Les kind-buckets, les
handles derives des FX, le remap LEGATO invisible, les constantes futures
historiques et le chemin `state puis enqueue` ont ete supprimes.
