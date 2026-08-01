# ARCHITECTURE_GLOBAL.md

## Addendum 2026-08-01 - autorite MPU SDRAM

- Z0 configure les 32 MiB de SDRAM externe en memoire normale write-back/write-allocate, non-shareable et XN. Les linkers Low-Cost/Premium reservent les 31 premiers MiB aux sections cacheables.
- Une region MPU prioritaire de 1 MiB a `0xC1F00000` reste shareable, non cacheable, non bufferable et XN pour les rings Recorder et le preroll Looper. Le pool de pages Sampler/Looper/Wavetables demeure hors de cet overlay.
- Z1 conserve l'autorite hard-RT de lecture des pages; Z6 les remplit hors IRQ. Les protocoles de publication/recyclage et la compatibilite DMA restent explicites et ne sont pas remplaces par une maintenance cache globale.

## Addendum 2026-07-31 - Play MIDI et External

- Z2 distingue `MIDI/MIDI`, sans cible audio, de `External/External`, qui combine le chemin MIDI et une cible audio sur une entrée physique sélectionnée.
- `track_input_ownership` est l'autorité unique, statique et bornée de réservation des entrées. Une entrée appartient soit à sa Special Input fixe, soit à une seule Play External; Z1 ne la traite qu'une fois.
- Z5 affiche `USED Pn` sur la Special réservée et refuse tout conflit sans fallback. Z6 persiste la sélection dans snapshot Track et `PatternSaveV1`; Pattern/Project passent en version 3 et les restores/Undo valident avant mutation.

## Addendum 2026-07-31 - autorite centrale de topologie des tracks

- Z2 porte `track_topology` comme description unique de la cible produit : 8 Play Tracks, 4 Special Low-Cost ou 6 Special Premium, avec identites fixes Master/Looper/Input/FX.
- Les capacites notes, audio, MIDI, clavier, arpege, automation, mute et reservation d'entree sont explicites. Premium declare exactement trois entrees et aucune ressource Input4.
- Les formats Pattern/Project restent communs aux deux cibles mais identifient chaque enregistrement par role/ordinal; les sequences Play et Special y ont des payloads distincts.
- Les Special ont une identite immuable dans Z2 et leurs acces note/clavier/ARP ainsi que les navigateurs PATCH/KIT sont bloques par capacite. Master porte les effets globaux et FX porte les quatre MacroFX; Z2/Z3/Z5 discriminent directement leur role topologique.

## Addendum 2026-07-31 - ownership des Special audio

- Z2 expose Master uniquement avec `CFG/SEQ/TONE` et FX avec `CFG/SEQ/TONE/MOD`; les MacroFX ne sont applicables que sur la role FX fixe. Looper et chaque Input conservent leur adaptateur fixe et leur backend existant.
- Z5 limite `MIX` a `Level/Pan/Send1/Send2` pour les tracks disposant d'une lane mixer. La Special Master expose en `TONE` les surfaces globales reverb, delay et compresseur; la Special FX expose les quatre slots MacroFX et son contexte ROUT UI-only.
- Z1 conserve les autorites DSP globales du mixer et execute l'insert MacroFX post-mix depuis l'etat de la Special FX trouvee par `track_topology`; aucun nouvel etat sonore ni changement de chemin audio n'est introduit.

## Addendum 2026-07-31 - budget global des voix synthetiques

- Z2 porte l'autorite unique d'un budget borne de 16 slots reserves, commun LowCost/Premium, pour huit tracks moteur au maximum; un slot conserve un owner track et moteur explicite.
- Z1 adresse les moteurs, filtres et etats de note/allocation poly par slot publie par cette autorite, sans formule `track + voice * 8`; les pools et matrices historiques de 64 moteurs/etats et 56 filtres sont retires.
- Z5/Z6 appliquent le meme plafond aux changements structurels, au clipboard et aux restaurations; une destination reutilise d'abord ses propres reservations.

## Addendum 2026-07-31 - premier modele sonore MD `TRX-BD`

- Z1 publie `TRX-BD` dans le moteur unique MD avec un renderer natif specialise;
  les cinq autres modeles MD restent silencieux.
- L'etat DSP MD partage un stockage union avec `BD_ANALOG`, sans execution ni
  primitive Plaits dans le chemin MD.

## Addendum 2026-07-31 - primitives DSP communes `DRUM / MD`

- Z1 fournit des primitives MD autonomes et non publiees: phase Q32, sinus LUT
  native, enveloppe decay, bruit par voix, HPF/LPF, clipping, mix et fondu de
  retrigger.
- Elles ne sont encore reliees a aucun modele, ne produisent aucun son et
  n'introduisent aucune dependance Plaits, allocation ou acces SD.

## Addendum 2026-07-30 - surface dynamique `DRUM / MD`

- Z3 porte une autorite canonique par track `MODEL + P1..P8`, avec profils
  `TRX-BD/TRX-SD/TRX-CH/EFM-BD/EFM-SD/EFM-CB`; les slots utilisent neuf
  réserves neutres et IDs actifs canoniques contigus; `PARAM_COUNT` reste inchangé.
- Z4 applique toujours le p-lock `MODEL` avant les slots MD du meme step.
- Z5 resout labels et cardinalite depuis le profil courant; `MODEL` est
  p-lockable mais exclu des destinations de modulation.
- Z6 persiste cette surface dans les snapshots existants et invalide les
  fichiers prototypes anterieurs par bump de version.

## Addendum 2026-07-30 - integration du type `DRUM / MD`

- Z2 remplace l'ancien type reserve `TRX BD` par l'identite unique `DRUM / MD`
  sans changer son ordinal persiste ni creer une nouvelle autorite runtime.
- Z1 route ce type vers un modele MD explicitement silencieux et conserve
  `BD_ANALOG` et son rendu Plaits historique inchanges.
- La surface `MODEL + P1..P8` reste reservee a l'etape 2 du plan MD.

## Addendum 2026-07-30 - correction controles reverb et visualisation filtres

- Z1 corrige la courbe DAMP Deluge et conserve les phases LFO lors des mises a jour block-rate; Z5 preserve la sous-page lors d'un changement de famille dynamique Mutable/Digital.
- Z5 centralise aussi un widget de reponse HPF+LPF sur deux cellules pour les reverbs et delays, sans changer leurs deux autorites parametres.

## Addendum 2026-07-30 - second modele reverb Digital

- Z1 conserve une seule reverb SEND et un seul buffer de 32 768 samples, avec selection exclusive `MUTABLE/DIGITAL`; Digital porte fidelement la topologie Dattorro/Lexicon du Deluge transposee a 48 kHz.
- Z3 memorise les banques specifiques par modele sans changer `PARAM_COUNT`; Z5 resout dynamiquement deux ou trois pages reverb; Z6 persiste modele, banques et pan Digital.

## Addendum 2026-07-30 - reverb Mutable 48 kHz

- Z1 conserve l'unique reverb SEND Mutable/RevB, avec damping tank separe des filtres wet et bypass CPU reel du smear AP1.
- Z3 porte huit controles globaux persistants; Z5 les expose sur `REVERB 1/2`. Aucun autre effet ni autorite master n'est modifie.

## Addendum 2026-07-30 - prototype comparatif compresseur master

- Z1 porte un slot dynamics master unique, post-retours et post-XFade Looper, avec selection exclusive `OFF/DELUGE/BRICK`.
- Z3 conserve les parametres communs et caracteristiques sous une autorite globale; Z5 les expose sur la Special Master dans `TONE 3/3`; Z6 les capture dans le snapshot global Project/Pattern.

## Addendum 2026-07-30 - Hall low-cost raw et calibration utilisateur

- Z0 transmet desormais chaque mesure Hall low-cost valide directement a `hall_engine` a la cadence par touche de 2,8 ms; Premium conserve son ASC x4 et son chemin historique.
- Z5 reutilise les pages autoritatives `CALIBRATION` et `USER_CALIBRATION` depuis `Settings > Calibration`, et expose `KEYBOARD > VELOCITY` pour le profil, le mode par defaut et la courbe.
- Z6 conserve dans le blob flash Hall low-cost v2 le choix `DEFAULT/USER`, le mode `DV/TIME/ENERGY`, la courbe et le profil USER. Le format Hall Premium v1 reste inchange.

## Addendum 2026-07-30 - MT-12 replay deterministe

- Z0 peut reconstruire explicitement la sequence archivee depuis sa seed et la rejouer dans le meme snapshot jetable MT-05, via le seam d'entree normal MT-04 et avec la cadence logique originale.
- Le replay compare exactement l'action cible regeneree au breadcrumb archive (index, tick, delai, type, cible et valeur). Toute divergence arrete proprement la session avec `REPLAY MISMATCH`; aucune action n'est blacklistee.
- Le moteur se met en pause juste avant l'injection de l'action fautive. Z5 expose alors une commande physique explicite pour l'executer; en Debug avec sonde attachee, un breakpoint est place avant l'injection. Cette fonction reste exclue de Release/Premium et ne depend d'aucun module `audio_test_*`.

## Addendum 2026-07-30 - filtre musical et ordre track unifié

- Z3 sépare cutoff de base/p-lock lissé et cible Matrix/LFO en Hz; Z1
  interpole séparément base, modulation, enveloppe et keytrack par fenêtres
  de 8 samples avant de composer le cutoff du coeur TPT.
- Z1 impose le même ordre mono/stéréo
  `moteur -> filtre -> VCA/volume -> inserts track -> bus`. Les sends/returns
  delay et reverb restent sous l'autorité master du mixer.

## Addendum 2026-07-30 - AUDIO TEST 2

- Z0 orchestre le lifecycle diagnostic, Z1 substitue la source au dernier seam
  float avant PCM24, Z5 expose `Settings > Test > Audio 2` et Z6 sérialise les
  WAV/CSV INTERNAL. Tout le sous-système est exclu avec
  `BRICK_TEST_BUILD=0`.
- Les phases analogiques n'appellent aucun service SD. Le pipeline production
  reste inchangé lorsque le hook test est inactif.

## Addendum 2026-07-30 - MT-11 journal MONKEY TEST

- Z6 ajoute un writer MONKEY autonome, sans dependance envers `audio_test_csv`, cadence par Z0 hors IRQ. Le client generique `SD_ACCESS_CLIENT_DIAGNOSTIC_LOG` est le seul acces FatFs du journal et reste autorise par la politique read-only Monkey uniquement dans le dossier reserve aux diagnostics.
- Le dernier crash archive est ecrit et synchronise avant d'etre marque `REPORTED` en Backup SRAM. Une absence SD, une contention ou une erreur FatFs conserve l'archive et provoque une nouvelle tentative bornee; un identifiant stable et le marqueur `END` evitent de dupliquer un rapport deja durable apres un reset au mauvais instant.
- Le journal ajoute aussi `START`, un resume toutes les dix minutes et `STOP`, jamais une ligne par action. `MONKEY.LOG` est borne a 256 KiB et tourne vers un unique `MONKEY.OLD`, soit environ 512 KiB maximum.

## Addendum 2026-07-30 - MT-10 reprise apres reset

- Les builds diagnostic capturent `RCC->RSR` au tout debut de `main()`, avant l'initialisation HAL et peripherique, puis effacent les flags une seule fois. Une capsule `FAULTED` conserve son type de fault; une session `RUNNING` avec `IWDG1RSTF` est classee watchdog. Les autres resets ferment la session interrompue sans faux crash.
- Le dernier crash est copie dans une banque d'archive Backup SRAM double-slot distincte de la session courante. Seed, index, breadcrumbs, registres et flags RCC restent donc disponibles pour le rapport et le replay explicite pendant qu'une nouvelle session s'execute.
- Apres la fin du boot applicatif, MONKEY TEST reprend automatiquement avec une nouvelle seed derivee, sans blacklist d'action. L'archive fautive reste accessible; aucune ecriture SD n'est realisee dans cette etape.

## Addendum 2026-07-30 - MT-09 watchdog de diagnostic

- `IWDG1` est arme uniquement au premier demarrage de MONKEY TEST dans les builds `Debug` et `Test`, avec un delai nominal de 12 s. Il reste arme jusqu'au reset, y compris apres un arret manuel du test.
- Le seul heartbeat est place en fin de boucle principale, apres les services application, USB/MIDI, UI, rendu et flush display, et exige aussi une progression de `engine_tick_count` issue de la cadence audio. Aucun IRQ, DMA, tasklet partiel ni handler de fault ne nourrit l'IWDG.
- `Debug` fige l'IWDG lors d'un halt debugger; `Test` conserve le comportement cible. La capsule persistante enregistre l'armement et un checkpoint de heartbeat borne a environ 1 Hz.
- Le reset explicite de MT-08 reste l'autorite primaire apres fault; l'IWDG n'est qu'un filet de secours.

## Addendum 2026-07-30 - MT-08 capture des faults

- Les quatre faults Cortex-M7 selectionnent MSP/PSP depuis `EXC_RETURN`, basculent sur une pile DTCM dediee, finalisent la capsule MT-07 sans FatFs ni affichage, puis demandent toujours un reset systeme explicite.
- L'IWDG n'est pas requis par ce chemin et restera seulement un filet de secours MT-09. Dans les builds normaux, les handlers ne portent pas la capsule mais remplacent aussi les attentes infinies par un reset explicite.

## Addendum 2026-07-30 - MT-07 capsule persistante

- Z0 reserve initialement 1 KiB de Backup SRAM a une capsule Monkey double-slot, versionnee et protegee par CRC32. MT-10 ajoute une seconde banque double-slot de 1 KiB pour archiver le dernier crash sans qu'une nouvelle session l'ecrase. Chaque action est inscrite avant injection dans un ring compact de 16 breadcrumbs; le commit alterne les slots et publie son marqueur valide en dernier.
- La zone `0x38800000..0x38800FFF` est NOLOAD et non cachee par MPU dans `Debug`/`Test`. La capsule et son code sont absents de `Release`/`Premium`; les linkers ne font que nommer la ressource physique libre.

## Addendum 2026-07-30 - MT-06 supervision MONKEY TEST

- Z0 echantillonne a 10 Hz les compteurs generiques de charge IRQ, underruns Sampler/Looper et les invariants UI; Z1 reste l'autorite des compteurs audio. Les anomalies recuperables sont classees sans arreter le flux, tandis qu'un invariant UI ou une sentinelle corrompue provoque un arret controle et la restauration MT-05.
- Cette supervision est compilee uniquement dans `Debug`/`Test`, reste hors IRQ et ne depend d'aucun module `audio_test_*`.

## Addendum 2026-07-30 - MT-05 isolation MONKEY TEST

- Z0 possede le lifecycle de session jetable et son snapshot SDRAM; Z6 centralise le refus des clients SD mutateurs; Z5 neutralise les actions Project/Sample non couvertes par FatFs. Ces seams sont conditionnes par `BRICK_TEST_BUILD` et restent independants de `audio_test_*`.

## Addendum 2026-07-30 - calibration perceptuelle AUDIO TEST

- Z0 orchestre les phases et la synthese hors IRQ, Z1 observe par K-weighting
  sans toucher au mix, Z6 ecrit `CAL_RAW`/`CAL_SUMMARY`.
- Aucun gain moteur n'est modifie; le CSV porte seulement une recommandation
  bornee pour validation humaine.

## Addendum 2026-07-30 - MT-04 injection MONKEY TEST

- Z0 distribue les actions deterministes via `diagnostic_input` vers les autorites existantes Z5: file d'evenements boutons, accumulateurs encodeurs et runtime clavier. La primitive est generique, compilee uniquement dans `Debug`/`Test`, et ne depend d'aucun module `audio_test_*`.

## 1. Rôle

Ce document est une carte d’orientation.

Il ne détaille pas l’architecture locale.
Il sert uniquement à :
- identifier la bonne zone
- comprendre les dépendances principales entre zones
- savoir quels documents lire avant de modifier le code

Lecture cible du projet :
- état canonique / contrôle
- projection runtime track-aware
- exécution bornée

La trajectoire d’architecture vise des seams explicites et une préparation propre au futur split dual-core, sans créer de bus central ou d’IPC prématuré.

Le détail réel vit dans :
- `docs/architecture/z0_plateforme_cadence.md`
- `docs/architecture/z1_audio_hard_rt_mix.md`
- `docs/architecture/z2_track_runtime_authority.md`
- `docs/architecture/z3_param_modulation_control.md`
- `docs/architecture/z4_seq_clock_scheduler.md`
- `docs/architecture/z5_ui_navigation_interaction.md`
- `docs/architecture/z6_state_persistence_patterns_projects.md`

---

## 2. Zones d’architecture

## Z0 — Plateforme / Cadence
Lit ce document si le sujet touche :
- boot
- init système
- ordre d’initialisation
- superloop
- tasklets / cadence hors audio IRQ

Doc :
- `docs/architecture/z0_plateforme_cadence.md`

## Z1 — Audio Hard-RT / Mix
Lit ce document si le sujet touche :
- DMA / IRQ audio
- blocs audio
- conversion I/O audio
- callback DSP
- mixage
- taps recorder Looper dans le pipeline audio
- playback Looper via pages RAM pretes dans le pipeline mixer
- runtimes synth mono externes Prism / Stack / Wave / DELUGE et leurs imports DSP
- metronome MAIN monitor-only post-capture/post-MacroFX

Doc :
- `docs/architecture/z1_audio_hard_rt_mix.md`

## Z2 — Track Runtime Authority
Lit ce document si le sujet touche :
- track_state autoritatif
- family / type effectifs
- binding runtime
- mix target runtime
- capacités runtime
- statut effectif d’un domaine param
- logique track-aware centrale

Doc :
- `docs/architecture/z2_track_runtime_authority.md`

## Z3 — Param / Modulation / Control
Lit ce document si le sujet touche :
- écriture param
- clamp / dispatch / apply
- staging / commit
- modulation LFO
- coexistence global / track-aware / legacy
- modèle paramétrique par track
- base commune `track_sound_state` + base TONE `track_tone_sound_state` (Sampler + Wave + Stack + DELUGE + MIDI simple + DRUM / MD silencieux + BD Analog)

Doc :
- `docs/architecture/z3_param_modulation_control.md`

## Z4 — Seq / Clock / Scheduler
Lit ce document si le sujet touche :
- transport
- tempo / clock
- progression musicale
- boundaries
- scheduling sample-accurate
- evenements audio metronome sample-accurate
- live-rec lié au transport

Doc :
- `docs/architecture/z4_seq_clock_scheduler.md`

## Z5 — UI / Navigation / Interaction
Lit ce document si le sujet touche :
- état UI
- navigation
- hall modes
- track select
- résolution contextuelle des pages
- raccourcis
- clipboard UI

Doc :
- `docs/architecture/z5_ui_navigation_interaction.md`

## Z6 — State / Persistence / Patterns / Projects
Lit ce document si le sujet touche :
- snapshot live
- queue/apply pattern
- save/load pattern
- save/load project
- boot context
- restore global d’état
- writer Looper, reservoirs RAW systeme, export SAVE RAW -> WAV durable, paths finaux

Doc :
- `docs/architecture/z6_state_persistence_patterns_projects.md`

---

## 3. Dépendances principales entre zones

- Z0 initialise et cadence tout le reste
- Z1 consomme surtout Z2, Z3 et Z4
- En clock interne/externe sequencer, Z1 fournit la consommation finale d'avance step de Z4 (domaine audio bloc)
- Z2 fournit la vérité runtime aux autres zones
- Z2 fournit aussi la topologie produit et ses capacites; les autres zones ne doivent pas reconstruire les roles depuis des index magiques
- Z3 applique des valeurs en s’appuyant sur Z2
- En PLAY+REC actif, les edits param track-aware sont redirigés vers Z4 (écriture p-lock live), sans write runtime direct Z3 en parallèle
- Z4 produit les événements temporels consommés par Z1
- Z4 et Z5 peuvent notifier Z3 des note/trig runtime via le seam explicite LFO, sans devenir autorité de modulation
- Z5 pilote Z2, Z3, Z4 et Z6 via l’interaction utilisateur
- Z6 capture/restaure de l’état qui réimpacte Z2, Z3, Z4 et Z5

Règle de lecture transversale :
- Z2 porte la projection runtime track-aware
- Z3 et Z4 consomment cette projection sans recréer d’autorité locale parallèle
- Z5 expose des choix utilisateur, mais ne doit pas devenir une seconde autorité de structure
- Z1 et Z0 restent des zones d’exécution / orchestration bornée, pas des lieux de décision métier

---

## 4. Comment choisir quoi lire

### Si le prompt parle de :
- **boot / ordre d’init / boucle principale** ? Z0
- **IRQ audio / mix / buffer audio / DMA** ? Z1
- **family / type / mix target / runtime bind** ? Z2
- **paramètres / LFO / apply / staging** ? Z3 (hors redirection live-rec PLAY+REC)
- **transport / tempo / scheduler / live rec séquenceur** ? Z4
- **UI / halls / navigation / pages / clipboard** ? Z5
- **save/load / patterns / projects / restore** ? Z6

### Cas transverses fréquents
- **Input Special vs External Play** ? Z2 + Z3 + Z5
- **Special FX MacroFX** ? Z1 + Z2 + Z3 + Z5
- **bug track-aware transversal** ? commencer par Z2
- **bug après load/restore** ? Z6 puis Z2/Z3/Z4/Z5 selon symptôme

### Philosophie d’architecture attendue
- track-aware avant global
- ownership clair avant confort d’implémentation
- canonical state avant projection
- projection avant exécution
- seams explicites avant abstractions larges
- future dual-core par séparation réelle, pas par surcouche de transport
- pas de nœud central ambigu
- pas d’infra prématurée

---

## 5. Annexes utiles (non canoniques)

Regle de navigation: un seul document canonique par zone Z0..Z6 (listee en sections 1 et 2 ci-dessus).

Annexes utiles conservees:
- docs/architecture/annexe_z3_param_authority_matrix.md: matrice d'autorite d'ecriture param (detail Z3).
- docs/architecture/annexe_z5_mode_transition_contracts.md: details de transitions de modes UI (detail Z5).
- docs/architecture/annexe_z5_ui_core_tick_order_contracts.md: contrat d'ordre ui_core_tick et priorites de consommation (detail Z5).
- docs/architecture/annexe_z5_ui_restore_track_config_bulk_flow_2026-04-14.md: flow detaille de restore bulk track config (detail Z5).
- docs/architecture/annexe_z5_ui_system_sync_boundary_2026-04-15.md: frontiere et extraction du noyau de sync systeme UI (detail Z5).

## 6. Historique / non autoritatif

Documents conserves pour tracabilite uniquement:
- docs/architecture/historique_z3_param_write_map_audit_2026-04-14.md (passe d'audit ciblee).
- docs/architecture/historique_z4_quant_swing_runtime_contract_2026-04-14.md (note de chantier pre-consolidation).
- docs/architecture/historique_z5_ui_orchestration_cartographie_2026-04-14.md (cartographie de passe initiale).

## 7. Addendum 2026-05-13


- Z2 conserve `TRACK_RUNTIME_ENGINE_WAVE` comme autorité unique avec son renderer float natif.

## Addendum 2026-07-29 - seam diagnostic AUDIO TEST

- Z0 cadence le runner automatique hors IRQ; Z5 n'affiche que sa progression et expose `STOP`.
- Z1 conserve uniquement taps/accumulateurs bornés; Z2/Z3 configurent les cas par leurs autorités existantes; Z6 capture/restaure le snapshot RAM et sérialise le CSV après la mesure.
- Banc inactif, les taps restent derrière le test scalaire d'activation existant et n'ajoutent aucun parcours diagnostic par sample.
- Les cas FX longs séparent l'autorité de mesure Z1 (`FX_ACTIVE` puis queue sans reset DSP) de l'autorité d'écriture Z6: les deux lignes CSV sont écrites seulement après les 3 s de tail.

## Addendum 2026-07-30 - firmware Test et decisions stables MONKEY TEST

- Le preset `Test` est un build `Release` low-cost avec `BRICK_TEST_BUILD=1`; `Debug` active egalement cette frontiere tout en conservant `-Og -g3`. `Release` et `Premium` forcent `BRICK_TEST_BUILD=0`: sources, pages, chaines, buffers et hooks couteux propres aux diagnostics en sont exclus.
- `MONKEY TEST` reste independant des modules `audio_test_*`. Il peut reutiliser `sd_access_gate`, FatFs et une primitive de journalisation generique extraite si necessaire, sans dependance directe envers `audio_test_csv`.
- Apres crash, la capsule conserve la seed et l'index fautifs pour replay explicite; aucune action n'est blacklistee et la reprise automatique demarre une nouvelle seed.
- MT-08 doit garantir un reset systeme explicite apres commit de la capsule, que l'IWDG soit actif ou non; l'IWDG reste un filet de secours et aucun handler n'attend indefiniment son expiration. La numerotation MT-01 a MT-12 reste inchangee.

## Addendum 2026-07-30 - MT-02 seam MONKEY TEST

- Z0 porte un lifecycle autonome `monkey_test` compile uniquement dans `Debug` et `Test`; Z5 expose `Settings > Test > Monkey` et ne possede pas l'etat runtime.
- Aucun moteur d'actions, injection d'input, acces SD, replay, watchdog ou handler de fault n'est introduit par MT-02.

## Addendum 2026-07-30 - MT-03 flux logique MONKEY TEST

- Z0 possede le PRNG, la seed, l'index et l'horloge logique 1500 Hz du flux d'actions. Les gestes press/release composes sont bornes par une file statique de quatre elements et le rattrapage superloop par huit actions.
- Z5 affiche seed, compteur et dernier type sans devenir une autorite. L'injection dans les chemins d'input reels reste hors MT-03.

## Addendum 2026-07-29 - cache WAVE multibande autoritaire

- Z6 publie atomiquement un unique `B6WT` v2 contenant le répertoire et les payloads 2048→8; Z1 sélectionne une bande hors boucle sample.

## Addendum 2026-07-29 - remplacement Synth/Daisy par Synth/DELUGE

- Z1 remplace le moteur de test DaisySP par un port GPL-3.0 des oscillateurs basic-wave Deluge, scalarise pour Cortex-M7, a phase/tables fixed-point et comportement fixe non degrade.
- La reference figee du port est le commit upstream `0d9cbf0440f0555e2544cc1eb019b31675637008` de `SynthstromAudible/DelugeFirmware`.
- Z2 conserve le point d'ownership mono par track sous les nouvelles identites DELUGE; Z3 porte sept params dedies, Z5 deux pages TONE, Z6 leur snapshot sans migration prototype.
- L'ancien runtime/math/oscillateur Daisy et les sources `Drivers/Daisy_SP/Source/Synthesis` exclusives sont retires; les autres modules DaisySP encore utilises restent hors de cette suppression ciblee.

## Addendum 2026-07-26 - MOD operators Matrix

- Z3 étend l'autorité Matrix avec des opérateurs control-rate `MULTI/SLEW` et des destinations `LFO rate`; Z5 expose cette surface en `MOD 2/2`.
- Les opérateurs restent dans le flux modulation existant, sans nouveau backend audio ni calcul par sample.

- Le buffer master dedie est retire des zones Z1/Z2/Z3/Z5/Z6.
- `audio_xfade` reste rattache au Looper, sans backend buffer master.
- Le recorder legacy dormant `live_recorder` / `recorder_transport` est retire de Z0/Z1; le record produit conserve uniquement Looper RAW + `multi_record_writer`.

## Addendum 2026-05-27 - Patch V1

- Patch V1 ajoute un seam Z5/Z6: UI Patch Assign + persistence de snapshot canonique d'une track sous slots fichiers separes de Project/Pattern.
- Kit reste l'extension future multi-track; Set est retire du contrat produit.

## Addendum 2026-05-29 - Kit V1 etape 3

- Kit V1 etend le seam Z5/Z6 avec `PAGE2 APPLY`: le Kit charge un payload complet, pre-valide les assets Sampler deja presents, neutralise les notes/voix, applique la structure tracks en bloc via les autorites track_state/runtime, puis restaure sound/tone/LFO et reprojette les params track-aware.
- Le contrat reste sans apply partiel, target mask, preview, rollback, Set, sequence, pattern, p-lock, playhead ni transport.


- Set reste supprime; Kit reste le snapshot sonore complet machine.


## Addendum 2026-05-29 - Kit V1 etape 2


- Kit V1 ajoute un seam Z5/Z6 distinct de Patch: storage/capture/save/browser/rename/delete pour snapshot sonore complet machine, sans apply dans cette etape.
- HALL1 devient l'entrée workflow Kit en overlay: single tap ouvre le browser après fenêtre double tap, double tap sauvegarde directement; aucun hall mode Kit persistant n'est ajouté.
## Addendum 2026-05-29 - Kit lie au Pattern

- Z6 etend le seam Pattern/Kit: un Pattern porte une reference de slot Kit, tandis que la banque Kit `B6KT` reste l'autorite durable du contenu sonore complet.
- Z5 expose ce lien dans le header principal (`Kit: nom`, dirty `*`, Pattern dessous) et le browser Kit lie le slot applique au pattern actif.
- Set reste retire; Kit reste full-machine sonore, Patch reste separe.


- Z3 applique SPREAD via le pan MIX existant et intercepte LINK au point unique d'edition manuelle UI; `PLAY`, p-locks et scheduler restent exclus.


- Autorite cible: `track_state` en Z2, avec lecture master-effective exposee aux consumers. Z4 peut consulter cette projection pour les p-locks non-PLAY, mais le pipeline PLAY conserve son adressage historique par track cible.

## Addendum 2026-07-28 - correction pipeline PLAY master group

- La route commune Z4 peut rester consommee pour la liste bornee des cibles, mais la provenance des p-locks PLAY reste locale a la track cible.

## Addendum 2026-07-28 - Multi Spread Keytrack

- Z1 applique le keytrack uniquement dans le rendu `Sampler/Multi`; `KEYTRK=OFF` garde le spread historique par pan MIX.

## Addendum 2026-07-29 - coeur filtre track

- Z1 porte maintenant les modes track `LP/HP/BP` sur un SVF TPT/ZDF float stereo/mono, plage effective `20 Hz..16 kHz`, jeux de coefficients coherents tenus par chunks de 8 et bypass OFF reel.
- Z3 conserve les autorites parametre/modulation existantes et mappe la resonance historique vers le Q TPT; le DJ EQ prepare ses coefficients couteux au boot.

## Addendum 2026-07-29 - contrat p-lock et Matrix

- Z4 declare a Z3 la valeur p-lock non-PLAY comme base Matrix temporaire; Z3 additionne les slots Matrix sur cette base et restaure la base canonique au release.
- Z1 consomme les endpoints continus moteur et filtre avec les rampes locales adaptees au DSP; Z3 conserve les bases, mappings et exclusions des parametres discrets.
## Addendum 2026-07-30 - Track paste sans notes fantômes

- Z5/Z6 encadrent l'apply du snapshot Track par un seam Z4 de restauration ciblée.
  les événements scheduler périmés, purge notes/gates/lookahead aux deux frontières,
  puis reprend sans arrêter le transport ni les autres tracks.

- Correction cause racine: Z4 conserve le playhead, le boundary courant et la phase
  DIV pendant l'apply; Z1 ne migre plus de gate actif lors du rebind mono-track;
  Z2/Z3 ne réappliquent que les domaines persistants capturés; les états ARP et les
  commandes note moteur en attente sont annulés uniquement pour la fermeture
# Addendum 2026-07-31 - ownership des voix polyphoniques

- Z2 porte l'origine `MANUAL/SEQUENCER` dans l'autorite d'allocation des voix;
  Z4 libere au STOP uniquement les voix du sequenceur.
- Les note-off de transition ciblent chaque instance et laissent Z1 terminer
  les releases; aucun panic global ne coupe les notes manuelles encore tenues.
- Z1 porte obligatoirement une enveloppe VCA par lane polyphonique; son passage
  a `IDLE` acquitte la fin de release a Z2, sans dependre du VCA mono de track.
# Addendum 2026-07-31 - Looper unique LowCost

- Z2 impose sur LowCost une ressource Looper globale unique, refuse une seconde configuration et bind l'unique instance `0`; Z6 normalise les anciens contenus en conservant le premier Looper puis en convertissant les suivants en `Sampler/RAM`.
- Z1/Z6 dimensionnent LowCost a un slot Shifter et un slot RAW. Premium reste inchange.
# Addendum 2026-07-31 - autorite mute centrale

- `track_mute` est l'autorite comportementale unique du mute par track et derive sa politique des capacites runtime.
- Z2 classe la cible; Z4 suspend et purge les notes; Z1 realise les fades audio et la contribution FX; Z5 ne porte plus de politique audio.
- Master n'expose pas de mute ordinaire. Les evenements manques pendant mute ne sont jamais rejoues au demute.

# Addendum 2026-07-31 - modeles sequence Play et Special

- Z4 conserve huit modeles Play complets `64 steps / 32 locks par step / pool 1024` et alloue aux Special un pool distinct `64 / 16 / 512`.
- Les Special refusent le set PLAY, ne portent ni notes ni roll, et disposent d'un champ action extensible. Aucun Brain ni MIDI FX n'est introduit.
- L'etat et la configuration ARP ne sont alloues et persistes que pour les huit Play Tracks. Z6 stocke les actions et automatisations Special dans un payload leger sans donnee PLAY.
- Les tokens scheduler, compteurs de notes exactes et overlays PLAY ne sont plus alloues aux Special; leur runtime ne conserve que les 16 locks d'automatisation actifs possibles.

# Addendum 2026-07-31 - persistence Play/Special

- Z2 fournit l'identite persistante `role + ordinal`; Pattern, snapshots Track et Kit la valident avant mutation, tandis que Patch reste strictement Play.
- Z6 conserve directement les formats courants v3 communs Low-Cost/Premium, sans migration legacy: huit payloads sequence Play et six emplacements Special legers.
- Les actions Special traversent Pattern/Project, snapshot, clipboard et Undo/Redo. Les collages incompatibles sont refuses avant mutation et aucun etat note/ARP n'est stocke sur Special.

- Les anciens champs et APIs restent des adaptateurs de compilation jusqu'aux etapes de migration et de suppression definitives.
# Addendum 2026-07-31 - execution musicale mono-track

- Z4 schedule PLAY/ARP et live record uniquement sur la Play Track source; Z5 route clavier, MIDI et mute directement par track.
- Les piles de notes, compteurs d'occupation et distributions tournantes inter-tracks sont retires; Stop/Panic conserve sa fermeture exhaustive par track.


# Addendum 2026-07-31 - edition sequence mono-track

- Z6 retirera le champ historique persistant a l'etape de persistence suivante; il est deja sans effet runtime.

# Addendum 2026-07-31 - persistence mono-track

- Patch est desormais un contenu mono-track unique, applicable independamment aux pistes selectionnees par la UI.

# Addendum 2026-07-31 - Play Tracks independantes

- Z2 ne porte plus aucune autorite de regroupement entre Play Tracks. Chaque identite logique possede directement son runtime, ses quatre voix PLAY et ses parametres moteur.
- Z3 a Z6 operent strictement par track pour controle, sequence, UI et persistence. Patch reste mono-track; la Special Track Master et le master audio sont inchanges.
# Addendum 2026-07-31 - seam UI MIDI FX

- Z5 retire le mode/vue ARP et transforme son raccourci en ouverture directe de la page temporaire `MIDI FX`, sans mutation du mode musical `SEQ/KEYBOARD` et sans effet sonore a l'ouverture.
- La projection `ROUT` des Special necessaires est conservee sur cette meme page. Z4 garde provisoirement l'ancien moteur ARP historique jusqu'au branchement du nouveau pipeline MIDI FX.
# Addendum 2026-07-31 - MIDI FX etape 2

- Z2 publie une capacite, un ensemble et un domaine MIDI FX reserves aux huit Play Tracks note-capable. Z3 porte l'autorite fixe `track x 4 slots`; Z5 projette les quatre pages et leurs labels dynamiques.
- Z6 etend seulement les snapshots RAM Undo. Pattern, Project, track snapshot, Patch et Kit restent hors MIDI FX jusqu'a l'etape persistence dediee.
# Addendum 2026-08-01 - moteur MIDI FX isole

Le moteur generique `Src/NoteFx/note_fx_engine.c` et le modele ARP neuf separe utilisent exclusivement le sample-domain. A l'etape 3 ils sont compiles mais pas encore inseres dans le flux live/sequenceur, inchange jusqu'a l'etape 4.
# Addendum 2026-08-01 - MIDI FX operationnel

Z4 unifie clavier, MIDI entrant et sequenceur avant les quatre slots MIDI FX, puis reutilise le dispatcher terminal moteur/MIDI existant. Z1 honore les echeances du moteur dans son decoupage sample-domain. Le live record reste en amont et ne capture jamais les notes generees.
# Addendum 2026-08-01 - lifecycle MIDI FX

Z2/Z4 centralisent cleanup et suspension MIDI FX sur Stop/Panic, mute, Pattern et mutations structurelles; Z3 nettoie avant changement effectif de MODEL. Les sorties conservent leur destination d'origine jusqu'au Note Off.
# Addendum 2026-08-01 - p-locks MIDI FX operationnels

Z3 porte un overlay runtime MIDI FX separe des bases, Z4 l'applique avant les sources de note au boundary, Z5 reutilise l'edition/live-record/Undo generiques et Z6 conserve les locks dans le pool sequenceur existant.
# Addendum 2026-08-01 - persistence MIDI FX

Z6 persiste les bases MIDI FX dans Pattern/Project et Track snapshot/Clipboard/Undo, sans etat runtime et sans inclusion Patch/Kit. Les overlays p-lock sont reinitialises avant toute restauration de bases.
