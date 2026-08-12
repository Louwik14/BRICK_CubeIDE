# Audit local Z1 — dette hard real-time audio

Branche observée : `main_doublemcu_monocore`  
Périmètre : Z1, avec lectures directes Z2/Z3/Z4/Z5/Z6 uniquement pour reconstruire les producteurs.  
Méthode : analyse statique du code courant et des fichiers `.su` présents ; aucun test lourd, aucune mesure sur cible et aucun changement fonctionnel.

## 1. Verdict Z1

Z1 ne possède pas un contrat unique de publication à la frontière audio. Trois formes distinctes coexistent : état DSP écrit directement depuis main et IRQ, ring Stack main/IRQ avec perte silencieuse puis drain intégral, et runtime NoteFx mutable depuis les deux contextes. Les risques Stack et NoteFx sont confirmés par le code. La portée de `GLOBAL-001` doit être nuancée : une écriture scalaire alignée n'est pas en elle-même un snapshot déchiré sur Cortex-M7, mais plusieurs setters publient des couples de champs, réinitialisent des machines ou effacent de gros buffers pendant que l'IRQ les consomme. La corruption mémoire n'est pas démontrée ; l'incohérence sémantique et le travail non maîtrisé le sont.

La frame automatique IRQ de `process_half` est connue (1632 octets dans les `.su` Release/Premium courants), mais la marge MSP réelle, le graphe cumulé complet et l'imbrication effective des IRQ ne le sont pas. La partie IRQ de `GLOBAL-002` requiert donc une mesure sur cible.

| Ticket | Statut final | Résumé |
|---|---|---|
| `GLOBAL-001` | `PARTIAL` | Partage main/IRQ confirmé et plusieurs transitions incohérentes prouvées ; généralisation à tous les scalaires et conséquence audible non démontrées. |
| `GLOBAL-002` (IRQ) | `NEEDS MEASUREMENT` | Frame `process_half` de 1632 octets et réserve minimale linker de 1024 octets confirmées ; dépassement MSP et marge cumulée inconnus. |
| `Z1-001` | `CONFIRMED` | Capacité utile 255, échecs ignorés, aucune métrique/priorité, drain jusqu'à 255 commandes par appel IRQ. |
| `Z1-002` | `CONFIRMED` | `g_slot`, overrides et état ARP sont réellement multi-écrivains main/IRQ sans sérialisation. |
| `Z1-003` (nouveau) | `CLOSED PARTIALLY — ÉTAPE 6` | Le budget NoteFx est désormais partagé sur 64 frames; le coût H743 et l'admission terminale restent à mesurer/traiter. |

## 2. Graphe réel producteurs → publication/file → consommateurs

### Contrôles mixer/FX (`GLOBAL-001`)

```text
hors IRQ
  UI / clipboard / macro / restore Pattern-Patch / transitions track
    -> param_registry_apply_track_value()
    -> param_filter / param_registry_backends / apply wrappers
    -> mixer_set_*() / fx_*_global_set_*()
    -> g_tracks[], g_track_filters[], g_reverb, g_delay_type,
       g_delay, g_dual, g_reverb_global, g_revb

IRQ audio
  seq_runtime_audio_apply_event()
    -> seq_param_iface
    -> param_registry_apply_track_value_runtime_temp()
    -> mêmes backends/setters pour les paramètres temporaires

IRQ audio consommateur
  HAL_SAI_Rx{Half,}CpltCallback()
    -> process_half()
    -> audio_process_block_int32()
    -> brick6_audio_runtime_process()
    -> mixer_process()
    -> filtres de lane + delay + reverb
```

Preuves : `Src/Audio/audio.c::{HAL_SAI_RxHalfCpltCallback,HAL_SAI_RxCpltCallback,process_half,audio_apply_seq_event_at_sample}` ; `Src/Seq/seq_param_iface.c` ; `Src/Param/param_registry.c::{param_registry_apply_track_value,param_registry_apply_track_value_runtime_temp}` ; `Src/Param/param_filter.c` ; `Src/Param/param_registry_backends.c` ; `Src/Param/param_registry_apply_wrappers.c` ; `Src/Audio/mixer.c::{mixer_process,mixer_set_track_*,mixer_set_reverb_*,mixer_set_delay_*}`.

Données partagées réellement dangereuses :

- `g_tracks[]` : cibles gain/pan/send/mute/routes écrites hors IRQ, valeurs courantes de smoothing modifiées dans l'IRQ. Les champs scalaires séparés sont atomiques à l'échelle machine, mais aucune génération ne groupe une restauration multi-paramètres.
- `g_track_filters[]` : configuration et état d'exécution cohabitent. `mixer_set_track_vca_enabled()` modifie successivement `vca_enabled`, note/count/gate/value puis appelle `env_adsr_reset`; l'IRQ peut observer un préfixe de cette transition. Les setters ADSR touchent l'objet que `mixer_process` fait avancer.
- delay : les setters feedback/width/volume écrivent une cible puis un compteur de smoothing. Une préemption entre les deux donne temporairement une nouvelle cible avec l'ancien compteur. `mixer_set_delay_type()` publie d'abord `g_delay_type`, puis efface le backend sélectionné ; l'IRQ peut traiter ce backend pendant son clear.
- reverb : `fx_reverb_global_set_model()` applique plusieurs paramètres et `fx_reverb_revb_global_set_model()` reset le predelay et le moteur avant de publier modèle/fade. `fx_reverb_global_set_wet()` peut aussi reset le backend. Ces resets hors IRQ peuvent préempter le traitement du même moteur.

La preuve est insuffisante pour affirmer que chaque setter scalaire isolé produit un état invalide ou un clic. Le défaut confirmé est le contrat synchrone commun à des appels simples et à des transitions composites ; les symptômes doivent être regroupés sous cette cause, pas sous un « bus » générique à créer.

### File Stack (`Z1-001`)

```text
hors IRQ
  keyboard_engine / seq_output_guard / reapply paramètre ou restore
    -> brick6_stack_runtime_submit_*()
IRQ audio
  scheduler/terminal -> mêmes submit_*() possibles pendant la timeline
publication
  g_stack_command_queue[256], head/tail uint8_t, section PRIMASK locale
consommateur IRQ
  audio_process_block_int32()
    -> brick6_audio_runtime_process()
    -> brick6_stack_runtime_process_commands_from_audio()
    -> g_stack_runtime[8] ou g_stack_poly_runtime_d2[8]
```

Preuves : `Src/Core/brick6_stack_runtime.c::{brick6_stack_runtime_submit_command,brick6_stack_runtime_cancel_note_state,brick6_stack_runtime_process_commands_from_audio}` ; `Src/Keyboard/keyboard_engine.c` ; `Src/Seq/seq_output_guard.c` ; `Src/Param/param_registry_backends.c` ; `Src/Core/brick6_audio_runtime.c::brick6_audio_runtime_process`.

La ring est cohérente en monocœur contre la préemption pendant head/tail grâce à `PRIMASK`, mais elle ne garantit pas le sens musical : capacité utile exacte 255 ; plein => retour `0` sans compteur ; les Note On, Note Off, All Notes Off et commandes paramètre partagent la même capacité. Les appels clavier et guard ignorent le retour. Le reapply Stack ignore jusqu'à 27 retours par instance (4 slots × 6 paramètres, puis noise/detune/phase-reset). `cancel_note_state()` compacte la ring et pose un flag seulement pour les instances 0..7, alors que les soumissions et le runtime acceptent 0..15.

### Runtime NoteFx (`Z1-002`)

```text
hors IRQ
  keyboard_engine::note_fx_pipeline_submit()
  UI focus/clear, snapshots, undo, Pattern restore, param MODEL/base
    -> cleanup/suspend/reset overrides/sync/configure/source

IRQ audio
  seq_play_scheduler -> note_fx_pipeline_submit()
  seq_param_iface p-lock -> apply/release_runtime_param()
  process_audio_segment -> note_fx_pipeline_process()

état partagé sans file ni section critique
  note_fx_pipeline.c: override_valid/value, runtime_arp_slot
  note_fx_engine.c: g_slot[][] (arp, owned[], next_sample, generation,
                    model/rate/style/range/suspended/destination), g_token, g_diag
  note_fx_state.c: bases g_note_fx_state[][]

sortie
  note_fx_pipeline_terminal()
    -> seq_play_scheduler_dispatch_terminal_note_to_channel()
    -> moteurs locaux / MIDI et output guard
```

Preuves : `Src/NoteFx/note_fx_pipeline.c` ; `Src/NoteFx/note_fx_engine.c` ; `Src/NoteFx/note_fx_state.c` ; appels directs dans `Src/Keyboard/keyboard_engine.c`, `Src/Seq/seq_play_scheduler.c`, `Src/Seq/seq_param_iface.c`, `Src/Param/param_registry.c`, `Src/UI/ui_core.c`, `Src/Core/track_snapshot.c`, `Src/Storage/pattern_live_ram.c` et `Src/Storage/undo_v2.c`.

Une préemption peut être incohérente : `release_slot()` incrémente generation, parcourt/émet/efface `owned[]`, réinitialise l'ARP puis `next_sample`; simultanément l'IRQ peut parcourir les mêmes owned, émettre et programmer `next_sample`. `note_fx_pipeline_sync_track()` reconfigure quatre slots successivement ; l'IRQ peut voir une configuration partielle. `note_fx_state_set_param()` désactive un ancien slot ARP puis écrit le nouveau ; sa copie par `capture_track` n'est pas transactionnelle face à cette mutation. Les appels hors IRQ peuvent enfin émettre des Note Off directement dans le scheduler/output guard pendant que l'IRQ les utilise.

## 3. Bornes exactes connues et inconnues

### Connues

- Demi-buffer audio : exactement 64 frames (`BOARD_AUDIO_FRAMES_PER_HALF`).
- Tableau automatique `process_half` : 128 `seq_runtime_audio_event_t`; frame compilateur Release/Premium observée : 1632 octets.
- Segmentation : au plus 64 itérations externes et 64 segments audio non vides par demi-buffer, car les fallbacks forcent au moins une frame.
- Stack : ring 256 avec un slot vide, donc backlog maximal 255 ; le drain traite 0..255 commandes lors de chaque appel. Il examine aussi exactement 8 flags cancel. La fonction est appelée une fois par appel à `brick6_audio_runtime_process`, donc jusqu'à 64 fois par demi-buffer, mais seul le premier appel peut vider le backlog présent et les producteurs IRQ peuvent en ajouter ensuite.
- NoteFx : 8 tracks × 4 slots ; 16 owned par slot ; scan `next_deadline` de 32 slots ; `process` recrée un budget de 8 émissions par track et par appel. Au plus 64 appels `process` par demi-buffer, donc le plafond syntaxique du budget normal est 8 × 8 × 64 = 4096 émissions. Ce nombre n'est pas un débit musical attendu, mais c'est la borne imposée par le code actuel, pas 64 émissions par demi-buffer.
- `release_slot()` peut parcourir et émettre jusqu'à 16 Note Off par slot hors du budget local ; cleanup d'une track parcourt 4 slots, cleanup_all 8 tracks.
- Linker Low-Cost et Premium : `_Min_Stack_Size = 0x400` (1024 octets), MSP au sommet des 512 Kio de RAM_D1. Cette constante réserve/protège un minimum ; elle ne prouve pas que seulement 1024 octets sont disponibles à l'exécution.

### Inconnues ou non prouvées

- cycles maximum de chacune des 13 variantes de commande Stack, notamment reset/model, et coût exact de 255 commandes ; aucun budget en cycles n'existe ;
- nombre réel maximal d'événements scheduler produits dans un demi-buffer sous toutes les combinaisons ;
- profondeur MSP cumulée complète de `process_half` jusqu'aux moteurs C/C++, plus coût d'empilement matériel FPU et IRQ de priorité supérieure ; les `.su` sont locaux et non un callgraph ;
- high-water MSP Low-Cost/Premium et distance réelle jusqu'aux sections D1 vivantes ;
- durée des clears/reset delay/reverb et effet audible d'une préemption ;
- débit maximal externe de producteurs main pendant 1,333 ms et probabilité réelle de saturation Stack ;
- temps maximal NoteFx/terminal quand les émissions déclenchent moteurs et MIDI. Une validation temporelle sur cible est obligatoire.

## 4. Scénarios de panne réalistes

1. Restore/reapply d'une track Stack remplit la ring avec des paramètres ; une rafale clavier ajoute un Note Off ou All Notes Off. Le retour `0` est ignoré : la voix reste active. L'IRQ suivante traite jusqu'à 255 commandes avant le rendu Stack.
2. Une instance Stack 8..15 doit être annulée pendant transition. `brick6_stack_runtime_cancel_note_state()` retourne sans compacter ni poser de flag ; les anciennes commandes note restent exécutables après la transition.
3. La main change le type de delay : `g_delay_type` pointe déjà le nouveau backend, puis l'IRQ préempte le clear de ce backend. Traitement et effacement accèdent simultanément au même buffer/indices ; rendu partiellement remis à zéro plausible.
4. Désactivation VCA/filter ou changement de modèle reverb pendant l'IRQ : l'audio observe une transition multi-champs partiellement appliquée, pouvant créer reset, discontinuité ou enveloppe incohérente.
5. Édition MODEL/clear/restore NoteFx pendant `note_fx_engine_process()` : cleanup et process émettent le même owned Note Off, ou cleanup efface l'ownership pendant que process crée le nouveau token ; double fermeture ou note orpheline plausible.
6. Des deadlines NoteFx forcent des segments d'une frame. Le budget de 8 émissions/track est recréé 64 fois et chaque segment relance tout le pipeline audio ; le coût est borné syntaxiquement mais trop élevé et non mesuré pour un contrat hard-RT.
7. Une grosse frame main/UI est préemptée par l'audio. Les 1632 octets de `process_half` s'ajoutent à la profondeur courante et aux IRQ supérieures ; le dépassement réel reste indéterminé sans high-water/callgraph.

## 5. Causes racines et plus petits contrats de correction

- `GLOBAL-001` : les setters synchrones mélangent publication de contrôle et mutation de l'état d'exécution. Contrat minimal : pour chaque sous-système prouvé (lane mixer/filter, delay, reverb), regrouper seulement ses contrôles composites dans un staging fixe et les appliquer par l'owner audio à une frontière de segment/bloc. Les apply déjà en IRQ peuvent appeler l'owner directement. Aucun bus générique, aucune façade dual-core.
- `GLOBAL-002` IRQ : absence de budget MSP vérifié. Contrat minimal : seuil `.su` explicite plus callgraph IRQ conservateur et canari/high-water par variante avant tout déplacement de buffer.
- `Z1-001` : une file unique sans classes de sûreté ni contrat de refus. Contrat minimal : compteur/high-water d'abord ; garantir qu'un Note On n'est accepté que si sa terminaison est garantie, rendre les échecs visibles, couvrir les 16 instances, puis fixer un quota par demi-buffer fondé sur mesure. Ne pas introduire une infrastructure de messages générale.
- `Z1-002` : aucun owner unique du runtime NoteFx. Contrat minimal : `g_slot`, overrides et tokens appartiennent exclusivement à l'IRQ ; les mutations main nécessaires passent par une petite file fixe NoteFx et sont appliquées avant process. `note_fx_state` reste la base canonique hors runtime. Les sorties terminales sont émises uniquement par l'owner.
- `Z1-003` : le budget est maintenant attaché à la période hard-RT de `process_half`; le coût H743 et l'admission terminale restent résiduels.

## 6. Ordre recommandé des futurs micro-patches

1. Instrumentation sans changement de politique : overflow/high-water Stack, émissions/cleanup NoteFx par demi-buffer, max segments et cycles IRQ, canari MSP.
2. Stack : étendre le cancel aux 16 instances et traiter tous les retours terminal/restore ; séparer la garantie terminale de la future politique de quota.
3. NoteFx : fixer un budget partagé par demi-buffer, y inclure les releases, puis fermer le multi-écrivain par une file fixe locale au module.
4. Mixer : publier d'abord les seules transitions destructrices prouvées (`delay_type/clear`, reverb model/reset/wet reset, enable/reset VCA/filter), puis les couples target+smoothing. Ne pas déplacer tous les scalaires par principe.
5. Stack : introduire le quota mesuré par demi-buffer seulement après garantie des commandes terminales.
6. Stack MSP : déplacer uniquement une frame démontrée problématique après high-water et callgraph ; ne rien relocaliser spéculativement.

## 7. Validations requises par patch

| Patch futur | Validations minimales |
|---|---|
| Instrumentation | Build Low-Cost/Premium ; compteurs monotones et sans coût IRQ significatif ; trace worst-case 64-frame. |
| Cancel/retours Stack | Saturation à 254/255 entrées ; Note On/Off/All Notes Off ; instances 0, 7, 8 et 15 ; aucun replay après transition. |
| Budget NoteFx | Deadlines à chaque frame, 8 tracks/4 slots, cleanup et MODEL concurrents ; plafond d'émissions et cycles par demi-buffer vérifiés. |
| Owner NoteFx | Stress clavier main + scheduler/p-lock IRQ + clear/restore ; invariant « chaque owned actif a une fermeture unique » ; ordre tokens/generations. |
| Publication delay/reverb/VCA | Préemption injectée à chaque point de transition ; aucune lecture pendant clear/reset ; comparaison audio avant/après et mesure CPU. |
| Quota Stack | Backlog 255 avec mix des 13 types ; latence terminale garantie ; coût maximum par demi-buffer ; ordre des paramètres conservé ou politique documentée. |
| Gouvernance MSP | Analyse `.su` propre des deux variantes, callgraph IRQ, FPU/IRQ imbriquées, stack painting en scénarios UI Audio Rec/clipboard + audio ; marge minimale documentée. |

## 8. Nouveau ticket Z1 à preuve forte

### `Z1-003` — budget NoteFx réinitialisé à chaque sous-segment audio

- **Statut :** `CLOSED PARTIALLY — ÉTAPE 6`.
- **Preuve :** `process_half()` peut forcer 64 blocs d'une frame ; `process_audio_segment()` peut appeler `note_fx_pipeline_process()` à chaque frame ; `note_fx_engine_process()` recrée localement `uint8_t budget = 8` pour chacune des 8 tracks à chaque appel. `release_slot()` émet en plus hors de ce budget.
- **Impact démontré :** le contrat annoncé par le symbole `NOTE_FX_MAX_EMISSIONS_PER_BLOCK` ne borne ni une demi-IRQ ni tous les chemins d'émission. La borne syntaxique normale est 4096 émissions par demi-buffer, plus releases ; le respect du deadline audio nécessite une mesure.
- **Micro-contrat :** budget unique, fixe et transmis sur toute la durée du demi-buffer, releases comprises. Aucun bus et aucune préparation dual-core.

## Addendum 2026-08-02 - fermeture partielle de Z1-002

Le runtime NoteFx dispose maintenant d'une file locale fixe de 32 commandes;
les overlays, configurations, sources main et transitions sont appliques par
l'owner audio avant `note_fx_engine_process()`. Les sorties terminales de ce
chemin restent emises par cet owner. Le budget partage par demi-buffer est
maintenant ferme par l'etape 6; la validation H743 et l'admission terminale
restent a mesurer/traiter.

La chaîne de stages est également explicite: les continuations générées
reprennent au slot suivant et le terminal est réservé au stage 4. Le retrait
du premier-ARP ne change pas les limites de budget; celles-ci restent une
dette de l'étape 6.

## 9. Limites de preuve

Cet audit ne conclut ni à un underrun observé, ni à une corruption MSP, ni à un clic reproduit. Les fichiers `.su` présents peuvent provenir de révisions/options différentes ; seuls les artefacts Release/Premium concordants à 1632 octets ont été retenus. Les bornes en nombre d'itérations/commandes sont exactes au niveau source ; leurs coûts temporels restent à mesurer sur H743 avec les deux variantes.
# Addendum 2026-08-02 - fermeture partielle de Z1-003

`process_half()` ouvre un contexte NoteFx unique pour 64 frames. Les quotas
On/Off, les continuations, les releases et les cleanups partagent ce contexte;
la file owner est plafonnée à 32 commandes consommées par demi-buffer. Le test
statique `tests/note_fx_budget_validation.ps1` vérifie l'absence du budget local
par sous-segment et l'absence de release hors admission.

# Addendum 2026-08-05 — admission mono et validation

L’admission terminale des moteurs mono possède maintenant une politique fixe
d’une occurrence interne active par piste : un nouvel On concurrent est
refusé avant l’appel aux APIs moteur `void`, et un Off stale est un no-op
acquitté. Cette politique borne le ledger sans prétendre que les moteurs
historiques acquittent eux-mêmes leurs APIs `void`. Les interleavings, la
saturation, l’exécution hôte et le coût H743 restent à mesurer. Un test C de
restore et une validation statique sont maintenant présents dans le CMake ;
les interleavings owner, la saturation et les mesures H743 restent ouvertes.

## Addendum 2026-08-05 - consolidation étapes 6 à 10

Le runtime EUCLID applique maintenant ce micro-contrat avec trois slots par
piste, 16 sources fixes et 16 owned fixes par instance. Le pipeline expose les
admissions/refus On et Off générés, l'utilisation de la réserve Off et le
high-water par demi-buffer ; le moteur expose les high-water et causes de
refus par slot. La borne logique est 8 x 3 x 16 sources/owned, sans allocation.

La validation statique de consolidation est enregistrée dans
tests/note_fx_step10_consolidation_validation.ps1 et les builds Release
Low-Cost/Premium passent. Les mesures DWT/p99, marge IRQ et underrun H743 sont
explicitement hors de cette passe et restent ouvertes.
