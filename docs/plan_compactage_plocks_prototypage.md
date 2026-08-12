# Chantier — Compacter les états et les slots de p-locks en phase de prototypage

## 1. Objectif

Réduire fortement la consommation de `RAM_D2` du système de p-locks en remplaçant les grands tableaux clairsemés par une représentation compacte entièrement statique.

Le projet est encore en prototypage :

- aucune compatibilité avec les anciens projets, patterns ou fichiers sauvegardés n’est exigée ;
- le format courant peut être cassé ou réinitialisé ;
- les anciens numéros de slots ne constituent pas un contrat à préserver ;
- aucune couche de traduction historique ne doit être ajoutée ;
- le nouveau mapping compact devient directement l’autorité du runtime et du stockage.

Le chantier doit préserver le comportement musical du firmware produit après la migration :

- mêmes paramètres p-lockables décidés pour le firmware courant ;
- mêmes limites de locks par step ;
- mêmes pools Play/Special ;
- même application, continuité et restauration des p-locks ;
- Undo/Redo et Clipboard cohérents avec le nouveau format ;
- changements de moteur sûrs ;
- aucune allocation dynamique.

Le chantier ne doit pas modifier les moteurs audio ni profiter du compactage pour effectuer un ménage général de la taxonomie des paramètres.

---

## 2. État actuel

Le runtime réserve actuellement :

```c
g_seq_param_state[14][5][256];
g_seq_param_mix_state[14][4];
g_seq_param_base_valid_bits[];
g_seq_param_runtime_locked_bits[];
```

Chaque état contient :

```c
typedef struct
{
    seq_value16_t base_value;
    seq_value16_t runtime_value;
} seq_param_slot_state_t;
```

Les principaux symboles compactables représentent actuellement environ :

```text
g_seq_param_state                   71 680 B
g_seq_param_mix_state                  224 B
g_seq_param_base_valid_bits          2 247 B
g_seq_param_runtime_locked_bits      2 247 B
g_seq_param_id_to_slot               1 615 B
g_seq_param_slot_to_id               2 560 B
Total                               80 573 B
```

Le mapping effectif audité donne les capacités maximales suivantes :

```text
ENV       25 slots
TONE      21 slots
PLAY      16 slots
MOD       12 slots
MIDI_FX   16 slots
MIX        4 slots
Total     94 slots par piste
```

Les 127 paramètres TONE n’exigent que 21 slots par piste, car les mêmes slots sont réutilisés selon le moteur actif.

---

## 3. Principe retenu

Utiliser directement un tableau runtime compact unifié :

```c
#define SEQ_PARAM_ENV_SLOT_COUNT       25U
#define SEQ_PARAM_TONE_SLOT_COUNT      21U
#define SEQ_PARAM_PLAY_SLOT_COUNT      16U
#define SEQ_PARAM_MOD_SLOT_COUNT       12U
#define SEQ_PARAM_MIDI_FX_SLOT_COUNT   16U
#define SEQ_PARAM_MIX_SLOT_COUNT        4U

#define SEQ_PARAM_RUNTIME_SLOT_COUNT   94U
```

Offsets fixes :

```text
ENV       0
TONE     25
PLAY     46
MOD      62
MIDI_FX  74
MIX      90
END      94
```

Stockage runtime :

```c
SEQ_STATE_D2 static seq_param_slot_state_t
    g_seq_param_runtime_state[SEQ_TRACK_COUNT][SEQ_PARAM_RUNTIME_SLOT_COUNT];
```

Pour cette première migration, conserver `SEQ_TRACK_COUNT == 14` pour les deux variantes.

Ne pas réduire Low-Cost à 12 lignes dans ce chantier. Le gain de 800 octets ne justifie pas d’ajouter un second changement de contrat. Une réduction à 12 lignes pourra être étudiée séparément.

---

## 4. Nouveau contrat des slots

Après migration, le slot compact devient directement l’identité utilisée par :

- le runtime ;
- les entrées de p-lock des patterns ;
- Undo/Redo ;
- Clipboard ;
- les APIs internes du séquenceur.

Il n’existe plus de distinction entre :

```text
ancien slot persistant
index runtime compact
```

Il n’existe donc aucune table :

```text
ancien slot → nouveau slot
```

et aucun code de compatibilité avec les projets précédents.

Pour chaque set, les slots valides sont directement :

```text
ENV       0..24
TONE      0..20, interprétés selon le moteur actif
PLAY      0..15
MOD       0..11
MIDI_FX   0..15
MIX       0..3
```

Les slots hors limites doivent être refusés proprement lors de l’écriture ou du chargement du nouveau format, et jamais conservés comme entrées inertes historiques.

---

## 5. Mapping paramètre ↔ set/slot

Créer une autorité unique et explicite du mapping courant :

```text
param_id_t → set_id + slot compact
set_id + slot compact + moteur éventuel → param_id_t
```

Le mapping ne doit plus être reconstruit à partir de grands tableaux RAM de 256 entrées.

Préférer des tables `const` en Flash :

- table directe `param_id_t → set + slot`, avec valeur invalide pour les paramètres non p-lockables ;
- tables inverses compactes pour ENV, PLAY, MOD, MIDI_FX et MIX ;
- tables TONE existantes ou consolidées par moteur.

Les ordinals des sets restent :

| Set | Valeur |
|---|---:|
| ENV | 0 |
| TONE | 1 |
| PLAY | 2 |
| MOD | 3 |
| MIDI_FX | 4 |
| MIX | 5 |

Ajouter des assertions de compilation sur :

- les ordinals ;
- les offsets ;
- le total de 94 slots ;
- les capacités maximales ;
- la taille des tables inverses ;
- la validité des slots TONE pour chaque moteur.

---

## 6. Décision explicite sur les paramètres p-lockables

Puisque la compatibilité historique n’est pas exigée, ne pas figer les incohérences actuelles par accident.

Avant la migration, produire une décision explicite pour les cas ambigus, notamment Looper :

- `PARAM_LOOPER_ARM`
- `PARAM_LOOPER_LEN`
- `PARAM_LOOPER_PLAY`
- `PARAM_LOOPER_STRETCH`
- `PARAM_LOOPER_PITCH`
- `PARAM_LOOPER_GRAIN`

Le code actuel est incohérent entre reconstruction des maps et validation track-aware.

Pour le nouveau contrat :

1. déterminer quels paramètres Looper doivent réellement être p-lockables dans le produit courant ;
2. définir une seule allowlist ;
3. utiliser cette allowlist dans toutes les directions du mapping ;
4. supprimer les exclusions dupliquées et contradictoires.

Ne pas conserver un comportement uniquement parce qu’un ancien firmware le produisait.

Appliquer la même règle aux autres exclusions : une seule autorité du caractère p-lockable.

Hors périmètre malgré tout :

- renommage global de domaines ;
- ajout de nouveaux paramètres ;
- déplacement fonctionnel de paramètres entre ENV/TONE/PLAY/MOD/MIX ;
- refonte musicale du Looper.

---

## 7. Structure d’accès centrale

Tous les accès aux états doivent passer par une façade unique :

```c
static seq_param_slot_state_t *
seq_param_iface_state_at(seq_track_id_t track,
                         uint8_t set_id,
                         seq_param_slot_t compact_slot);
```

Cette fonction doit :

1. vérifier `track < SEQ_TRACK_COUNT` ;
2. vérifier le set ;
3. vérifier la capacité compacte du set ;
4. vérifier le slot TONE selon le moteur actif lorsque nécessaire ;
5. calculer `offset[set_id] + compact_slot` ;
6. retourner `NULL` si le couple est invalide.

Aucun autre module ne doit connaître directement les offsets.

Le calcul doit rester O(1), sans recherche linéaire dans le chemin d’application ou de restauration des locks.

---

## 8. Bitmaps compactes

Redimensionner directement les deux bitmaps selon :

```text
SEQ_TRACK_COUNT × 94 slots
```

Exemple :

```c
#define SEQ_PARAM_RUNTIME_FLAG_BIT_COUNT \
    (SEQ_TRACK_COUNT * SEQ_PARAM_RUNTIME_SLOT_COUNT)

#define SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT \
    ((SEQ_PARAM_RUNTIME_FLAG_BIT_COUNT + 7U) / 8U)
```

Puis :

```c
static uint8_t
    g_seq_param_base_valid_bits[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT];

static uint8_t
    g_seq_param_runtime_locked_bits[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT];
```

Avec 14 lignes :

```text
14 × 94 = 1316 bits
165 octets par bitmap
```

Centraliser le calcul d’index des bits sur le même index compact que les états.

---

## 9. Format de stockage

Le format de stockage peut être cassé.

Adapter les structures persistées pour documenter explicitement que `param_slot` est désormais un slot compact du firmware courant :

```c
uint8_t set_id;
uint8_t compact_slot;
uint16_t value16;
uint8_t flags;
```

Le champ peut conserver son nom actuel si renommer entraînerait trop de bruit, mais son nouveau contrat doit être clair.

Les versions de Pattern/Project concernées doivent être incrémentées ou les données existantes doivent être explicitement invalidées.

Aucune migration depuis l’ancien format n’est demandée.

Au chargement :

- accepter uniquement la nouvelle version ;
- valider `set_id` ;
- valider le slot compact ;
- rejeter proprement un fichier incompatible ou invalide ;
- ne jamais tenter d’interpréter les anciens slots.

Mettre à jour ensemble :

- Pattern ;
- Project si les p-locks y sont inclus ;
- Clipboard ;
- Undo/Redo ;
- fixtures de tests.

Le format des pools de p-locks, leurs budgets et leurs limites par step ne changent pas.

---

## 10. Gain RAM attendu

Avec 14 pistes et 94 états :

```text
14 × 94 × 4 = 5264 B
```

Deux bitmaps :

```text
165 + 165 = 330 B
```

Nouvelle RAM principale :

```text
5264 + 330 = 5594 B
```

Ancienne RAM compactable :

```text
80 573 B
```

Gain cible approximatif :

```text
80 573 - 5 594 = 74 979 B
```

Les mappings deviennent `const` en Flash.

Cible contractuelle Low-Cost :

```text
au moins 74 900 B récupérés en RAM_D2
```

Le gain exact doit être prouvé dans le `.map` Release Low-Cost.

Aucun gain ne doit être revendiqué sur :

- `g_param_runtime_track_values` ;
- `g_param_runtime_track_valid` ;
- les pools de p-locks ;
- les active locks ;
- Undo/Clipboard en SDRAM ;
- les autres domaines RAM.

---

# Plan d’action révisé

## Étape 1 — Définir le nouveau contrat compact

### But

Décider et figer le mapping du firmware courant, sans compatibilité historique.

### Travaux

- inventorier tous les paramètres p-lockables actuels ;
- décider explicitement les paramètres Looper p-lockables ;
- centraliser les exclusions ;
- définir les capacités `25/21/16/12/16/4` ;
- définir les offsets et le total 94 ;
- définir les nouvelles versions de fichiers invalidant l’ancien format ;
- ajouter les tests exhaustifs `param → set/slot → param`.

### Contraintes

- aucun nouvel état compact encore utilisé par le runtime ;
- aucun changement de moteur audio ;
- aucune tentative de migration d’ancien projet.

### Validation

- chaque paramètre possède un statut explicite ;
- chaque mapping valide est bijectif ;
- chaque slot invalide est rejeté ;
- tous les moteurs TONE sont couverts ;
- Release Low-Cost et Release Premium passent.

### Commit

Créer un commit local dédié, sans push.

---

## Étape 2 — Migrer simultanément états et bitmaps

### But

Remplacer les grands tableaux et les deux bitmaps par le stockage `[14][94]`.

### Travaux

- introduire `g_seq_param_runtime_state[14][94]` ;
- introduire les deux bitmaps de 165 octets ;
- créer la façade centrale d’accès ;
- migrer tous les appels de capture de base, application, continuité, restauration et reset ;
- supprimer simultanément :
  - `g_seq_param_state`;
  - `g_seq_param_mix_state`;
  - les anciens calculs de bits ;
- ne pas conserver les anciens et nouveaux tableaux en parallèle.

### Invariants

- même ordre d’application dans le firmware courant ;
- même retour aux valeurs de base ;
- mêmes limites de locks ;
- aucun accès hors limites ;
- changement de moteur sûr ;
- slots TONE revalidés selon le moteur.

### Tests

- base 40 → lock 80 → lock 60 → step vide → retour 40 ;
- locks consécutifs ;
- six sets simultanés ;
- tous les moteurs TONE ;
- changement de moteur avec locks actifs ;
- reset/init ;
- slots invalides refusés ;
- Release Low-Cost et Premium.

### Commit

Créer un commit local dédié, sans push.

---

## Étape 3 — Passer les mappings en Flash et supprimer le legacy

### But

Supprimer les mappings RAM et les chemins de reconstruction devenus inutiles.

### Travaux

- table `const param_id_t → set/slot compact` ;
- tables inverses compactes ;
- conserver ou consolider les tables TONE par moteur ;
- supprimer :
  - `g_seq_param_id_to_slot`;
  - `g_seq_param_slot_to_id`;
  - `seq_param_iface_rebuild_slot_maps()`;
  - constantes historiques de capacité 256 ;
  - façade legacy sans consommateur ;
  - exclusions dupliquées ;
- rechercher tous les accès directs aux anciens symboles.

### Validation

```powershell
rg "g_seq_param_state|g_seq_param_mix_state|g_seq_param_id_to_slot|g_seq_param_slot_to_id|rebuild_slot_maps" Src Inc
```

Les résultats restants doivent être nuls ou explicitement justifiés.

Vérifier le delta RAM/Flash dans les `.map`.

### Commit

Créer un commit local dédié, sans push.

---

## Étape 4 — Migrer le stockage courant, Undo et Clipboard

### But

Faire du slot compact l’identité stockée par le nouveau format.

### Travaux

- incrémenter ou réinitialiser les versions de stockage concernées ;
- mettre à jour Pattern/Project ;
- mettre à jour Clipboard ;
- mettre à jour Undo/Redo ;
- supprimer les fixtures d’ancien format ou les marquer explicitement incompatibles ;
- rejeter proprement les anciens fichiers ;
- valider les slots au chargement.

### Tests

- créer, sauvegarder et recharger un projet au nouveau format ;
- copy/paste/clear ;
- undo/redo ;
- p-locks dans les six sets ;
- pistes Play et Special ;
- changement de moteur ;
- fichier ancien rejeté proprement ;
- fichier corrompu ou slot hors limites rejeté.

### Commit

Créer un commit local dédié, sans push.

---

## Étape 5 — Validation finale et mesure mémoire

### But

Prouver le comportement courant et le gain D2.

### Validations obligatoires

- Release Low-Cost ;
- Release Premium ;
- tests mapping exhaustifs ;
- tests application/restauration ;
- tests moteur TONE ;
- tests Looper selon la nouvelle décision ;
- tests Pattern/Project nouveau format ;
- Clipboard ;
- Undo/Redo ;
- recherches négatives des anciens tableaux.

Comparer les `.map` avant/après :

```text
g_seq_param_state
g_seq_param_mix_state
g_seq_param_base_valid_bits
g_seq_param_runtime_locked_bits
g_seq_param_id_to_slot
g_seq_param_slot_to_id
g_seq_param_runtime_state
```

Rapporter :

- RAM_D2 avant/après Low-Cost ;
- RAM_D2 avant/après Premium ;
- gain par symbole ;
- gain total ;
- taille Flash ajoutée ;
- nombre final de paramètres et slots par set.

Critère Low-Cost :

```text
gain RAM_D2 >= 74 900 B
```

### Commit

Créer un commit local final de consolidation uniquement si des corrections supplémentaires sont nécessaires. Sans push.

---

## Étape 6 — Documentation

Documenter :

- les six sets et leurs ordinals ;
- les capacités compactes ;
- les offsets ;
- le fait que les slots sont directement runtime et stockage ;
- l’absence volontaire de compatibilité avec les anciens fichiers ;
- les règles TONE dépendantes du moteur ;
- la décision Looper ;
- les limites de p-locks inchangées ;
- les mesures finales de RAM_D2.

Ne mélanger aucun autre nettoyage taxonomique à cette étape.

---

# Hors périmètre

Ne pas réaliser dans ce chantier :

- compatibilité ou migration d’anciens projets ;
- couche de traduction ancien slot → nouveau slot ;
- allocation dynamique ;
- réduction de 14 à 12 lignes Low-Cost ;
- changement des pools de p-locks ;
- modification des limites 32/16 locks par step ;
- renommage ou déplacement général des domaines ;
- ajout/retrait de paramètres non décidé à l’étape 1 ;
- refonte du scheduler ;
- modification des moteurs audio ;
- ménage COLORS/ENV hors incohérences directement bloquantes ;
- déplacement des structures du streamer.

---

# Critère de réussite global

Le chantier est réussi si :

1. le nouveau firmware utilise directement 94 slots compacts par piste ;
2. aucun ancien grand tableau ou mapping RAM n’est conservé ;
3. aucun code de compatibilité historique n’est ajouté ;
4. le nouveau format de stockage est explicite et rejette l’ancien ;
5. les p-locks du firmware courant fonctionnent correctement ;
6. Undo/Redo et Clipboard utilisent la nouvelle identité compacte ;
7. les changements de moteur ne réutilisent aucun état invalide ;
8. les limites musicales restent inchangées ;
9. aucune allocation dynamique n’est ajoutée ;
10. le gain RAM_D2 Low-Cost est d’au moins 74 900 octets, confirmé par le linker ;
11. chaque passe validée produit un commit local précis, sans push ;
12. les builds requis sont Release Low-Cost et Release Premium uniquement.
