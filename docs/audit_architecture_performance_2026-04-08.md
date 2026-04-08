# Audit global architecture & performance (passe audit)

Date: 2026-04-08
Périmètre: `BRICK6_CUBE_fonctionnel` (runtime track-aware, audio/mixer, param, seq, UI, modulation, storage)

## Verdict track-aware (état actuel)

**Verdict: partiellement sain mais encore bancal/incomplet pour l’autorité unique runtime.**

Points sains:
- Un modèle `track_runtime_ctx` existe bien (family/type/engine/instance/mix target/bind state), avec bind explicite et quotas d’instances. 
- Le chemin runtime `param_registry_apply_track_value{,_rt_fast}` applique `TONE/MIX/COLORS` via `track_runtime` (engine + instance + mix_target), ce qui va dans la bonne direction.

Points bancals:
- Un chemin parallèle subsiste via `runtime_target.h` (mapping UI family -> filter_target/synth_target hardcodé) utilisé côté param/UI, donc **deux autorités de routing coexistent**.
- `track_runtime_get_effective_param_status()` déclenche un `refresh_all` implicite à chaque check (via `refresh_track`), créant un couplage caché UI/runtime + coût évitable.
- La frontière audio réelle reste 4 tracks physiques (`MAX_TRACKS=4`) alors que UI/SEQ/MIX opèrent à 8: la cohérence “track-aware end-to-end” n’est pas complète.

## Scalabilité vers 16 tracks

### Ce qui scale relativement bien
- Le séquenceur est dimensionné par constantes (`SEQ_TRACK_COUNT`) et les boucles sont propres, donc extension structurelle possible.
- `track_runtime` est déjà table-driven par track (ctx array + binding pass), donc extension conceptuelle faisable.

### Ce qui scale mal / cassera vite
- Multiples plafonds divergents: `MAX_TRACKS=4`, `MIXER_MAX_TRACKS=8`, `UI_TRACK_COUNT=8`, `SEQ_TRACK_COUNT=8`.
- Param/mix legacy codé en dur track0..3 (`apply_mix_track0_* ... track3_*`) non génératif.
- `FILTER_TRACK_TARGET_COUNT=4` et `runtime_target` hardcodent des cibles de filtre limitées.
- Mapping runtime->mix statique `{0..7}` dans `track_runtime.c` (non généralisé).
- L’audio I/O TDM exploite 3 entrées + 1 track interne; passer à 16 exige un redesign I/O (pas juste des `#define`).

### Coûts CPU/RAM attendus à 16
- CPU: coût quasi-linéaire dans mixer (fader/pan/routing/inserts/sends par track et par sample), plus coût synth/FX.
- CPU caché: `mod_lfo_v1` fait des scans de destinations param (`PARAM_COUNT`) + status runtime, multiplié par tracks × LFO.
- RAM: explosion dans `pattern_v1` et caches per-track (`[SEQ_TRACK_COUNT][PARAM_COUNT]`, pool p-lock, blocks snapshot), multipliée presque par 2 en passant 8->16.

## Points de tension perf (priorisés)

### Top goulots potentiels
1. **Mixer hot-path**: sommes par-sample, pan/gain smoothing, inserts/sends, bus FX, plus copies `memcpy` vers tracks 0/1.
2. **Modulation LFO**: validation destination/status + apply param runtime en boucle contrôle; beaucoup d’indirections et checks.
3. **Refresh runtime fréquent**: `track_runtime_refresh_track` fait un pass global (all tracks), potentiellement déclenché souvent.
4. **Rendu synth runtime**: boucle `brick6_render_synth_tracks` + submit externes; DX7 partagé mais logique active-track dépendante.

### Tension moyenne
- Param registry: module massif, nombreux switch, branches et chemins legacy + runtime.
- Séquenceur: plutôt propre mais volume de données p-lock/steps augmente fortement avec le nombre de tracks.

### Probablement OK à ce stade
- Pont `dsp_engine` (indirection simple).
- Layout macro mémoire (`AUDIO_HOT`/`SEQ_STATE_D2`/`DMA_BUFFER`) bien posé comme base.

## Mémoire / cache / localité

Constats:
- Bonne intention de placement mémoire via sections (hot/warm/dma/cold), utile pour STM32H7.
- Mais structures “mixed hot+cold” restent dans des structs monolithiques (ex. état filter/mixer mélange paramètres rarement changés et état sample-rate critique).
- `g_param_runtime_track_values[track][param]` favorise accès “param scan”; selon usage, un layout inversé ou split hot/cold pourrait réduire miss/cache pollution.
- `pattern_v1` duplique de gros blocs (sound + mix + globals + seq + mod), acceptable à 8 mais coûteux à 16.

Opportunités futures:
- Séparer strictement états hot-path audio (par bloc/sample) vs config/UI/persistence.
- Réduire les refresh globaux runtime en “dirty sets” par track/type.
- Préparer des représentations SoA pour chemins massivement vectorisables (mix gains/pans/sends, modulation accumulators).

## Limites structurelles à lever

1. **Autorité de routing dupliquée** (`track_runtime` vs `runtime_target`).
2. **Cardinalités incohérentes** (4/8 partout).
3. **Legacy param/mix hardcodé track0..3**.
4. **Refresh runtime global trop fréquent**.
5. **Frontière audio physique non alignée avec ambition 16 tracks** (I/O + scheduler DSP + mixer buses).

## Refactors structurants (gain/coût)

### Gros gain / coût raisonnable
1. Unifier l’autorité de mapping dans `track_runtime` (supprimer dépendance opérationnelle à `runtime_target`).
2. Introduire une constante unique de cardinalité logique track + adaptateurs explicites (audio phys, mix bus, seq, UI).
3. Remplacer `apply_mix_track0..3_*` par des handlers indexés/table-driven.
4. Passer `track_runtime` à un modèle refresh dirty/incrémental (no full refresh par query).
5. Scinder `param_registry` en couches (routing runtime / filter ui-state / global params) pour réduire branches hot.

### Gros gain / gros chantier
1. Refonte frontière audio pour >4 tracks réels (bus internes + virtualization + voice/synth routing).
2. Rework storage pattern/project pour 16 tracks avec versioning binaire efficace (éviter duplication naïve).
3. Réorganisation data-layout mixer/modulation vers SoA + blocs hot compacts.

### Faible gain / à éviter maintenant
- Micro-optimiser maths isolées sans avoir d’abord supprimé refresh globaux/duplication d’autorité.
- Changer prématurément tous les DSP kernels avant stabilisation du routing et des cardinalités.

## Ce qui vaut le coup maintenant vs plus tard

Maintenant:
- Unification autorité runtime.
- Alignement cardinalités et suppression hardcodes 0..3/0..7.
- Réduction refresh runtime global en check/apply.

Plus tard:
- Optimisations cache agressives/SoA.
- Redesign I/O audio multi-track matériel/virtuel.
- Compression/packing avancé des snapshots pattern/projet.

## Verdict final 16 tracks

**Capacité actuelle à évoluer vers 16 tracks: oui, mais pas en l’état sans prérequis structurants.**

Pré-requis indispensables avant tentative 16:
1. Autorité unique `track_runtime` (plus de chemin caché `runtime_target` opérationnel).
2. Cardinalité unifiée et explicite entre UI/SEQ/MIX/AUDIO.
3. Param/mix runtime génératifs (plus de track0..3 codé en dur).
4. Refresh runtime incrémental.
5. Plan explicite pour frontière audio réelle (>4 tracks) et budget CPU/mémoire par bloc.

Sans ces prérequis, le passage à 16 augmentera surtout la complexité/couplage/coût, avec risque élevé de régression temps réel.


## Note de suivi (passe ciblée étape 1)

- 2026-04-08: le chemin opérationnel `runtime_target` pour la résolution filter target a été retiré des paths UI/param; la résolution passe désormais par `track_runtime` (avec maintien de la règle legacy “synth filter target seulement si une seule track synth”).
- `runtime_target.h` reste présent comme legacy passif/non utilisé, pour éviter une suppression large hors périmètre de cette passe.

## Note de suivi (passe ciblée étape 2)

- 2026-04-08: suppression des refresh implicites dans les getters/checks runtime (`track_runtime_get_effective_param_status`, `track_runtime_resolve_filter_target_track`), avec refresh explicites déplacés dans les call-sites (UI/param/seq/mod).
- `track_runtime_refresh_track()` ne déclenche plus systématiquement un full refresh: il n’exécute `refresh_all` que si le runtime a été invalidé explicitement.
- nouvelle invalidation explicite `track_runtime_invalidate_all()` appelée lors des changements de config track (family/type), afin de garder un modèle plus local/prévisible sans refonte globale.
