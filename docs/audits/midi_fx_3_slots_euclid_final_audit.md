# Audit final — trois slots MIDI FX et EUCLID

Date de l'audit : 2026-08-05
Depot : BRICK6
Base : HEAD `15d22c4a5` (`fix: keep zero-valued ADSR attack voices alive`) et modifications presentes dans l'arbre de travail au moment de l'audit.

## 1. Verdict

`CHANTIER TERMINÉ SOUS RÉSERVE DE VALIDATIONS MATÉRIELLES`

La structure fonctionnelle controlee est conforme pour trois slots MIDI FX et EUCLID. Les validations dynamiques host, USB/MIDI, moteur reel et mesures H743 n'ont pas pu etre executees dans cet environnement.

## 2. HEAD et commits contrôlés

Commits directement pertinents controles :

- `1d1cdaa75` — reduction des autorites MIDI FX a trois slots ;
- `3c9002bc0` — UI, p-locks et persistance a trois slots ;
- `d171e1c46` — reconciliation du rollout trois slots ;
- `5f0e408d3` — fermeture des preuves residuelles de l'etape 1 ;
- `15d22c4a5` — HEAD courant.

Les modifications locales pertinentes au chantier ont ete auditees sans reinitialiser ni nettoyer les autres changements de l'arbre.

## 3. Résumé des étapes

| Etape du plan | Statut | Constat |
|---|---|---|
| 1 — prerequis residuels et admissions | PARTIELLE | Preuves statiques fermees ; execution dynamique des admissions reportee. |
| 2 — reduction des autorites a trois slots | CONFORME | Cardinalites, IDs et consommateurs actifs bornes a trois. |
| 3 — UI, formats, p-locks et clipboard | CONFORME | UI et formats courants a trois slots ; Undo/Redo reste hors capture MIDI FX. |
| 4 — modele EUCLID et defaults | CONFORME | Modele central, schemas et defaults implementes. |
| 5 — algorithme et masque borne | CONFORME | Module pur sans allocation ni division dans le chemin chaud ; tests compiles. |
| 6 — runtime strict-actif, phase et deadlines | PARTIELLE | Implementation controlee ; execution comportementale non disponible. |
| 7 — paires Note On/Off et continuation | PARTIELLE | Ledger owned, fermetures et chaine presents ; validation terminale materielle reportee. |
| 8 — transitions, modele et p-locks refuses | PARTIELLE | Politiques et refus centraux presents ; sequences longues non executees. |
| 9 — multi-EUCLID, saturation et budgets H743 | PARTIELLE | Bornes structurelles presentes ; p99, underrun et saturation reelle non mesures. |
| 10 — validation finale et documentation | PARTIELLE | Audit et validations statiques faits ; validations host/H743 restantes. |

Bilan des etapes : 4 CONFORME, 6 PARTIELLE, 0 NON CONFORME.

## 4. Trois slots MIDI FX

Le contrat actif est `NOTE_FX_SLOT_COUNT == 3`, avec slots indexes 0, 1 et 2. Les IDs de parametres, le stockage pattern/project, les snapshots, l'UI et les p-locks utilisent cette cardinalite.

Les recherches negatives sur le code fonctionnel ne trouvent pas de `S4`, `SLOT4`, `SLOT_4`, `PARAM_MIDI_FX_S4` ou `SEQ_PARAM_MIDI_FX_S4`. Les occurrences de ces chaines dans le script de validation sont intentionnelles et testent leur absence.

Le footer UI expose `SLOT 1`, `SLOT 2`, `SLOT 3`; le quatrième emplacement n'est pas un slot MIDI FX actif.

## 5. Absence du futur FX audio

Aucun FX audio n'a ete ajoute au pipeline MIDI FX. Il n'existe pas d'alias audio sur un ancien slot 4, ni de branche audio dans la chaine source → MIDI FX1 → MIDI FX2 → MIDI FX3 → terminal.

## 6. Modèle et paramètres EUCLID

Le modele central est `OFF`, `ARP`, `EUCLID`. EUCLID expose quatre parametres :

- `LENGTH` : 1..64 ;
- `PULSE` : 0..64, puis borne a `LENGTH` ;
- `DIV` : index de division 0..7 ;
- `MODEL` : selection du modele.

Le schema est fourni par `note_fx_state`, puis reutilise par l'edition UI et les chemins de parametres. Les bornes dynamiques permettent notamment `LENGTH=64` et `PULSE=0`; le descripteur generique historique ARP ne gouverne pas ces bornes actives.

## 7. Defaults et changements de modèle

Les defaults controles sont :

- OFF : `2, 0, 1, OFF` ;
- ARP : `2, 0, 1, ARP` ;
- EUCLID : `LENGTH=16`, `PULSE=4`, `DIV=2` (division `1/16`), `MODEL=EUCLID`.

Un changement de modele remplace le slot par les defaults du nouveau modele et normalise l'etat avant publication. Les transitions runtime ferment les occurrences owned avant le redemarrage. La restauration normale applique les defaults dependants du modele; la restauration exacte conserve les valeurs snapshot normalisees pour le rollback transactionnel.

## 8. Masque Euclid

`euclid_build_mask()` produit un masque `uint64_t` pour `LENGTH` 1..64 et `PULSE` 0..LENGTH. Le bit 0 est le premier pas, les bits au-dela de LENGTH sont nuls, et `PULSE=0` produit un masque nul.

Le calcul est deterministe, sans allocation et sans division dans le chemin runtime. Les vecteurs controles incluent notamment `3/2=0x5`, `8/3=0x49`, `8/4=0x55`, `8/5=0xB5`, `16/4=0x1111` et `64/64=UINT64_MAX`.

## 9. Sources strictement actives

Une source active est identifiee par son token, sa generation et sa provenance. La pitch seule n'est pas une identite. Les doublons de pitch sont donc distincts et peuvent etre fermes independamment.

La capacite Euclid par slot est fixe a 16 sources. Les sources inactives ne declenchent ni pulse ni nouvelle occurrence.

## 10. Phase et deadlines

Chaque slot possede sa phase, son prochain deadline, sa longueur, son pulse et sa division. Une premiere source initialise la phase a zero et le deadline a l'echantillon d'admission. La phase progresse modulo LENGTH; PULSE nul avance les deadlines sans publier de Note On.

Une reconfiguration EUCLID ferme d'abord les owned de ce slot, remet la phase a zero et reprend au prochain point autorise. Les divisions sont resolues via le catalogue partage; aucun calcul dynamique par allocation n'est necessaire.

## 11. Note On/Off et fermetures

Chaque Note On generee reserve et enregistre son owned avant publication, avec occurrence, token source, generation et deadline de fermeture. Si l'admission du Note On echoue, l'owned est libere sans Note Off artificiel.

Les Note Off dus, les fermetures de sources et les fermetures de reconfiguration sont rejoues avant les nouvelles generations. Un refus conserve l'owned et est retente. A un meme echantillon, l'ordre global controle d'abord les fermetures de tous les slots puis les generations.

Le test runtime Euclid a ete corrige pour respecter cette chronologie : lors de la reconfiguration du slot 0 a 610, le slot 1 ferme son ancienne occurrence mais ne repulse pas a la phase 1; il repulse a 620.

## 12. Chaîne des trois slots

Le pipeline impose `stage 0 → slot 1 → slot 2 → slot 3 → terminal`. La fanout maximale est statiquement egale a `NOTE_FX_SLOT_COUNT`, donc trois. Le dernier stage remet l'evenement au terminal commun avec le stage terminal explicite.

Les trois slots sont traverses par le meme chemin de continuation; aucun chemin special ne saute un slot ou ne reinjecte une occurrence au debut de la chaine.

## 13. Multi-Euclid

Les trois slots Euclid peuvent fonctionner simultanement. Leurs sources, phases, owned, generations et deadlines sont slot-locales. Les plafonds de construction sont fixes : 8 tracks × 3 slots × 16 sources/owned, avec fanout par slot plafonne a 16.

La saturation est refusee et comptee dans les diagnostics par slot; elle ne remplace pas une occurrence existante et ne cree pas de fermeture orpheline.

## 14. Mute et transitions

Le mute des triggers bloque les nouveaux triggers tout en preservant le runtime necessaire a la reprise. Les transitions destructives (stop, panic, changement de modele/pattern/destination/horloge) passent par le proprietaire pipeline, ferment les occurrences, purgent les sources et avancent la generation.

Les transitions sont idempotentes au niveau des ledgers et les evenements devenus obsoletes sont refuses par generation/provenance. La sequence exacte mute → unmute et les repetitions rapides restent a executer sur cible ou avec un runner host.

## 15. Terminal et admissions

Le terminal ne traite que les evenements au stage terminal. Il maintient un ledger de 64 occurrences par track et separe l'admission interne de l'admission MIDI par masque destination.

Les Note Off sont emis uniquement pour les destinations effectivement admises; les refus sont conserves pour retry. L'adaptateur interne actuel represente une admission de possession scheduler, pas encore un acquittement hardware independant. Les quatre combinaisons internes/MIDI, ainsi que les refus USB/UART repetes, n'ont pas ete executees dans cette passe.

## 16. UI, p-locks et Undo/Redo

L'UI MIDI FX expose trois sous-pages et des libelles dynamiques selon le modele. Les bornes d'edition EUCLID sont resolues depuis le schema courant; `PULSE` suit `LENGTH`.

Les p-locks utilisent 12 positions MIDI FX (3 slots × 4 parametres), et la frontiere de step refuse les parametres LENGTH/PULSE/DIV lorsqu'un MODEL effectif vaut EUCLID. Le refus est central et track-aware; la restauration d'un lock deja present utilise une voie explicite de restauration.

Le store Undo/Redo v2 reste sequence-only et ne capture pas les MIDI FX; cette absence est coherente avec le contrat documentaire courant et ne reintroduit aucun slot 4.

## 17. Persistance et clipboard

Pattern, project, live pattern, snapshot de track et clipboard utilisent la structure fixe de trois slots. La version de format courante est v7 pour project et pattern, avec le tableau MIDI FX de trois slots; aucun runtime owned/phase n'est persiste.

Le clipboard copie les donnees modeles et parametres via les chemins centraux. Les migrations et les tailles de structures ne contiennent pas de champ S4 fonctionnel.

## 18. Budgets et saturation

Les bornes compilees controlees sont :

- demi-buffer audio : 64 frames ;
- quota Note On genere : 8 par track et par demi-buffer ;
- reserve Note Off : 32 ;
- queue de commandes : 32 ;
- ledger terminal : 64 occurrences par track ;
- capacite owned/source Euclid : 16 par slot.

Le pipeline ne depend pas d'allocation dynamique dans le chemin audio. La reservation de fermeture a ete corrigee dans cette passe : le Note Off associe consomme la reservation deja chargee par le Note On, sans double-decrement ni bypass lorsque la reserve est vide.

Les compteurs de refus, hautes eaux, causes et generations existent. Leur comportement sous saturation reelle et leur cout temporel H743 ne sont pas mesures.

## 19. Builds, tests et mesures H743

Builds effectues avec succes, sans lancer `TestPremium` :

| Cible | FLASH | DTCMRAM | RAM_D1 | RAM_D2 | RAM_D3 |
|---|---:|---:|---:|---:|---:|
| Release / Low-Cost | 1,104,524 B (60.19%) | 104,064 B (79.39%) | 431,360 B (82.28%) | 102,144 B (34.64%) | 39,840 B (60.79%) |
| Premium | 1,091,960 B (59.51%) | 105,088 B (80.18%) | 484,576 B (92.43%) | 108,640 B (36.84%) | 39,840 B (60.79%) |

Les trois ELF de tests cross-compile ARM ont ete construits : `note_fx_state_restore_test`, `note_fx_euclid_mask_test` et `note_fx_euclid_runtime_test`. Les deux validations PowerShell statiques ont passe via CTest (2/2).

Les tests C dynamiques n'ont pas ete executes : la configuration host ne dispose pas de compilateur C, et aucun runner ARM/QEMU n'est disponible. Les mesures H743 DWT, p99, budget CPU, underrun, admissions USB/MIDI et comportements moteur reel restent donc a faire sur cible.

## 20. Code mort et recherches négatives

Les recherches sur `Inc`, `Src` et les tests confirment :

- aucun slot 4 fonctionnel ;
- aucune constante `PARAM_MIDI_FX_S4` ou `SEQ_PARAM_MIDI_FX_S4` ;
- aucune structure NoteFx a quatre slots ;
- aucune allocation dans le module EUCLID et aucun masque dynamique non borne ;
- aucun ancien chemin terminal specifique a quatre slots ;
- aucune presence d'un futur FX audio dans la chaine MIDI FX.

Les references historiques a quatre slots presentes dans les plans et journaux sont qualifiees comme historiques; elles ne correspondent pas a des autorites fonctionnelles courantes.

## 21. Documentation

Le plan `docs/plan_midi_fx_3_slots_euclid.md` contient le journal des etapes 1 a 10 et qualifie deja les validations host, USB/MIDI et H743 reportees. Le present audit ajoute la matrice finale de conformite, les tailles Release/Premium, la correction de budget de fermeture et les limites de preuve restantes.

## 22. Écarts restants

Il ne reste pas de deviation structurelle connue sur la cardinalite trois slots, le modele EUCLID, le masque, les owned, la chaine ou la persistance.

Les ecarts sont des validations non executees :

1. executer les trois tests C sur host ou runner ARM ;
2. exercer les admissions internes, USB, UART et les retries terminal ;
3. mesurer sur H743 le cout de la chaine, le p99, les hautes eaux et les underruns ;
4. valider mute/unmute, changements de modele rapides et saturation multi-Euclid sur cible.

Le descripteur generique historique conserve des libelles/bornes ARP, mais les consommateurs actifs UI, edition et p-lock passent par le schema model-aware; une harmonisation metadata complete pourra etre faite si un nouveau consommateur generique est introduit.

## 23. Verdict final

`CHANTIER TERMINÉ SOUS RÉSERVE DE VALIDATIONS MATÉRIELLES`

Corrections appliquees dans cette passe :

- correction de la comptabilisation de reserve Note Off dans `Src/NoteFx/note_fx_pipeline.c` ;
- correction des attentes de chronologie dans `tests/note_fx_euclid_runtime_test.c` ;
- ajout du present audit.

Aucun push n'a ete effectue.
