# BRICK — Passation refonte Track / PLAY / CONTROL↔AUDIO / préparation dual-MCU

Date de passation : 2026-08-12  
Projet : BRICK groovebox STM32  
HEAD audité durant les audits d’architecture : `62d09437b604040a4b7da4a20b881d0a8cf77782`

> Ce document sert de contexte de reprise pour une nouvelle conversation ChatGPT.  
> Il rassemble les faits établis, les décisions déjà prises, les mesures utiles et les questions encore ouvertes.  
> Il ne doit pas être traité comme un plan d’implémentation figé : plusieurs choix de placement mémoire et de découpage restent à arbitrer après synthèse.

---

# 1. But général du chantier

Le firmware actuel fonctionne mais mélange plusieurs notions et autorités :

- track top-level ;
- lane séquenceur ;
- runtime context ;
- mixer target ;
- instance moteur ;
- capacité PLAY ;
- polyphonie logique/configurée ;
- disponibilité physique des voix ;
- UI ;
- scheduler ;
- moteurs ;
- GROUP.

Le chantier vise à **simplifier les contrats**, supprimer les dépendances historiques et préparer une séparation future :

- **M4 = CONTROL**
- **M7 = AUDIO temps réel**

tout en conservant **la compatibilité STM32H743 monocore**.

La préparation dual-core doit donc d’abord fonctionner sur H743 :

```text
H743 monocore
CONTROL
    ↓ événements / commandes / snapshots explicites
AUDIO
```

Puis plus tard sur H747 :

```text
M4 CONTROL
    ↓ même contrat métier
M7 AUDIO
```

Le passage H747 ne doit pas nécessiter une nouvelle refonte métier.

---

# 2. Cibles build réellement importantes

Pour ce chantier, ne pas disperser l’effort sur les variantes historiques.

Cibles à préserver / valider :

- **Low-Cost Release**
- **Premium**

Le build Debug et les variantes temporaires ne sont pas une priorité pour cette refonte.

Un audit isolé du HEAD a trouvé que le preset Debug actuel échoue sur `audio_test2.c`, tandis que Low-Cost Release et Premium passent. Ce défaut Debug est hors priorité ici.

---

# 3. Aucune rétrocompatibilité projet requise

Le produit est encore en prototypage.

Il n’existe aucun projet utilisateur sauvegardé sur machine à préserver.

Conséquences :

- aucune compatibilité avec anciens fichiers projet/pattern nécessaire ;
- aucun convertisseur legacy ;
- possibilité de supprimer `PatternSaveV1`, `ProjectSaveV1` et autres formats historiques ;
- possibilité de repartir sur un format canonique propre ;
- privilégier simplicité et cohérence du modèle futur.

---

# 4. Topologie produit à préserver

## 4.1 Tracks top-level

Il existe **8 tracks top-level**.

L’identité utilisateur principale est `0..7`.

## 4.2 GROUP

Une seule GROUP par projet.

La track top-level 8 côté utilisateur / index logique 7 côté code devient la **GROUP master**.

La GROUP possède 8 sub-tracks.

État conceptuel souhaité :

```text
TOP-LEVEL TRACK
├─ identité stable
├─ séquence
├─ rôle
├─ moteur/config
└─ runtime

TRACK 7/8 en rôle GROUP
├─ master
└─ 8 children / sub-tracks
```

## 4.3 GROUP master

Décision fonctionnelle actuelle :

- reste fondamentalement une track top-level ;
- conserve sa séquence ;
- peut conserver un ensemble PLAY comme une track normale ;
- mais **n’émet aucune note** ;
- sa propriété logique doit être équivalente à `can_emit_notes = false`.

Donc :

```text
GROUP master
PLAY stockable : oui
émission note : 0
```

Les moteurs ne doivent pas connaître le cas GROUP master : le filtrage doit être fait par le rôle logique avant émission.

## 4.4 GROUP children / sub-tracks

Les sub-tracks sont strictement monophoniques.

Cible fonctionnelle :

```text
GROUP child
PLAY utile / step : 1
can_emit_notes : true
```

Aujourd’hui elles sont des lanes `8..15`, activées uniquement lorsque la track 7 est GROUP.

---

# 5. Décision PLAY recherchée

## 5.1 Tracks normales

Objectif :

- **8 notes PLAY stockables par step**
- indépendamment du moteur courant.

UI cible :

```text
PLAY 1/2 → V1 V2 V3 V4
PLAY 2/2 → V5 V6 V7 V8
```

Les pages PLAY ne doivent plus apparaître/disparaître selon la polyphonie du moteur.

## 5.2 Changement de moteur

Un changement de moteur ne doit jamais supprimer/tronquer les PLAY.

Exemple :

```text
step contient 8 notes
moteur mono → seule la réalisation sonore mono s’applique
puis moteur 8 voix → les 8 notes sont toujours présentes
```

Le stockage musical est indépendant du moteur.

## 5.3 GROUP master

Cible :

- peut garder le même stockage PLAY qu’une top-level normale ;
- `can_emit_notes = false` ;
- ses PLAY n’ont pas d’effet sonore.

## 5.4 GROUP child

Le child est intrinsèquement mono.

L’utilisateur a explicitement remis en question le choix « 8 PLAY physiques partout ».

Le choix **top-level×8 / child×1** est à réévaluer sérieusement car :

- il reflète le contrat réel ;
- il réduit fortement le surcoût D2 ;
- la D2 est très chargée.

Ne pas imposer automatiquement une structure uniforme ×8 sur les children simplement pour simplifier le code.

---

# 6. Workflow Step Hold → clavier déjà voulu / chantier associé

Workflow fonctionnel :

Quand l’utilisateur maintient un ou plusieurs steps et joue au clavier :

- les notes jouées sont écrites dans les PLAY ;
- la vélocité jouée est enregistrée ;
- `MICTIM = 0` dans ce mode ;
- les anciennes NOTE/VEL PLAY sont remplacées ;
- le `LEN` existant doit être préservé ;
- les autres p-locks et ROLL restent inchangés.

Arbitrage Quick Length :

- plusieurs steps tous vides → capture clavier autorisée ;
- plusieurs steps tous remplis → capture autorisée ;
- un seul step → capture possible sans casser short press ;
- sélection mixte rempli + vide → ne pas détourner Quick Length ;
- `filled + empty` reste prioritairement Quick Length.

Ce workflow doit s’adapter à la future capacité PLAY.

---

# 7. Ce que les audits ont trouvé sur le modèle actuel

## 7.1 Définitions actuelles

- `track` : index top-level `0..7`, autorité principale `track_state`.
- `lane` : index séquenceur `0..15`.
- `sub-track` : lane `8..15` si la track 7 est GROUP.
- runtime : `track_runtime_ctx_t[16]`, indexé par lane.
- mixer target : `0..16`, attribution dynamique.
- engine owner : `engine + instance_id`.

Les types `track ID` / `lane ID` existent mais la représentation numérique et beaucoup d’APIs restent confuses.

## 7.2 GROUP actuel

La relation GROUP parent/children n’est pas stockée dans une vraie structure GROUP.

Elle est reconstruite depuis :

```text
track_state_get_type(track 7) == GROUP
```

Le parent est toujours track/lane 7.

Les children sont reconstruits par plage `8..15`.

Il n’existe pas encore de `group_id` / table de membres / owner explicite GROUP.

## 7.3 GROUP master actuel

Lorsque track 7 devient GROUP :

- son identité top-level reste ;
- lane 7 reste ;
- séquence reste ;
- p-locks restent ;
- runtime devient GROUP ;
- engine = NONE ;
- `can_emit_notes = 0` ;
- pas de source audio propre.

Des PLAY historiques peuvent physiquement rester mais certaines validations empêchent actuellement leur écriture.

## 7.4 GROUP child actuel

Le child possède déjà :

- une lane indépendante ;
- une séquence indépendante ;
- steps/p-locks indépendants ;
- playhead ;
- Note FX lane ;
- runtime ;
- sampler RAM ;
- mixer target.

Mais il n’est pas une vraie top-level track.

La monophonie child est actuellement dispersée :

- runtime annonce 1 ;
- UI montre 1 ;
- paramètres V2..V4 bloqués ;
- modèle stocke encore 4 PLAY.

C’est précisément une autorité à simplifier.

---

# 8. Dispersion d’autorité actuelle

Les audits ont identifié plusieurs sources de vérité concurrentes.

## 8.1 Rôle

Le rôle est déterminé/recalculé par :

- `track_state.type`
- `seq_lane`
- `track_runtime.type`
- flags runtime
- tests numériques `<8`, `>=8`
- tests spécifiques track 7.

## 8.2 PLAY

Aujourd’hui :

- stockage : 4 voix fixes dans `seq_model`;
- édition : paramètres V1..V4;
- UI : 4 pages/subpages masquées selon runtime;
- live rec : hardcodé 4;
- snapshot : 4;
- scheduler : 4;
- runtime : capacité parfois 1 ou N;
- moteurs : propres règles physiques.

Il n’existe pas une autorité unique.

## 8.3 Polyphonie

Distinguer absolument quatre notions :

```text
1. capacité PLAY stockable
2. capacité du rôle à émettre
3. polyphonie nominale/configurée du moteur
4. disponibilité physique instantanée
```

La disponibilité physique ne doit jamais décider combien de PLAY existent dans la séquence.

## 8.4 Mixer

Le mixer utilise actuellement 17 targets :

- lanes / tracks ;
- bus GROUP spécial.

Le mapping logique→mixer est dynamique et le reverse mapping peut être fragile.

Le mixer target ne doit jamais devenir une identité logique.

---

# 9. Pipeline note actuel

Pipeline séquence actuel :

```text
seq_model
→ seq_runtime_exec
→ seq_boundary_engine
→ seq_play_scheduler
→ queue scheduler
→ audio collect/apply
→ Note FX
→ admission terminale
→ moteur/MIDI
→ allocator
→ rendu
→ mixer
```

Beaucoup de cette logique tourne aujourd’hui dans le domaine IRQ audio.

## 9.1 Transformation PLAY → événement

Le scheduler convertit :

- NOTE → note finale ;
- VEL → vélocité ;
- LEN → timestamp note-off ;
- MICTIM → timestamp note-on.

Une fois l’événement final créé, AUDIO n’a plus besoin de connaître :

- step ;
- index PLAY ;
- LEN ;
- MICTIM ;
- structure `seq_step_play_t`.

C’est un point clé pour la future frontière CONTROL/AUDIO.

## 9.2 Tokens / note-off

Note-on et note-off sont associés par tokens/générations/occurrences, pas par l’index PLAY.

Le passage 4→8 n’impose donc pas de refaire fondamentalement l’identité note-on/off, mais augmente les budgets d’événements.

---

# 10. Note FX

Les Note FX musicaux existants doivent conceptuellement appartenir au CONTROL :

- Probability
- Gate
- Groove
- Harmony/Voicing
- Euclid
- ARP

Ils peuvent :

- supprimer ;
- retarder ;
- transformer ;
- générer plusieurs notes ;
- posséder leurs propres note-offs/tokens.

## Point important polyphonie / ARP

Ne pas couper automatiquement les sources PLAY à la polyphonie du moteur avant Note FX.

Exemple voulu :

```text
8 PLAY source
→ ARP
→ moteur mono
```

L’ARP doit pouvoir voir les 8 sources.

Il faut donc distinguer :

```text
PLAY source capacity
Note FX fan-out/budget
éventuelle limitation musicale de sortie
admission physique AUDIO
```

Éviter de créer deux allocateurs concurrents :

```text
CONTROL voice stealing
+
AUDIO voice stealing
```

L’allocation/stealing physique reste AUDIO.

---

# 11. Frontière CONTROL / AUDIO retenue par les audits

## 11.1 CONTROL

Cible conceptuelle :

- projet ;
- édition ;
- persistance ;
- topologie logique ;
- séquence ;
- progression musicale ;
- PLAY ;
- p-locks ;
- live recording ;
- Hall/clavier ;
- transformations musicales ;
- Note FX ;
- mute logique ;
- configuration moteur ;
- polyphonie nominale/config ;
- demande de binding.

## 11.2 AUDIO

Cible conceptuelle :

- horloge sample ;
- exécution de commandes datées ;
- binding effectivement installé ;
- adaptateurs moteurs ;
- admission physique ;
- allocation / stealing ;
- sampler streaming ;
- voix ;
- DSP ;
- mixer ;
- filter/VCA ;
- enveloppes/LFO DSP nécessitant le temps audio ;
- panic d’exécution ;
- télémétrie.

## 11.3 Principe

```text
CONTROL décide ce qui doit musicalement arriver.
AUDIO décide comment le rendre physiquement.
```

---

# 12. Scheduler M4 / exécution M7

La direction recommandée est :

```text
CONTROL / futur M4
├─ progression
├─ PLAY
├─ p-locks
├─ Note FX
├─ calcul timestamps
└─ événements finaux datés
        ↓
AUDIO / futur M7
├─ horloge sample
├─ petite queue temporelle
├─ exécution à l’échantillon demandé
├─ admission
└─ DSP
```

Le M7 ne devrait pas avoir besoin de connaître :

- step ;
- PLAY ;
- LEN ;
- MICTIM ;
- ARP ;
- Euclid.

Il reçoit « exécuter telle action au sample X ».

---

# 13. Synchronisation rythmique future M4/M7

Oui, séquenceur sur M4 et audio sur M7 sont compatibles avec la précision sample.

Le principe :

- M7 est autorité de l’horloge audio/sample ;
- M4 planifie légèrement en avance ;
- les événements comportent un timestamp sample ;
- M7 applique l’événement exactement à l’offset voulu dans le bloc audio.

HEAD actuel :

- 48 kHz ;
- 64 frames par demi-buffer ;
- événements déjà applicables à l’échantillon exact dans les 64 frames.

Le futur lookahead exact n’est pas encore chiffré.

Il dépendra de :

- tempo max ;
- microtiming négatif max ;
- jitter CONTROL ;
- latence inter-core ;
- coût calcul CONTROL ;
- délai queue.

Le contrat doit permettre de mesurer ce lookahead plus tard sans changer l’architecture.

---

# 14. Architecture par bloc déjà compatible

Le firmware a déjà été refondu pour calculer beaucoup de modulation / enveloppes / paramètres par bloc plutôt que par sample.

Cela reste compatible avec la séparation :

- évolution normale : valeurs/rampes par bloc ;
- événement précis : commande horodatée au sample.

Le M4 configure / produit les commandes.
Le M7 conserve les calculs DSP nécessaires au rendu audio exact.

---

# 15. Événement CONTROL → AUDIO conceptuel

Les audits proposent une structure autonome contenant typiquement :

```text
protocol/version
epoch
message sequence
logical entity ID
topology generation
binding generation
due_sample
ON/OFF
occurrence/token
source/provenance
route
note
velocity
flags
```

À ne pas transporter :

- pointeur ;
- mixer slot ;
- instance moteur ;
- index PLAY ;
- step ;
- LEN ;
- MICTIM.

L’identité logique doit rester stable lorsque le moteur change.

---

# 16. Changement moteur

Le changement de moteur doit être atomique/logiquement versionné.

Principe :

- `entity_id` stable ;
- nouvelle `binding_generation` ;
- anciens événements deviennent stale ;
- ancien moteur est fermé ;
- nouveau binding installé ;
- ACK possible ;
- les futurs événements utilisent la nouvelle génération.

Un ancien note-off ne doit jamais fermer une note du nouveau moteur.

---

# 17. Paramètres / p-locks à travers la frontière

Aujourd’hui `seq_param_iface` et autres composants mutent directement plusieurs états audio.

À terme, la frontière doit remplacer ces accès directs.

Direction retenue :

- base persistante : CONTROL ;
- p-lock actif : CONTROL ;
- restauration base : CONTROL ;
- config modulation : CONTROL ;
- valeur DSP effective : AUDIO ;
- phase/enveloppes/LFO audio-rate : AUDIO.

Possibilités de transport :

- commandes paramètre datées ;
- snapshots versionnés pour changements structurels ;
- hybride.

Ne pas copier automatiquement un gros snapshot à chaque step.

---

# 18. GROUP common

Aujourd’hui il n’existe pas de vraie autorité GROUP commune.

La cible n’est pas « master p-lock → fan-out magique vers tous les children ».

Les paramètres réellement communs doivent être explicitement GROUP :

- bus commun ;
- inserts communs ;
- LFO/modulation commune selon contrat produit ;
- mute parent ;
- autres destinations dédiées.

Le bus GROUP doit devenir une vraie entité AUDIO/routing claire.

---

# 19. Mute

Aujourd’hui le mute existe sous plusieurs formes.

Cible conceptuelle :

```text
mute logique CONTROL
→ mute effectif (avec héritage GROUP)
→ projection AUDIO
```

CONTROL cesse les nouveaux ON.
AUDIO applique la projection temps réel.
Les OFF continuent à être acceptés.

---

# 20. Compatibilité H743 obligatoire

Invariant majeur :

> La préparation dual-core ne doit pas rendre le firmware dépendant du H747/M4.

Le H743 reste une cible supportée après la refonte.

Sur H743 :

```text
CONTROL et AUDIO tournent sur le même M7
mais communiquent déjà via les mêmes contrats explicites
```

Sur H747 :

```text
CONTROL → M4
AUDIO → M7
```

La migration doit principalement changer le placement/exécution/transport, pas les contrats métier.

---

# 21. RAM H743 vs H747 — point important

Le H747 ne donne pas une seconde copie complète de la SRAM du H743.

Les SRAM internes sont des ressources physiques de la puce à répartir/partager.

Donc :

```text
déplacer une responsabilité CPU M7 → M4
≠
libérer automatiquement sa RAM
```

Une donnée ne libère D2 que si elle est physiquement replacée ailleurs.

Ne jamais raisonner « M4 = nouvelle RAM gratuite ».

---

# 22. Mesures mémoire réelles HEAD

Audit isolé du HEAD.

## Low-Cost Release

```text
FLASH  : 1 105 404 / 1 835 008 B
DTCM   :   122 880 /   131 072 B
D1     :   503 776 /   524 288 B
D2     :   275 712 /   294 912 B
D3     :    38 688 /    65 536 B
SDRAM  :31 996 064 /32 505 856 B
Recorder: 198 432 / 1 048 576 B
```

## Premium

```text
FLASH  : 1 097 672 / 1 835 008 B
DTCM   :   122 880 /   131 072 B
D1     :   483 264 /   524 288 B
D2     :   276 096 /   294 912 B
D3     :    38 688 /    65 536 B
SDRAM  :32 016 544 /32 505 856 B
Recorder: 198 432 / 1 048 576 B
```

Marges critiques approximatives :

- DTCM : ~8 KiB libres ;
- D1 Low-Cost : ~20 KiB libres ;
- D2 : ~18–19 KiB libres ;
- SDRAM : ~0,48–0,50 MiB libre ;
- recorder SDRAM : marge confortable.

---

# 23. Principaux consommateurs mémoire

## DTCM

Principalement AUDIO :

- FM voices ~13,6 KiB ;
- track filters ~12,8 KiB ;
- poly filters ~12 KiB ;
- Haas L/R ~9,6 KiB chacun ;
- sampler voices ~7,1 KiB ;
- autres états audio hot.

DTCM est déjà ~94 % utilisée.

Ne pas y ajouter du CONTROL.

## D1

Principalement AUDIO/MIXED :

- reverb buffer ~128 KiB ;
- sampler clip slots ~66 KiB ;
- slots ~43,5 KiB ;
- looper ~16 KiB ;
- capture/stream.

Low-Cost est ~96 % utilisé.

## D2

Mesure réelle ~276 KiB utilisée.

Principaux blocs :

- `g_seq_project` : 127 104 B ;
- paramètres runtime ~40 KiB ;
- scheduler/admission/output guard ~55 KiB ;
- Note FX ~27,6 KiB ;
- track/audio/config ~10 KiB ;
- DMA/LUT/buffers ~8 KiB.

D2 est une zone très chargée.

## SDRAM

Gros consommateurs :

- sample page pool ~23,5 MiB ;
- delays L/R ~2,2 MiB ;
- capture ~0,9 MiB ;
- projet ~368 KiB ;
- multiples gros buffers `PatternSaveV1`.

SDRAM est ~98,5 % utilisée.

---

# 24. PLAY actuel — tailles exactes

HEAD :

```text
seq_step_play_voice_t = 5 B
4 PLAY / step         = 20 B
seq_step_t            = 28 B
```

Un PLAY contient actuellement :

- NOTE ;
- VEL ;
- LEN ;
- MICTIM ;
- `present_mask`.

`g_seq_project` complet avec p-lock pool = 127 104 B.

Les PLAY sont hors du pool p-lock générique.

---

# 25. Coût PLAY×8 uniforme — attention

Si on passe **toutes les 16 lanes à 8 PLAY physiques** :

```text
g_seq_project :
127 104 B → 147 584 B
delta = +20 480 B
```

Cela ne tient pas tel quel dans la D2 actuelle :
marge ~18–19 KiB seulement.

Mais ce n’est PAS forcément la cible à choisir.

---

# 26. Coût top-level×8 / child×1

Calcul audité :

PLAY physiques :

```text
8 top-level × 64 × 8 × 5 = 20 480 B
8 children × 64 × 1 × 5 =  2 560 B
```

Le `seq_project` complet projeté est environ :

```text
129 664 B
```

Donc par rapport à HEAD :

```text
+2 560 B seulement
```

C’est une différence majeure.

À la reprise, ne pas déplacer tout le séquenceur de D2 uniquement parce que l’option uniforme ×8 dépasse de ~1–2 KiB.

Il faut réévaluer proprement le layout top-level×8 / child×1.

L’utilisateur préfère éviter une refonte mémoire disproportionnée si le vrai contrat ×8/×1 tient déjà proprement.

---

# 27. Question structure uniforme vs structure adaptée

Deux directions :

## Uniforme ×8 partout

Avantages :
- un seul stride ;
- accès simple ;
- snapshots simples.

Inconvénient :
- ~17,5 KiB gaspillés sur children ;
- ne tient pas actuellement en D2 sans déplacement.

## Top-level ×8 / child ×1

Avantages :
- reflète exactement le produit ;
- seulement ~+2,5 KiB D2 ;
- économise ~17,5 KiB.

Inconvénient :
- deux layouts / accès à encapsuler.

Important :

Si cette solution est retenue, la différence doit être encapsulée dans le modèle/API.

Éviter des `if (group_child)` dispersés partout.

Le prochain architecte doit arbitrer selon simplicité réelle, pas appliquer automatiquement « uniformiser ».

---

# 28. P-lock pool

Actuel :

```text
16 lanes × 1024 nodes × 6 B = 98 304 B
```

Pool fixe par lane.

PLAY ne doit pas y retourner.

Observation :

- capacité 1024 locks/lane ;
- maximum théorique steps×32 = 2048/lane ;
- contrat actuel à clarifier mais pas forcément à changer dans ce chantier.

---

# 29. Persistance actuelle

`PatternSaveV1` :

```text
~237 184 B
```

`ProjectSaveV1` :

```text
~377 096 B
```

Il existe plusieurs copies PatternSave en SDRAM.

La persistance actuelle dépend largement de snapshots C/`sizeof()`/padding natif.

Comme aucune rétrocompatibilité n’est requise, la cible est un format canonique sérialisé explicitement :

```text
HEADER
PROJECT
TOPOLOGY
ENTITY CONFIG
SEQUENCE
PLOCKS
NOTE FX
PARAM BASES
GROUP COMMON
ROUTING
ASSET REFERENCES
```

Aucun runtime AUDIO ne doit être sauvegardé.

---

# 30. Undo / clipboard / live-rec

## Undo

HEAD :

- snapshot step ~220 B ;
- historique total ~129 KiB SDRAM.

Avec PLAY×8 uniforme :
- snapshot ~240 B ;
- historique ~141 KiB.

Pas besoin d’un système delta complexe uniquement pour économiser quelques KiB.

## Clipboard

Doit copier uniquement le modèle CONTROL :

- steps ;
- PLAY ;
- p-locks ;
- métadonnées utiles.

Ne pas copier états moteur/audio.

## Live-rec

Live-rec est CONTROL.

Hardcodes V1..V4 à remplacer.

Le nombre de pending notes dépend des occurrences, pas directement du nombre de PLAY.

---

# 31. Stack — alerte audit

Le linker réserve seulement de petites valeurs minimales mais les `.su` ont révélé de grosses frames locales :

- `ui_core_clipboard_clear_track` ~19,9 KiB ;
- `seq_clipboard_paste` ~14,2 KiB ;
- `seq_step_snapshot_apply` ~14,2 KiB ;
- certains écrans audio ~11,8 KiB.

La vraie marge stack n’a pas encore été mesurée par watermark runtime.

Ne pas simplement augmenter une constante linker sans comprendre.

Ce point peut être traité comme nettoyage structurel séparé si nécessaire.

---

# 32. Ce qu’il ne faut PAS conclure trop vite

## Erreur 1

« Passage M4 → libère automatiquement D2 »

FAUX.

La RAM est physique et partagée selon la région.

## Erreur 2

« PLAY×8 impose de déplacer le séquenceur »

Pas établi.

Avec top-level×8 / child×1, le surcoût estimé n’est que ~2,5 KiB.

## Erreur 3

« Il faut uniformiser ×8 partout à tout prix »

Pas décidé.

Le coût mémoire réel rend ×8/×1 intéressant.

## Erreur 4

« La polyphonie moteur doit couper les PLAY avant ARP/Note FX »

À éviter : un ARP mono doit pouvoir utiliser un accord source multi-note.

## Erreur 5

« GROUP master n’a pas de PLAY »

Décision utilisateur actuelle : il peut garder le PLAY, mais son rôle n’émet aucune note.

## Erreur 6

« H747 remplace la nécessité de garder H743 propre »

FAUX.

Le H743 reste une cible supportée.

---

# 33. Audits déjà réalisés

## Audit 1 — Track / Lane / Sub-track

Conclusion :

> modèle exploitable, mais autorités et mappings dispersés ; refonte structurelle justifiée avant PLAY×8.

## Audit 2 — Autorités

Conclusion :

- rôle, PLAY, polyphonie, UI, mixer, runtime ont plusieurs sources de vérité ;
- besoin d’un descriptor/topology logique et de projections propres.

## Audit 3 — Pipeline note

Conclusion :

- frontière note autonome identifiable ;
- après scheduling AUDIO n’a plus besoin de step/PLAY/LEN/MICTIM ;
- allocations physiques restent AUDIO.

## Audit 4 — CONTROL/AUDIO / dual-core

Conclusion :

> frontière suffisamment définie conceptuellement.

Direction :

- CONTROL produit événements/commandes datés ;
- AUDIO exécute à l’horloge sample.

## Audit 5 — mémoire/persistance

Conclusion :

- structures principales chiffrées ;
- persistance legacy nettoyable ;
- besoin de vrais chiffres link.

## Audit 5b — link map / placement

Résultats :

- Low-Cost/Premium mesurés ;
- DTCM/D1/D2/SDRAM très chargées ;
- PLAY×8 uniforme ne tient pas en D2 sans déplacement ;
- mais ce résultat doit être reconsidéré avec le vrai contrat child×1 ;
- H747 ne fournit pas de RAM « en plus » au sens d’une copie indépendante.

---

# 34. Prochaine étape recommandée

La prochaine conversation ne doit pas repartir sur un nouveau gros audit général.

Elle doit :

1. relire ce dossier ;
2. vérifier les décisions utilisateur ;
3. corriger les conclusions héritées qui reposaient sur PLAY×8 uniforme ;
4. décider si un petit audit ciblé est encore nécessaire sur :
   - layout top-level×8 / child×1 ;
   - placement réel D2 sans déplacer inutilement `g_seq_project` ;
   - stack ;
5. produire ensuite une **synthèse d’architecture finale** ;
6. découper la migration en chantiers indépendants et clôturables ;
7. seulement ensuite ouvrir une conversation d’implémentation avec Codex niveau Sol.

---

# 35. Découpage de chantier envisagé — NON FIGÉ

Une séquence plausible, à valider après synthèse :

1. Fondations identité / topology / entity / rôle.
2. Encapsulation runtime / binding et suppression des mappings ambiguës.
3. Contrat CONTROL↔AUDIO monocore H743.
4. Adaptateur AUDIO générique moteurs / suppression des appels directs scheduler→moteurs.
5. Sortie progressive du scheduler musical / Note FX hors IRQ AUDIO.
6. Refonte PLAY 8 top-level / 1 child + UI/live-rec/edit/snapshot.
7. Formalisation GROUP bus/common/mute.
8. Nouveau format persistance canonique sans legacy.
9. Nettoyage code mort / anciennes autorités.
10. Plus tard seulement : portage physique H747 M4/M7 en conservant les mêmes contrats.

Ce découpage n’est pas encore le plan officiel. La synthèse finale doit définir les dépendances exactes pour éviter un big-bang inutile.

---

# 36. Règles de pilotage pour la nouvelle conversation

- Ne pas coder avant la synthèse finale des audits.
- Ne pas préserver les abstractions historiques uniquement parce qu’elles existent.
- Ne pas faire de refonte mémoire disproportionnée sans mesure.
- Low-Cost Release + Premium seulement pour les validations de ce chantier.
- H743 doit rester supporté.
- Dual-core doit être préparé par des contrats, pas par des `#ifdef H747` prématurés.
- Aucun push automatique.
- Commits locaux par passe une fois l’implémentation commencée.
- Favoriser les contrats simples et les autorités uniques.
- Gains CPU : privilégier structurel/pire cas, pas fast-path ultra spécifique.
- Si une décision produit n’est pas dans ce dossier, la signaler comme ouverte au lieu de l’inventer.

---

# 37. Résumé ultra-court

Le firmware actuel mélange CONTROL et AUDIO et plusieurs identités/autorités.

Cible :

```text
H743 aujourd’hui :
CONTROL → contrat explicite → AUDIO
tout sur M7

H747 demain :
M4 CONTROL → même contrat → M7 AUDIO
```

PLAY :

```text
top-level : cible 8
GROUP master : PLAY possible mais émission 0
GROUP child : mono, cible 1
```

Ne pas confondre :

```text
PLAY stocké
rôle émetteur
polyphonie moteur
voix physiques libres
```

Mémoire :

- D2 ~276 KiB / 288 KiB utilisée ;
- PLAY×8 uniforme : +20 KiB, ne tient pas ;
- top-level×8 + child×1 : projection ~+2,5 KiB seulement ;
- ne pas déplacer tout le séquenceur sans nécessité réelle ;
- H747 ne fournit pas une seconde RAM indépendante.

Étape suivante :

> synthèse architecture finale après éventuellement un dernier contrôle ciblé ×8/×1 + stack, puis découpage des chantiers et nouvelle conversation d’implémentation Codex Sol.
