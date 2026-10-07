# Audit de demande SD par modulation START STREAM

Date : 2026-10-07. Portee : etat avant implementation, sans changement de
comportement. Le contrat implemente ensuite est decrit dans
[`stream_start_end_live.md`](stream_start_end_live.md).

## Verdict

Le pire cas reel provoque aujourd'hui par la modulation live de `START` sur une
voix STREAM deja en lecture est **zero changement de page et zero lecture SD**.
`brick6_sampler_runtime_set_start()` met seulement a jour la valeur de controle
et ne reconcilie en direct que les voix RAM. Le reader STREAM n'est ni seeke ni
rebinde. `LENGTH` a la meme propriete. Une nouvelle valeur START STREAM ne prend
effet qu'au prochain lancement/retrigger de la voix Clip.

Ce resultat invalide le scenario « une page 64 KiB abandonnee a chaque tick LFO »
pour le firmware actuel. Il revele en revanche une limite fonctionnelle : un
retrigger vers une page non READY ne cree pas une lecture differee; le bind
publie le besoin puis exige immediatement la page, echoue si elle manque, efface
le lease et le lancement n'a pas lieu.

## Chemin reel

- `PARAM_SAMPLER_START` aboutit a `brick6_sampler_runtime_set_start()` via le
  backend parametre ou la destination Matrix.
- Le setter ecrit `g_sampler_voice[track].start`; la reconciliation appelee est
  exclusivement `brick6_sampler_runtime_reconcile_ram_voice_bounds_live()`.
- Pour STREAM/Clip, `brick6_sampler_runtime_clip_start_playback()` relit START,
  calcule `region_begin`, construit un nouveau play-plan, reset le reader, puis
  le bind au nouveau `start_frame`.
- Le bind publie `CURRENT` et `NEXT`, puis exige `CURRENT` READY. Un miss sur
  CURRENT annule le bind. Si CURRENT est READY mais NEXT ne l'est pas, le
  Streamer peut charger NEXT en arriere-plan.
- Multi construit toujours son plan avec `start_frame = 0`; START ne pilote donc
  pas les voix Multi STREAM actuelles.

`LENGTH` ne seeke pas davantage une voix STREAM active. Il ne change que la fin
de region au prochain lancement. Le pitch, lui, avance naturellement le cursor
et publie une nouvelle fenetre lorsque la position atteint une autre page.

## Trois cadences distinctes

| Source | Evaluation | Publication/valeur moteur | Effet AUDIO minimal | Changement de page physique par START |
|---|---:|---:|---:|---:|
| LFO -> Matrix -> START | 64 frames = 1,333 ms = 750 Hz | une application par bloc AUDIO; valeur interne START peut differer chaque bloc | 64 frames | **jamais pendant la lecture (0 Hz)** |
| Matrix, autres sources bloc | 64 frames = 1,333 ms = 750 Hz | une fois par bloc | 64 frames | **0 Hz sans retrigger** |
| P-lock START | date sample-exacte dans le bloc SEQ/AUDIO | au sample `due_sample` | 1 sample possible entre evenements publies | seulement au note-on/retrigger associe; cible froide = lancement refuse |
| Retrig/ROLL + START deja applique | sample-exact | au retrigger | ROLL produit : minimum 600 samples a 300 BPM = 12,5 ms = 80 Hz par lane | rebind; transaction seulement si CURRENT est deja READY et une page necessaire (typiquement NEXT) est froide |
| Automation sequencee | aucun chemin d'automation continu distinct trouve; elle passe par les commandes parametres/p-locks | sample-exact pour commande datee | 1 sample en ABI | meme regle : aucune action sur voix active |
| Manuel/live | cadence de production CONTROL, puis commande AUDIO datee | sample-exact a l'execution | l'ABI peut distinguer des samples; ingress soutenu borne a 64/1024 = 3000 evenements/s global | **0 Hz sans retrigger** |
| Pitch | applique au plan de lecture | progression par sample | 1 frame AUDIO | changement naturel de page, independant de START |
| Multi | LFO/Matrix peut calculer START, mais le plan Multi force START=0 | sans effet positionnel | sans effet | **0 Hz par START** |

La frequence LFO libre maximale est 80 Hz. Le mode synchronise atteint toutefois
**160 Hz a 300 BPM** avec la division maximale 1/128 de mesure
(`0,0078125` mesure/cycle). Les formes disponibles incluent sine,
triangle, saw, reverse saw, square et random sample-and-hold, avec variantes
positives. Leur phase est planifiee dans le bloc, mais START n'est pas une
destination rampable sample par sample : la valeur terminale appliquee reste au
grain bloc de 64 frames (750 publications/s maximum; une square 160 Hz porte au
plus 320 fronts/s avant cette quantification). Une square/random a grande amplitude peut donc faire
alterner la valeur visible entre pages eloignees, mais ne force aucune page tant
qu'il n'y a pas de retrigger. Il n'existe donc pas, dans le code actuel, de
« nombre de frontieres de pages/s » LFO START non nul.

## Geometrie et gaspillage theorique 64 KiB

Une page stereo FLOAT32 contient 8192 frames. Les chiffres suivants sont le
cout **si** chaque changement vise une page froide differente et est admis; ce
n'est pas le comportement live actuel.

| Intervalle | Frames AUDIO utilisees a x1 | Utile/page x1 | Pages/s | MB/s decimal | % de 20,221615 MB/s |
|---:|---:|---:|---:|---:|---:|
| 1,333 ms (64 frames) | 64 | 0,781 % | 750 | 49,152 | 243,1 % |
| 5 ms | 240 | 2,930 % | 200 | 13,107 | 64,8 % |
| 10 ms | 480 | 5,859 % | 100 | 6,554 | 32,4 % |
| 12,5 ms (ROLL max) | 600 | 7,324 % | 80 | 5,243 | 25,9 % |
| 20 ms | 960 | 11,719 % | 50 | 3,277 | 16,2 % |
| 40 ms | 1920 | 23,438 % | 25 | 1,638 | 8,1 % |
| lecture continue x1 | 8192 | 100 % | 5,859 | 0,384 | 1,9 % |
| lecture continue x8 | 8192 | 100 % | 46,875 | 3,072 | 15,2 % |

Au cas bloc theorique, 8128 frames sur 8192, soit **99,219 % = 65 024 octets**,
peuvent rester inutilises. A x8, un bloc consomme 512 frames source : 6,25 % de
la page, donc 61 440 octets potentiellement inutilises. Le cout d'un saut froid
s'ajoute aux frontieres naturelles dues au pitch; une borne sure est donc
`pages_naturelles/s + sauts_froids/s`, pas seulement le maximum des deux.

## Cache : hit, changement de page et transaction

Le cache possede 376 pages : 340 pages de pool global et 36 pages de fenetres
reservees (8 readers musicaux + Recorder, quatre roles chacun; un slot Preview
existe dans les identifiants mais pas dans cette reserve de 36). Les demandes
runtime musicales allouent dans la fenetre de 36 pages. Une page READY non
protegee reste reutilisable jusqu'a eviction LRU; les leases protegent les roles
CURRENT, NEXT, LOOP_START et LOOP_START_NEXT.

Consequences :

- modulation START sans retrigger : ni lookup, ni miss, ni transaction;
- retrigger vers la meme page CURRENT : cache hit, aucune lecture CURRENT;
- alternance periodique entre un petit ensemble tenant dans les 36 pages : apres
  warm-up, rereads potentiellement nuls (les NEXT associees doivent aussi tenir);
- parcours de plus de 36 pages froides/non protegees : eviction puis rereads
  atteignables;
- cible CURRENT froide : le produit actuel ne soutient pas la demande, il refuse
  le lancement; elle ne devient donc pas une rafale SD continue;
- cible CURRENT chaude + NEXT froide : une transaction NEXT peut etre emise. Le
  scheduler n'autorise qu'une I/O en vol; le plafond mesure reste environ
  308 pages/s, mais bien avant cela les deadlines musicales deviennent le risque.

## P-lock, retrig et predictibilite

Les p-locks et ROLL sont connus du sequenceur avant leur `due_sample`; ils sont
donc predictibles en principe. Le moteur courant publie les commandes datees et
les execute sample-exact, mais ne transforme pas ce lookahead musical en
reservation de page START. Le seul horizon AUDIO ferme est le bloc de 64 frames
(1,333 ms), inferieur au P99.9 mesure de 3,3 ms : ce n'est pas un prechargement
suffisant a lui seul. Le ROLL produit le plus rapide est 80 Hz par lane a 300 BPM
(600 samples, 12,5 ms), donc temporellement prechargeable si une future passe
expose les occurrences a STORAGE. Les changements live, LFO et sources Matrix
dependant du jeu ne sont pas connus a l'avance de la meme facon.

## Worst-case produit actuel

Il faut separer demande semantique et I/O effectivement admise :

| Cas actuel | Nouvelles pages START/s | Debit START | Autre debit lecture naturel |
|---|---:|---:|---:|
| 1 reader, LFO/manuel/Matrix sans retrig | 0 | 0 MB/s | x1 0,384; x8 3,072 MB/s |
| 1 reader, ROLL 80 Hz vers pages froides | lancement froid refuse; pas 80 lectures/s soutenues | 0 soutenu par ce chemin | pages naturelles de lancements reussis |
| 1 reader, ROLL 80 Hz, CURRENT chaud et NEXT froid | jusqu'a 80 demandes NEXT/s si un working-set prechauffe le permet | 5,243 MB/s | inclus/reduit selon recouvrement |
| 2/4/8 readers dans ce montage chaud/froid | 160/320/640 demandes/s theoriques | 10,486/20,972/41,943 MB/s | scheduler physique plafonne a ~308 pages/s |

Le montage 8 x 80 Hz n'est atteignable durablement comme lecture correcte de
pages toutes froides : il faut garder les CURRENT chaudes, les 36 slots limitent
le working-set, et le serveur SD sature a ~308 pages/s. Il est toutefois
atteignable comme **demande en attente/underrun/echec**, pas comme debit utile.
Le worst-case physique observe ne peut depasser le serveur mesure, soit environ
20,22 MB/s; a cette saturation il ne reste aucune marge et les deadlines ne sont
plus garanties.

Le produit admet huit readers musicaux partages entre Classic STREAM et Multi,
plus un reader overdub. Le Recorder ecrit 48 000 x 8 = **0,384 MB/s**. Son reader
overdub x1 ajoute environ **0,384 MB/s**, soit **0,768 MB/s** hors surcouts et
metadonnees. Huit lectures continues x1 + overdub lecture/ecriture valent environ
3,84 MB/s (19,0 %). Huit lectures continues x8 valent deja 24,576 MB/s avant
Recorder : ce cas depasse 121,5 % du benchmark et aucune limitation START ne peut
le rendre sur par elle-meme.

## Faut-il plafonner les sauts START STREAM ?

**Pas pour corriger le firmware actuel : il n'existe pas de seek START live a
plafonner.** Ajouter maintenant un limiteur ne reduirait aucune transaction SD.

Si une future passe rend START live et charge les cibles froides, un plafond
simple peut etre utile, mais il doit etre global et sensible au pitch/Recorder.
Avec une marge de 25 % sur 20,221615 MB/s et 0,768 MB/s reserves au Recorder :

| Readers x1 | Budget sauts total | Plafond par reader | Intervalle minimal |
|---:|---:|---:|---:|
| 1 | 14,014 MB/s apres lecture x1 | 213,8 Hz | 4,68 ms |
| 4 | 12,862 MB/s apres lectures x1 | 49,1 Hz | 20,38 ms |
| 8 | 11,326 MB/s apres lectures x1 | 21,6 Hz | 46,29 ms |

Ces valeurs utilisent la borne additive conservative et supposent chaque saut
froid. Une politique produit ronde de 20 Hz par reader serait soutenable a x1
pour huit readers (~14,33 MB/s avec lecture naturelle et Recorder, 70,9 % du
debit mesure). 50/100/250/500/750 Hz par reader ne sont pas des plafonds produit
8-voix soutenables sur pages froides. Musicalement, 750 Hz est le grain actuel
de valeur bloc; 1000 Hz n'a donc aucun sens sans augmenter la cadence moteur.
20-50 Hz est rapide pour des scans/gestes, mais les formes a saut perdraient des
transitions au-dessus du plafond.

Il n'existe toutefois **aucun X fixe universel** garantissant huit readers sur
toute la plage pitch : a x8, le debit naturel seul depasse deja le plafond SD.
La solution simple viable pour une future implementation est un budget global
de transactions/s, avec soustraction du debit naturel previsible et de la charge
Recorder, plus cache hits gratuits. Un simple limiteur local identique par voix
ne suffit pas au contrat complet.

## Taille de page et benchmarks suivants

64 KiB rend le gaspillage theorique tres eleve des qu'un vrai seek froid devient
plus rapide que quelques dizaines de millisecondes. Ce gaspillage n'est pas
declenche par la modulation live actuelle, donc changer la taille maintenant
n'est pas justifie par ce bug suppose. Avant d'implementer START live, mesurer
32/16/8 KiB en random avec average, P99, P99.9, max et request-to-ready est utile :
cela donnera le cout fixe par transaction, le debit utile et la taille minimale
compatible avec les deadlines. Aucune extrapolation lineaire depuis 64 KiB n'est
valide.

## Reponses directes

1. START STREAM change physiquement de page pendant une voix active a **0 Hz**.
2. Pire demande due a la modulation seule : **0 nouvelle page/s/reader**.
3. Debit SD correspondant : **0 MB/s/reader**.
4. Huit readers + Recorder : **0 MB/s de surcout START live**; charge de base x1
   approximative **3,84 MB/s**, soit **19,0 %**.
5. Le serveur mesure plafonne a ~308 pages/s; les montages retrigger chaud/froid
   peuvent le saturer, mais les cibles CURRENT froides echouent avant lecture.
6. Gaspillage potentiel futur maximal : **65 024 octets (99,219 %) a x1 par
   saut au bloc**, mais non realise par START live aujourd'hui.
7. Les cycles periodiques tenant dans 36 pages runtime peuvent devenir 100 % hits
   apres warm-up; au-dela, LRU reread.
8. P-lock/ROLL sont predictibles; live et LFO/Matrix ne le sont pas en general.
9. Aucun plafonnement START n'est requis pour le comportement actuel.
10. Pour un futur START live, **20 Hz/reader a x1 avec huit readers** est justifie
    avec >25 % de marge; ce n'est pas une garantie tous pitches.
11. Une garantie globale requiert un budget partage tenant compte du pitch et du
    Recorder; x8 x huit est deja hors budget sans aucun saut.
12. Benchmarks 32/16/8 KiB necessaires avant toute decision de page, pas avant.
