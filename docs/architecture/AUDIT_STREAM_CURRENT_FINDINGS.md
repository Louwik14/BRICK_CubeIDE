# Audit final Stream V3 — findings courants

## VERDICT

**NEW FINDINGS**

Audit statique de l’état courant uniquement. Aucun patch code et aucun build.

## NEW FINDINGS

### F-STREAM-01 — préremplissage Classic bloqué hors candidats RR

Le chemin Classic PARTIAL réserve les pages de base comme pages statiques
(`Src/Sampler/sample_cache.c:222-305`). Le chemin FULL réserve toutes les pages
et passe en `PREFILLING` (`Src/Sampler/sample_cache.c:100-140`).

Le collecteur du scheduler Stream ne crée des candidats qu’à partir des leases
publiées (`Src/Sampler/sample_stream_manager.c:111-220`). Une page `RESERVED`
sans lease active n’est donc jamais soumise au transport. Le service FULL ne
fait ensuite qu’observer les états et attendre `READY`
(`Src/Sampler/sample_cache.c:143-185`); la réservation détectée par
`sample_cache_has_pending_sd_work()` ne crée aucun candidat.

Conséquence : le cold-start Classic PARTIAL ne peut pas rendre sa base
playable, et le Classic FULL ne peut pas terminer son préremplissage. Le
chemin direct de prefill ayant été retiré, aucun chemin RR unique ne le
remplace.

Classification : mécanisme runtime requis, intégré au scheduler Stream/RR.
Ce n’est ni un bypass de prefill ni une nouvelle FSM.

### F-STREAM-02 — un reader actif continue après invalidation de son admission

`sample_voice_reader_publish_lease()` efface le lease puis retourne lorsque le
token n’est plus valide, mais ne désactive pas le reader et ne libère pas son
curseur (`Src/Sampler/VoiceReader/sample_voice_reader_cursor.inc:52-75`).
`sample_voice_reader_acquire_audio_page()` continue ensuite la résolution de
page après cette publication (`.../sample_voice_reader_cursor.inc:246-271`).

Une release CONTROL peut donc placer le token en `RETIRE_REQUESTED` entre deux
blocs AUDIO : le reader reste `active` et peut continuer à consommer sa page
courante, voire à résoudre la suivante, sans token/lease valide. Le même
pattern existe côté Looper : `looper_publish_lease()` efface le lease sur
admission invalide mais ne stoppe pas le rendu
(`Src/Audio/brick6_looper_runtime.c:572-615`).

Classification : mécanisme runtime requis. L’invalidation doit faire cesser la
consommation et libérer le curseur; ce n’est pas un simple assert, car la
concurrence CONTROL/AUDIO est réelle.

### F-STREAM-03 — association physique non atomique avec release/réutilisation

`sample_stream_admission_audio_associate_role()` valide le token et le slot,
puis écrit `physical_slot`, `physical_class` et `physical_present`
(`Inc/Sampler/sample_stream_admission.h:240-268`). CONTROL peut, entre ces
lectures et ces écritures, passer le crédit en `RETIRE_REQUESTED`, le libérer,
puis réallouer le même emplacement avec une nouvelle génération
(`Src/Track/sample_stream_admission.c:113-148`).

L’écriture AUDIO issue de l’ancien token peut alors marquer le crédit libéré
ou réalloué comme physiquement présent. Cela permet une utilisation stale et
peut bloquer ou fausser la réassociation du slot physique avant release
complète.

Classification : mécanisme runtime de claim/revalidation ou sérialisation
inter-core requis. Une assertion seule ne protège pas cet entrelacement réel.

## INVARIANTS VS RUNTIME

Les contrôles suivants sont cohérents dans les chemins de production audités :

- ledger global de 8 crédits ; Classic et Multi réservent 1 crédit ; Looper
  réserve 2 crédits et conserve l’admission de `REC` à `READY` puis
  `PLAY_AUTO` ;
- bind Classic/Multi et association Looper passent par un token actif ; les
  leases valident propriétaire, rôle, classe, slot et génération ;
- les completions page revalident token, slot, génération de page, epoch et
  état `LOADING` ; aucune completion stale n’est publiée ;
- le scheduler Stream applique un RR strict et marque chaque slot servi : au
  plus une page par slot/reader et par round ; aucun EDF ni choix par
  pitch/playhead/deadline/low-water ;
- le bulk Multi reste une continuation `STORAGE_OWNER_MULTI`, distincte du
  Stream runtime, tout en partageant le transport et l’unique scheduler SD ;
- la release physique utilise lease vide stable, `physical_present` et
  génération avant de libérer un groupe.

Les cardinalités exactes Classic/Multi (=1), Looper (=2), la correspondance
primary/auxiliary et l’unicité d’association sont des invariants/assertions
des API d’admission. Les appels de production audités respectent ces
cardinalités ; leur généralisation dans l’API ne constitue pas une nouvelle
FSM.

## AUDIT CIBLÉ

- **Reader actif sans token :** trouvé — F-STREAM-02.
- **Token stale encore utilisé :** F-STREAM-02 et F-STREAM-03.
- **Slot réutilisé avant release physique :** le protocole nominal le bloque ;
  F-STREAM-03 expose la fenêtre inter-core qui le compromet.
- **Reader servi plus d’une fois par round :** non trouvé ; le masque des
  slots servis et la fin de round sont effectifs.
- **Chemin runtime hors STREAM/RR :** le prefill Classic n’a plus de chemin
  de soumission ; le fallback FatFs conditionnel de
  `sample_stream_io_begin_to()` (`Src/Sampler/sample_stream_io.c:281-317`)
  reste réservé aux registrations `physical_only == 0` de FULL/bulk et n’est
  pas un chemin actif de reader Stream dans le graphe courant.
- **SD scheduler :** un seul arbitre physique est branché
  (`Src/SD/sd_scheduler_runtime.c`). Ses priorités de marge/protection sont
  inter-classes READ/WRITE/FILESYSTEM, pas un EDF ni une priorité par reader
  Stream ; elles ne modifient pas le RR Stream.

## DOCUMENT

Document mis à jour après réaudit complet de l’état courant.

## DÉPENDANCES HORS ZONE

Aucune.
