# Streaming sample produit

## Contrat

Classic STREAM, Multi et REC_SOURCE utilisent un seul Page Cache. Un slot fait
exactement 16 KiB. En FLOAT32 stereo entrelace, cela represente 2048 frames.
Les 376 slots produit sont conserves: 340 slots generaux et 36 slots reserves
aux neuf readers garantis et a leurs quatre roles `CURRENT`, `NEXT`,
`LOOP_START`, `LOOP_START_NEXT`. Le payload occupe donc 6 160 384 octets en
SDRAM. Le passage depuis 64 KiB libere 18 481 152 octets de SDRAM sans changer
le nombre de slots, les identifiants, les leases, les generations, l'eviction
ou les refcounts/protections existants. Ce choix libere la SDRAM au lieu de
porter le cache a 1504 slots: le pool general partage par Classic STREAM, Multi et
Wavetable passe mecaniquement de 22 282 240 a 5 570 560 octets. Augmenter le
nombre de slots aurait ajoute environ 154 KiB de metadata chaude et depasse le
budget D1 actuel; aucun placement agressif du prototype n'est repris.

Une page suit toujours `FREE -> RESERVED -> LOADING -> READY`, avec les chemins
`EVICTING` et `FAILED` existants. `LOADING` porte en plus un watermark monotone
`available_frame_end`. Il vaut zero au debut, progresse de 64 frames par chunk
stereo de 512 octets, puis vaut 2048. Une completion n'est publiee que si la
cle, le slot, la generation et l'epoch d'enregistrement correspondent encore.

## Lecture SD progressive

Trois granularites independantes sont contractuelles:

```text
page logique du Page Cache = 16 KiB
publication progressive     = 512 octets
I/O physique sequentielle   = 16, 32, 48 ou 64 KiB
```

Le fastpath ne cree aucune page de 64 KiB. Il reserve jusqu'a quatre slots
16 KiB distincts, avec leurs propres cle, slot, generation, etat et watermark,
puis effectue au maximum un seul `CMD18` physique pour les remplir. La constante
`SAMPLE_STREAM_FASTPATH_MAX_PAGES` borne cette premiere version a quatre pages.
Il n'existe qu'un Stream Manager, un scheduler, un backend, un Page Cache et un
VoiceReader.

Une page canonique complete, alignee sur un secteur et deja au format FLOAT32
stereo utilise une seule transaction physique:

```text
CMD18, DLEN=16384
IDMABSIZE=512, IDMABNDT=16
BASE0=page+0, BASE1=page+512
32 x IDMABTC/publication
DATAEND
CMD12 unique
LOADING -> READY
```

L'IDMA fonctionne en double buffer. A chaque `IDMABTC`, le handler identifie le
buffer termine avec `IDMABACT`, rearme ce buffer vers le chunk suivant tant
qu'il en reste un, puis publie le chunk termine. Le H743 signale aussi le
32e/dernier chunk par `IDMABTC`: ce chunk n'est pas rearme et `IDMABTCIE` est
masque, tandis que DPSM et IDMA terminent naturellement `DLEN`. `DATAEND`
declenche ensuite l'unique `CMD12`; la reponse valide la completion normale.

Le descriptor prepare par le block device transporte explicitement le flag
progressif. `chain_next` le restitue au lancement suivant: une lecture
canonique chainee conserve donc les memes 32 publications et ne retombe pas
sur le chemin mono-buffer.

Les pages partielles et les formats non canoniques restent sur le chemin de
lecture asynchrone complet existant. Les writes Recorder/Looper/Overdub ne sont
pas fragmentes en 512 octets: ils conservent ring, coalescing et gros DMA. Le
scheduler et l'arbitrage READ/WRITE communs ne changent pas.

## D-cache et publication AUDIO

Avant le DMA, le block device conserve la preparation de destination existante.
Apres chaque buffer termine, il invalide uniquement ses 512 octets, execute un
`DMB`, puis augmente `available_frame_end`. Une zone deja publiee n'est jamais
invalidee de nouveau. Le chunk, la page et le pool sont alignes sur les lignes
D-cache de 32 octets.

AUDIO ne bloque jamais et ne fait aucun I/O. Apres publication de son lease, le
VoiceReader accepte un descriptor `READY`, ou `LOADING` si son snapshot du
watermark couvre la frame demandee. Le span rendu est borne a ce snapshot; a la
frontiere publiee le reader relache son cursor et retente au bloc suivant. Une
cible START situee plus loin dans la page ne devient applicable que lorsque sa
frame est dans le prefixe publie. Un miss conserve le comportement d'underrun
existant. La validation de generation, epoch, cle et slot reste identique pour
les pages partielles et completes.

START/END live reste plafonne a 12 Hz et conserve sa politique latest-wins. Un
cache hit ne lance aucune transaction SD. Une cible froide reserve une page de
16 KiB et peut etre appliquee des que sa frame appartient au prefixe publie,
avant `READY`. Multi et les transitions de loop reutilisent le meme transport;
aucune plage au-dela du watermark n'est exposee au rendu.

Le sampler produit reste forward-only. Reverse et ping-pong sont temporairement
retires: ils ne reviendront qu'avec un contrat natif VoiceReader/Page Cache.

## IRQ et charge CPU

SDMMC1 est a la priorite NVIC 1 et les DMA AUDIO Stream3/Stream4 a la priorite
2. Le prototype materiel de reference a mesure environ 131 cycles moyens et
316 cycles maximum pour le rearm, 1214/2467 cycles pour le handler complet, et
un intervalle minimum de 9734 cycles entre `IDMABTC`. Le compteur CPU AUDIO
soustrait le temps passe dans SDMMC lorsque celui-ci preempte l'IRQ AUDIO.
Aucune autre priorite n'est changee.

Le placement produit reste celui de `main`: payload en SDRAM, descriptors et
index chauds en D1, cursors/emplois Stream locaux en D2, et controle du cache en
D3. Aucun placement DTCM agressif du prototype n'est repris. Sur le build
Release de reference, le payload fait 6 160 384 octets, les 376 descriptors
font 24 064 octets, l'index fait 15 040 octets et la table last-slot fait
1 544 octets. Les occupations globales sont DTCM 130 816, D1 515 904,
D2 non-cacheable 6 592, D2 cacheable 107 776, SRAM2/D2 130 240,
SRAM3/D2 23 264, D3 52 768 et SDRAM 14 030 944 octets.

## Fastpath sequentiel

Pour un burst coalesce de quatre pages, le transport programme `DLEN=65536`,
conserve `IDMABSIZE=512`, traite 128 `IDMABTC`, puis un seul `DATAEND` et un
seul `CMD12`. Le callback scatter traduit chaque index de chunk en page logique
et chunk dans la page, puis rearme la base IDMA inactive vers le payload final
du slot concerne. Les quatre slots n'ont pas besoin d'etre contigus en SDRAM et
aucun buffer temporaire 64 KiB n'est alloue.

Chaque `IDMABTC` invalide uniquement les 512 octets reellement ecrits et publie
le watermark de la page concernee: 64, 128, ... 2048 frames, puis repart a 64
sur la page suivante. Une page complete reste `LOADING` avec un watermark a
2048 jusqu'au `DATAEND` global. Le contrat `LOADING -> READY` reste donc lie a
la completion backend, tandis qu'AUDIO peut deja lire tout le prefixe publie.
A la completion physique, les quatre tokens sont valides et termines
individuellement.

### Eligibilite, reservation et mapping

L'eligibilite est dynamique et appartient a une voix et sa sequence courante,
pas a un type de track. Le burst est envisage uniquement pour `NEXT`, lorsque
`CURRENT` est deja `READY`, que le kernel est forward, que le lease decrit une
suite sequentielle, qu'aucun saut hors de `CURRENT/NEXT` n'est pending et que
la borne END ou LOOP END laisse au moins deux pages completes utiles. Le pitch
ne sert pas de drapeau d'eligibilite: x1, x2, x4 et x8 utilisent le meme
contrat. La premiere politique tente simplement jusqu'a quatre pages utiles.

Avant la soumission, le Stream Manager fige les keys et generations, verifie
le format FLOAT32 stereo canonique, le mapping physique, puis reserve les slots
necessaires. Le resolver FAT peut traverser plusieurs extents uniquement si le
LBA suivant est exactement adjacent. Il reduit le burst de 4 a 3, 2, puis 1;
une rupture d'extent n'est jamais traversee silencieusement. Un manque de slot,
une page deja presente/loading, une page finale partielle ou un format non
direct reduit egalement le burst. Une seule page retombe sur la transaction
normale 16 KiB. AUDIO n'attend jamais que quatre slots deviennent disponibles.

### Scheduler, sauts et compatibilite

Le scheduler reste unique. L'ordre de besoin est: page `CURRENT` froide/saut
urgent, continuation `NEXT`, puis preparation de boucle ou prefetch plus
lointain. Un fastpath non encore soumis physiquement est annule si le lease de
la voix change; le nouveau seek peut alors lancer son 16 KiB urgent. Une
transaction deja active finit normalement: cette version n'ajoute pas de
preemption `CMD12`.

Un P-lock START, SLICE ou sample publie une nouvelle key/cible. La demande
froide correspondante passe par le chemin normal 16 KiB. Des que le nouveau
lease redevient forward et stable, sa continuation redevient eligible. Un
START live qui reste dans la page utile courante ne retire pas cette
eligibilite; un START hors sequence conserve latest-wins et le meme retour au
fastpath apres stabilisation.

Chaque voix Multi possede toujours son lease et son fastpath. Aucun burst ne
melange deux voix, fichiers ou zones, meme si leurs LBA sont voisins. Classic
garde START live et les P-locks. Pour une loop forward, la borne sequentielle
est LOOP END et les roles `LOOP_START`/`LOOP_START_NEXT` restent prepares par
le mecanisme existant; rien n'est lu au-dela. Reverse et ping-pong ne font pas
partie du produit. Les writes
Recorder/Looper/Overdub restent de gros blocs asynchrones et partagent sans
changement l'admission du scheduler; ils ne sont jamais decoupes en writes de
512 octets.

Les compteurs produit exposes sont `fastpath_requests`,
`fastpath_pages_requested`, les comptes 2/3/4 pages,
`fastpath_fallback_noncontiguous`, `fastpath_fallback_cache_reserve`,
`fastpath_cancel_before_submit`, `fastpath_completed` et
`normal_16k_requests`.

## Multi: LOOP ON jusqu'au release

L'UI et la persistence conservent un unique booleen `PARAM_SAMPLER_MULTI_LOOP`,
affiche `Off` ou `On`. Il n'existe ni mode `FOREVER`, ni nouveau champ:

```text
LOOP OFF -> lecture jusqu'a SAMPLE END
LOOP ON  -> LOOP START <-> LOOP END tant que le gate est actif
            puis, au release, franchissement naturel de LOOP END
            et lecture de la tail vers SAMPLE END
```

Le release employe est le vrai `NOTE_OFF` Multi, qualifie par l'identite de
l'output dans `audio_command_executor_apply_seq_event()` puis route par
`audio_note_engine_adapter_apply_output()` vers
`brick6_sampler_runtime_note_off_multi_track_note_output()`. Ce chemin ferme
l'enveloppe VCA voix par voix avec
`brick6_sampler_runtime_multi_release_voice_vca()`; il ne cree aucun gate
parallele. Le meme helper retire maintenant `SAMPLE_PLAY_LOOP_FORWARD` du
`play_plan` de cette voix et appelle `sample_voice_reader_update_bounds()` en
conservant position, pitch, region et points de loop. La decision et le rendu
sont tous deux serialises sur la frontiere AUDIO: un release applique avant la
decision de wrap empeche ce wrap; un wrap deja commite dans le bloc precedent
reste acquis et le suivant est interdit. Aucun lock n'est ajoute.

Un release avant LOOP START desactive la loop avant qu'elle soit atteinte. Un
release dans la loop laisse la tete atteindre LOOP END, sans seek, puis continue
sur les frames reelles jusqu'a SAMPLE END. Les enveloppes et leur reclamation
physique restent celles du produit: la source est rendue pendant la release VCA
et peut etre liberee lorsque cette enveloppe n'a plus besoin de source. Retrig,
steal, choke/stop et polyphonie gardent leurs handles et etats par voix.

Le changement de plan republie immediatement le lease. `LOOP_START` et
`LOOP_START_NEXT` disparaissent, tandis que `CURRENT`/`NEXT` sont recalcules
avec SAMPLE END comme borne sequentielle; le fastpath peut donc reprendre sur
la tail. Le neighbor d'interpolation eventuellement pointe sur LOOP START est
invalide au meme changement, avant le retrait de ses roles de lease. Il n'y a
ni I/O AUDIO, ni attente, ni nouveau stockage par voix.

`PARAM_SAMPLER_MULTI_LOOP` reste un `BOOL` dans le catalogue, une valeur float
dans le codec CONTROL et la meme cle persistante. La page TONE Multi
reste `INST / GAIN / LOOP`; Preview, LOOP OFF, pitch, velocity, filtres,
enveloppes et formats mono/stereo ne changent pas. Multi ne possede pas de
reverse.

## Audit de la generation automatique des loops Multi

### Chemin complet et moment du calcul

Le calcul n'est execute ni a l'assignation d'une track, ni au Project Load, ni
a la volee:

1. `ui_page_settings_multi_prepare_entry()` lance
   `multi_sample_import_start()` lors de la preparation d'un dossier Multi.
2. La state machine cooperative `multi_sample_import_service()` scanne un WAV
   par appel de l'etat `MULTI_IMPORT_WORK_SCAN` et appelle
   `multi_import_add_wav()`.
3. `wav_parser_parse_info()` decrit le fichier;
   `multi_import_read_wav_metadata()` lit les chunks RIFF `smpl` et `inst`.
4. Un premier loop forward valide du chunk `smpl` est prioritaire. Son end
   inclusif RIFF devient un end exclusif BRICK par `end + 1`.
5. Sans loop `smpl` valide, `multi_import_try_auto_loop()` analyse deux
   fenetres du WAV source. Un echec d'analyse appelle
   `multi_import_auto_loop_mechanical_bounds()`.
6. `multi_import_add_wav()` copie `has_loop`, `loop_begin`, `loop_end` et le
   flag `MULTI_SAMPLE_INDEX_META_LOOP_AUTO` dans
   `multi_sample_index_source_sample_t`. Si le WAV est converti vers le format
   canonique 48 kHz FLOAT32 stereo, `multi_import_refresh_converted_sample()`
   redimensionne les deux positions avec arrondi au frame le plus proche.
7. `multi_import_write_index()` puis `multi_sample_index_write()` persistent
   les positions dans l'enregistrement sample 64 octets du fichier
   `.brickmulti` (offsets 36 et 40), protege par le CRC de l'index. Les points
   auto ne sont pas ajoutes au chunk `smpl` du WAV converti.
8. Au chargement ou Project Load, `multi_sample_index_load()` et
   `multi_sample_index_apply_to_pool()` restaurent les champs via
   `multi_sample_pool_set_sample_loop()`. Le passage de l'instrument a
   `MULTI_SAMPLE_INSTRUMENT_READY` publie `loop_begin/end` dans la projection
   AUDIO avec `multi_sample_audio_projection_publish()`.
9. A chaque Note On, `multi_sample_audio_projection_resolve()` choisit la zone;
   `brick6_sampler_runtime_build_common_play_plan()` applique les points si le
   booleen LOOP de la track est ON, puis le `sample_voice_reader` les utilise
   comme borne forward et comme roles de lease de loop.

Les fichiers autoritaires de ce trajet sont:

| Etape | Fichier | Fonctions principales |
|---|---|---|
| commande UI d'import | `Src/UI/pages/Settings/ui_settings_multi_assets.inc` | `ui_page_settings_multi_prepare_entry` |
| metadata WAV et calcul | `Src/Sampler/multi_sample_import.c` | `multi_import_read_wav_metadata`, `multi_import_try_auto_loop`, `multi_import_add_wav` |
| persistence `.brickmulti` | `Src/Sampler/multi_sample_index.c` | `multi_sample_index_write/load`, `multi_sample_index_apply_to_pool` |
| metadata runtime CONTROL | `Src/Sampler/multi_sample_pool.c` | `multi_sample_pool_set_sample_loop`, `multi_sample_pool_set_state` |
| publication CONTROL vers AUDIO | `Src/Sampler/multi_sample_audio_projection_control.c` | `multi_sample_audio_projection_publish` |
| resolution note/velocite AUDIO | `Src/Audio/multi_sample_audio_projection_audio.c` | `multi_sample_audio_projection_resolve` |
| creation de voix et gate | `Src/Audio/Engines/Sampler/sampler_multi_voice.inc`, `sampler_audio_common.inc` | `brick6_sampler_runtime_start_multi_renderer`, `brick6_sampler_runtime_multi_release_voice_vca` |
| rendu, wrap et leases | `Src/Sampler/VoiceReader/sample_voice_reader_cursor.inc`, `sample_voice_reader_multi_kernels.inc` | `sample_voice_reader_begin_segment`, `sample_voice_reader_commit_segment`, kernels Multi |

Le calcul est donc persistant par sample dans `.brickmulti`; le Patch/Project
ne persiste que la reference d'asset et le booleen LOOP de la track.

### Algorithme auto reel

Les constantes vivent dans `Src/Sampler/multi_sample_import.c`:

| Parametre | Valeur |
|---|---:|
| cible LOOP START | 40 % du sample (`2/5`) |
| cible LOOP END | 55 % du sample (`11/20`) |
| demi-fenetre autour de chaque cible | 150 ms |
| rayon du matching | 16 frames |
| fenetre comparee par paire | 33 frames |
| candidats conserves par cote | 64 maximum |
| duree minimale de reference | 150 ms |
| buffer I/O | 4096 octets |
| resolution des points | 1 frame source, puis arrondi lors du resampling |

Chaque fenetre couvre donc jusqu'a environ 300 ms, augmentes des marges du
matching. A 48 kHz elle contient au plus 14 436 frames. Cette borne est calculee
sur 48 kHz: un source de frequence superieure dont la fenetre depasse la borne
retombe sur les points mecaniques.

`multi_import_auto_loop_collect_candidates()` transforme chaque frame en un
signal mono `(L + R) / 2` uniquement pour detecter les passages par zero. Les
passages montant et descendant sont distingues. Pour chacun, le candidat
memorise la somme d'amplitude instantanee `abs(L)+abs(R)`, une energie locale,
les pentes L et R sur deux frames et la direction. Lorsque 64 candidats sont
deja presents, seuls les passages dont l'amplitude instantanee est inferieure
au pire candidat conserve peuvent entrer. L'energie calculee et stockee n'est
ensuite jamais utilisee par le score.

Toutes les paires START/END valides sont evaluees. Pour les 33 frames centrees
sur les deux passages, `multi_import_auto_loop_match_score()` additionne
separement les differences absolues L et R, puis calcule:

```text
score = moyenne(amplitude instantanee START, END)
      + difference des pentes L/R / 8
      + moyenne des differences waveform L/R * 4
```

Un couple de passages de meme direction est prefere. A defaut, le meilleur
couple toutes directions est accepte. Une loop plus courte que 300 ms recoit
une penalite lineaire, mais 150 ms n'est pas une contrainte dure du chemin
score. Il n'existe ni estimation de periode, ni correlation normalisee, ni
analyse RMS de sustain, ni seuil absolu qui refuserait un mauvais raccord.

La stereo est donc traitee de facon mixte: le zero crossing candidat vient du
mid `(L+R)/2`, mais amplitude, pente et matching comparent L et R separement et
les additionnent dans un score commun. Un bon resultat global est favorise sur
les deux canaux, sans obligation ni seuil de qualite individuel; une annulation
L/R peut creer un zero du mid alors qu'aucun canal n'est proche de zero.

### Fallbacks et cas particuliers

Le fallback mecanique choisit 40 % et 55 %, et etend si possible la loop a
150 ms. Il garantit seulement `begin < end` pour un sample d'au moins deux
frames. Il est utilise si la lecture d'une fenetre echoue, si aucune fenetre ne
tient dans l'arene statique, s'il manque des zero crossings, ou si aucune paire
n'est valide. Il n'y a pas de verdict «aucune bonne loop»: une paire finie est
toujours acceptee, sinon les pourcentages le sont. Un sample d'une frame ne
recoit pas de loop auto; le runtime normalise alors LOOP ON sur la region
entiere.

- Sample tres court: fenetres tronquees; sous 150 ms, le fallback garde
  essentiellement 40--55 % et peut produire une loop tres courte.
- Percussion: le placement 40/55 evite souvent l'attaque, mais ne cherche pas
  explicitement une zone stationnaire dans la decroissance.
- Pad long: les deux zones centrales tombent souvent dans le sustain et le
  matching 33 frames peut donner un raccord correct.
- Waveform periodique: nombreux zero crossings et matching waveform utiles,
  mais aucune contrainte ne rend la longueur multiple de la periode.
- Bruit: nombreux candidats mais faible coherence; faute de seuil, le meilleur
  raccord disponible est accepte meme s'il reste mauvais.
- Silence final: il n'est pas vise (END est centre a 55 %, fenetre typiquement
  limitee vers 70 %); s'il entre dans une fenetre tronquee, le filtre de faible
  amplitude peut le favoriser.
- DC offset: l'absence de passage par zero declenche le fallback mecanique;
  aucune suppression de DC n'est appliquee.
- Stereo L/R tres differente: le score voit les deux canaux mais le zero du mid
  et l'absence de seuil par canal rendent le choix fragile.

### Cout et verdict

Le calcul est borne et offline dans STORAGE, mais l'analyse d'un WAV reste
synchrone a l'interieur d'un tick de scan. Par sample, il lit au maximum deux
fenetres de 14 436 frames, soit 28 872 frames (environ 231 ko pour un source
stereo FLOAT32), par blocs de 4096 octets. Sa complexite est
`O(W * R + Z * C + C^2 * R)`: scan et energie locale, maintien des 64 meilleurs
candidats, puis au plus 4096 paires de 33 frames. `R=16` et `C=64` la bornent,
mais le pire cas tres riche en zero crossings effectue plusieurs millions
d'operations entieres simples dans l'appel de scan.

Les buffers statiques propres a l'auto-loop occupent 123 168 octets de
`.sdram_multi_import`: 115 488 octets de frames, 3 584 octets de candidats et
4096 octets d'I/O. La section d'import complete mesure 291 200 octets dans le
build Release de reference; elle est NOLOAD et n'est pas une allocation par
voix. Le rendu ne paie aucun de ces calculs.

Verdict **B -- correct mais ameliorable**. Le choix borne, sans allocation,
avec zero crossing, pentes et comparaison L/R est encore raisonnable pour un
H743 et explique probablement la conception historique: obtenir a l'import un
raccord souvent propre avec un cout deterministe, peu d'I/O et aucune analyse
DSP lourde. C'est une inference motivee par le code, pas une specification
historique retrouvee. Les faiblesses actuelles sont le zero crossing du mid,
l'energie inutilisee, les centres fixes, la fenetre tres courte, l'absence de
normalisation/periode et surtout l'absence de seuil de rejet.

Une passe future pourrait, sans toucher au runtime: retirer le DC, detecter une
zone de sustain stable par energie/variation spectrale, proposer des candidats
par canal, scorer amplitude et pente L/R avec seuil individuel, employer une
correlation normalisee sur une fenetre plus longue, estimer la periode pour les
sons harmoniques, puis conserver un fallback explicite marque «qualite faible».
Cette refonte n'est pas incluse ici.

### Validation fonctionnelle cible

Le contrat a verifier sur hardware couvre: gate long avec plusieurs wraps puis
tail, gate relache avant LOOP START sans premier wrap, release dans la loop,
release autour de LOOP END, retrig, voix Multi simultanees dans des etats gate/
release differents, LOOP OFF, pitch 1x et interpole, cache hit/cold page,
transition `LOOP_START` vers `CURRENT/NEXT`, enveloppes/velocity, steal/choke et
Save/Load. Les chemins Classic, Stream et Recorder ne sont pas modifies par
cette semantique Multi.

## Projection RT1172 (pas une mesure hardware)

Pour 16 voix, 48 kHz, stereo FLOAT32 et pitch x8, le debit utile projete est
`16 * 48000 * 8 * 8 = 49,152 MB/s`. Avec une hypothese RT1172 de 80 MB/s et
une latence fixe conservatrice de 300 us:

| I/O physique | transactions/s | cout fixe | transfert | total SD theorique |
|---|---:|---:|---:|---:|
| 16 KiB | 3000 | 900 ms/s | 614,4 ms/s | 151,4 % |
| 64 KiB coalesce | 750 | 225 ms/s | 614,4 ms/s | 83,9 % |

Ces nombres dimensionnent la future cible et supposent une lecture entierement
sequentielle/coalescee. Ils ne sont ni un benchmark ni un critere d'acceptation
du H743. Sur le build H743 Release introduisant le fastpath, le delta statique
par rapport au build STREAM 16 KiB de reference est: DTCM +192 octets, D1 0,
D2 cacheable +736 octets (D2 non-cacheable, SRAM2 et SRAM3: 0), D3 0 et SDRAM
+96 octets. Aucun payload Page Cache ni buffer de donnees supplementaire n'est
alloue.
