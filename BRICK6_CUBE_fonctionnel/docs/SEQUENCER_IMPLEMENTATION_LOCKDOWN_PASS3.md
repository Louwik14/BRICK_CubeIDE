# Passe 3 — verrouillage final pré-implémentation

## 1. Corrections par rapport à la passe 2

1) **Multi-step lock manquant (corrigé et confirmé)**  
- Ancienne ambiguïté: possible base “ref step”.  
- Règle finale:  
  - lock existant => `old + delta`  
  - lock absent => `base_track + delta`  
- Donc l’exemple utilisateur est respecté (`A:10->20`, `B:base40->50` pour delta +10).  

2) **Politique pool (corrigée)**  
- La règle produit n’expose **aucune limite globale supplémentaire**.  
- Règle utilisateur unique: limite **par step** (`MAX_LOCKS_PER_STEP`).  
- Le pool global n’est qu’une matérialisation mémoire de la capacité théorique totale.  

3) **Messages UI (corrigé)**  
- Supprimer le message “POOL FULL” comme contrainte produit distincte.  
- Conserver seulement erreurs utilisateur pertinentes: “STEP FULL” (+ éventuellement “PASTE TRUNC/PARTIAL”).

---

## 2. Décision ferme sur la référence multi-step

**Décision V1: référence = plus petit index de step tenu (`min(held_steps)`).**

Pourquoi:
- Déterministe, stable, indépendant de l’ordre de scan capteurs/halls.  
- Reproductible en debug (bitmask -> min bit set).  
- Cohérent avec une sélection spatiale “gauche -> droite” sur grille 16 steps/page.  

Non retenu:
- “premier step tenu” (dépend du timing d’appui).  
- “step actif UI” (ambigu pendant lecture/changement focus).

---

## 3. Algorithme final multi-step avec lock manquant

Entrées:
- `held_mask` (steps sélectionnés)  
- `param8` édité  
- `delta` encodeur  
- `track` actif  

Pseudo-algo C embarqué:

```c
for each step s in held_mask:
    if has_lock(track, s, param8):
        v0 = get_lock(track, s, param8);        // old
    else:
        v0 = seq_param_base_get(track, param8); // base track

    v1 = clamp_param(param8, v0 + delta);

    if has_lock(track, s, param8):
        set_lock(track, s, param8, v1);
    else:
        // auto-create lock manquant
        if step_lock_count(s) >= MAX_LOCKS_PER_STEP: return STEP_FULL;
        alloc_and_insert_lock(track, s, param8, v1);
```

Cas limites:
- Delta négatif/positif: clamp individuel par step.  
- Mix lockés/non lockés: comportement homogène via `v0` différent (`old` vs `base`).  
- Si création impossible (step plein): fail local explicite, pas de magie.

---

## 4. Décision ferme sur PLAY/STOP

**Décision V1: PLAY depuis STOP repart au step 0 (global).**

Pourquoi:
- Comportement le plus lisible et testable en V1.  
- Cohérent avec architecture actuelle (transport minimal déjà “reset-like” côté recorder).  
- Évite ambiguïté “resume” sans UI dédiée d’état transport avancé.  

STOP:
- stoppe la lecture globale, conserve données séquence, UI focus libre.  

Évolution future:
- option “resume position” possible plus tard, mais hors V1.

---

## 5. Politique mémoire finale corrigée

Définir le pool à la **capacité théorique totale**:

```c
POOL_CAP = TRACK_COUNT * MAX_STEPS * MAX_LOCKS_PER_STEP
```

Avec V1:
- `TRACK_COUNT=8`, `MAX_STEPS=64`, `MAX_LOCKS_PER_STEP=16`  
- `POOL_CAP = 8192 entries`

Conséquence:
- plus de contrainte produit “globale” distincte; tout step peut atteindre son max sans être bloqué artificiellement par un pool trop petit.  
- simplicité conceptuelle: la limite utilisateur reste uniquement “par step”.

Note RAM:
- si entrée lock = 8 bytes => ~64 KB pour pool seul (acceptable selon contrainte RAM D2 donnée).  
- rester en RAM interne (pas SDRAM) pour runtime déterministe.

---

## 6. Impact concret sur les structs / constantes / messages UI

Constantes (corrigées):

```c
#define SEQ_STEP_MAX_LOCKS   16u
#define SEQ_TRACK_COUNT      8u
#define SEQ_MAX_STEPS        64u
#define SEQ_PLOCK_POOL_CAP   (SEQ_TRACK_COUNT * SEQ_MAX_STEPS * SEQ_STEP_MAX_LOCKS) // 8192
```

Structs:
- inchangées en forme (step + lock pool + free-list), mais tableau `pool[]` redimensionné à 8192.

Logique runtime:
- inchangée (restore/apply/trig), sauf suppression du chemin d’erreur “pool global produit plein”.

Messages UI:
- retirer “POOL FULL”.  
- garder “STEP FULL”, “PASTE PARTIAL”, “PASTE TRUNC”.

Pass 2 explicitement corrigée:
- section multi-step: lock absent = `base_track + delta` (et non ref-step).  
- section pool: plus de limite globale produit indépendante.

---

## 7. Questions restantes uniquement si elles sont encore réellement bloquantes

1) **Mapping `param8` persistant exact** (table stable COLORS/TONE) — bloquant format fichier.  
2) **Aucune autre question bloquante** pour démarrer l’implémentation V1.

