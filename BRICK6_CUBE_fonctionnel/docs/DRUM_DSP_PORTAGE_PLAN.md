# Audit de preuve technique — dépendances réelles du rendu sonore (`md-drum-synth-main`)

## 0) Méthode de preuve utilisée (dans cette base exacte)

Audit réalisé sur les fichiers présents dans `BRICK6_CUBE_fonctionnel/md-drum-synth-main` avec:

- inspection des includes réels (`rg '^#include' -n ...`);
- lecture des chemins d’appel `Process()/Trigger()/Init()`;
- vérification des symboles réellement appelés (fonctions, constantes, types);
- distinction stricte:
  - **A = requis compilation**,
  - **B = requis fidélité sonore**,
  - **C = non requis pour ces moteurs**.

> Important: ce document répond à “ce qui est prouvé dans cette copie”. Il ne généralise pas à tout Plaits/DX en dehors des chemins effectivement utilisés ici.

---

## 1) Tableau global de preuve des dépendances

Légende colonnes:
- **Direct** = inclus directement par un moteur drum (`*.cpp/*.h`) ou `DrumModel.h`.
- **Indirect** = traversé via chaîne d’inclusion depuis les moteurs.
- **Req. comp.** = requis pour compiler les moteurs présents (dans leur état actuel).
- **Req. fidélité** = requis pour préserver le comportement sonore réel observé dans ces moteurs.
- **Excluable sans impact sonore prouvé** = oui/non, avec justification.

| Fichier | Direct | Indirect | Req. comp. | Req. fidélité | Excluable sans impact sonore prouvé | Justification de preuve |
|---|---:|---:|---:|---:|---:|---|
| `DrumModel.h` | Oui | Non | Oui | Partiel | Non (tant que classes héritent) | Base virtuelle héritée par tous les moteurs (`Init/Trigger/Process/RenderControls`, save/load). |
| `TRXBassDrum.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` calcule le signal (env, sine, noise, clip). |
| `TRXClaves.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` = 2 osc + env + clip. |
| `TRXHiHat.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` = bruit métallique + LP/HP + env/gap. |
| `TRXSnareDrum.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` = 2 osc + noise HP + snap + clip. |
| `FmKickModel.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` appelle `plaits::fm::RenderOperators`. |
| `FmSnareModel.cpp/.h` | Oui | Non | Oui | Oui | Non | `Process()` appelle `RenderOperators` + bruit + HPF. |
| `FmTomModel.cpp/.h` | Oui | Non | Oui | Oui | Non | FM sinus direct + enveloppes expo dans `Process()`. |
| `FmRimshotModel.cpp/.h` | Oui | Non | Oui | Oui | Non | FM manuel + mix + HPF dans `Process()`. |
| `FmClapModel.cpp/.h` | Oui | Non | Oui | Oui | Non | séquence clap + FM + HPF dans `Process()`. |
| `FmCowbellModel.cpp/.h` | Oui | Non | Oui | Oui | Non | FM manuel 2 carriers + double decay. |
| `FmCymbalModel.cpp/.h` | Oui | Non | Oui | Oui | Non | 4 paires FM + HPF + sustain/decay. |
| `mi/operator.h` | Oui (`FmKick/FmSnare`) | Non | Oui (pour ces 2 moteurs) | Oui | Non | `RenderOperators` appelé directement depuis `Process()`. |
| `mi/sine_oscillator.h` | Non | Oui (via `operator.h`) | Oui | Oui | Non | `RenderOperators` utilise `SinePM`, défini ici. |
| `mi/dsp.h` | Non | Oui (via `operator.h`/`sine_oscillator.h`) | Oui | Oui | Non | `SinePM`/interpolation utilisent fonctions `Interpolate*`. |
| `mi/parameter_interpolator.h` | Non | Oui (via `sine_oscillator.h`) | Oui (header path) | Non prouvé (chemin courant) | Incertain | Utilisé par classes `SineOscillator/FastSineOscillator`; `RenderOperators(size=1)` n’appelle pas ces méthodes ici. |
| `mi/resources.h` | Non | Oui (via `sine_oscillator.h`) | Oui | Oui | Non | `lut_sine` utilisée par `SinePM` appelée par `RenderOperators`. |
| `mi/rsqrt.h` | Non | Oui (via `sine_oscillator.h`) | Oui (header path) | Non prouvé (chemin courant) | Incertain | Sert `FastSineOscillator`; ce chemin n’est pas appelé dans les moteurs audités. |
| `mi/stmlib.h` | Non | Oui (via `dsp.h` et autres) | Oui | Oui indirect | Non | Définitions types/macros requises dans la chaîne DSP MI. |
| `mi/resources.cc` | Non | Non | Non (dans cet usage) | Non | Oui | Aucun moteur n’appelle symboles nécessitant ce TU; `lut_sine` est déjà dans `resources.h`. |
| `mi/algorithms.h/.cc` | Non | Non | Non | Non | Oui | Pas d’include depuis moteurs/chaîne utilisée par moteurs. |
| `mi/dx_units.h/.cc` | Non | Non (pour ces moteurs) | Non | Non | Oui | Aucun include actif depuis moteurs actuels. |
| `mi/envelope.h` | Non | Non | Non | Non | Oui | Non référencé dans chaînes actives. |
| `mi/lfo.h` | Non | Non | Non | Non | Oui | Non référencé dans chaînes actives. |
| `mi/patch.h` | Non | Non | Non | Non | Oui | Non référencé dans chaînes actives. |
| `mi/voice.h` | Non | Non | Non | Non | Oui | Non référencé par moteurs actuels. |
| `CustomControls.h/.cpp` | Oui (dans `Fm* .cpp`) | Non | Oui (état actuel) | Non | Oui (si découplage UI) | Utilisé seulement dans `RenderControls()`, pas dans `Process()`. |
| `imgui.h` | Oui (`RenderControls`) | Non | Oui (état actuel) | Non | Oui (si découplage UI) | Aucun appel ImGui dans chemins audio `Process()`. |
| `main.cpp` | Non (moteurs) | Non | Non pour compiler classes moteur seules | Non | Oui | Hôte desktop (OpenGL/RtAudio/UI), pas nécessaire au DSP pur. |
| `glad.*`, `khrplatform.h`, `stb_image.h`, `resources/background*`, médias | Non | Non | Non | Non | Oui | Dépendances visuelles/demo uniquement. |

---

## 2) Preuve moteur par moteur

## 2.1 `TRXBassDrum.cpp/.h`

- **Dépendances directes**
  - `TRXBassDrum.h`, `imgui.h`, `<cmath>`, `<algorithm>`.
  - header: `DrumModel.h`.
- **Dépendances indirectes réellement traversées**
  - `DrumModel.h` -> `<iostream>`.
- **Fonctions/symboles critiques pour le son (preuve)**
  - `std::exp` (décroissance `env`, `rampEnv`),
  - `std::sin` via `sine()`,
  - `std::tanh` (harmonics + clip),
  - `rand()/RAND_MAX` (burst bruit),
  - `kSampleRate=48000`.
- **Dépendances purement UI/desktop**
  - `imgui.h` (uniquement `RenderControls`).
- **Remplaçables seulement avec validation audio**
  - RNG, `exp/sin/tanh`, valeur SR.
- **Verdict fidélité sonore**
  - A: `TRXBassDrum.*`, `<cmath>`, `DrumModel.h`.
  - B: `exp/sin/tanh/rand` + ordre de calcul.
  - C: `imgui.h`, `<algorithm>` (pas de symbole utilisé ici).

## 2.2 `TRXClaves.cpp/.h`

- **Dépendances directes**: `TRXClaves.h`, `imgui.h`, `<cmath>`, `<algorithm>`; header -> `DrumModel.h`.
- **Indirectes**: `<iostream>` via `DrumModel.h`.
- **Critiques son**: `std::exp`, `std::sin`, `std::tanh`, `kSampleRate`.
- **UI**: ImGui uniquement.
- **Remplaçables avec validation**: trig/exp/clip approx.
- **Verdict**
  - A: fichiers moteur + `<cmath>` + `DrumModel.h`.
  - B: équation mix 2 osc, env, clip.
  - C: `imgui.h`, `<algorithm>` non utilisé.

## 2.3 `TRXHiHat.cpp/.h`

- **Dépendances directes**: `TRXHiHat.h`, `imgui.h`, `<cmath>`; header: `DrumModel.h`, `<array>`, `<random>`.
- **Indirectes**: `<iostream>` via `DrumModel.h`.
- **Critiques son**
  - `std::default_random_engine`, `std::uniform_real_distribution<float>`,
  - `std::exp` (LPF/HPF coeffs + env),
  - phases statiques `phase[6]`, fréquences fixes `[306,512,551,743,826,900]`,
  - logique `gap` + `fadeTime`.
- **UI**: ImGui (`RenderControls`).
- **Remplaçables avec validation**: RNG/seed policy, filtres, expo.
- **Verdict**
  - A: moteur + `<random>` + `<cmath>` + `DrumModel.h`.
  - B: RNG/filtres/enveloppe/gap.
  - C: `imgui.h`, `<array>` non utilisé explicitement.

## 2.4 `TRXSnareDrum.cpp/.h`

- **Dépendances directes**: `TRXSnareDrum.h`, `imgui.h`, `<cmath>`, `<algorithm>`; header -> `DrumModel.h`.
- **Indirectes**: `<iostream>`.
- **Critiques son**
  - `std::exp` (amp/snap env),
  - `std::sin`, `std::tanh`,
  - `rand()` pour `snapNoise` et `rawNoise`,
  - HPF discret (`hp_a`, états `hp_x/hp_y`).
- **UI**: ImGui.
- **Remplaçables avec validation**: RNG, expo/trig, HPF formule.
- **Verdict**: A moteur+cmath; B chemin tonal+noise+HP+clip; C imgui, algorithm non utilisé.

## 2.5 `FmKickModel.cpp/.h`

- **Dépendances directes**
  - header: `DrumModel.h`, `mi/operator.h`.
  - cpp: `<cmath>`, `<imgui.h>`, `CustomControls.h`.
- **Indirectes réellement traversées**
  - `mi/operator.h` -> `mi/sine_oscillator.h` -> `mi/dsp.h`, `mi/parameter_interpolator.h`, `mi/rsqrt.h`, `mi/resources.h` -> `mi/stmlib.h`.
- **Critiques son (preuve d’appel)**
  - `plaits::fm::RenderOperators<2,...>` appelé dans `Process()`;
  - `Operator::Reset`, états `fb_state`, enveloppes itératives `amp_env/mod_env/freq_env`.
  - `ratio_index`/`ratios` impactent directement `mod_freq`.
- **UI/desktop**
  - ImGui + `CustomControls` seulement dans `RenderControls()`.
- **Remplaçables avec validation**
  - toute réimplémentation de `RenderOperators`/`SinePM`/`lut_sine`, quantification feedback.
- **Verdict**
  - A: moteur + chaîne `mi` active + `<cmath>` + UI headers (état actuel).
  - B: chaîne FM MI active et constantes/ratios.
  - C: `CustomControls`/ImGui non sonores.

## 2.6 `FmSnareModel.cpp/.h`

- **Dépendances directes**: `DrumModel.h`, `mi/operator.h`, `CustomControls.h`, `<cmath>`, `<cstdlib>`, `<imgui.h>`.
- **Indirectes**: même chaîne MI que `FmKickModel`.
- **Critiques son**
  - 2 appels `RenderOperators` (mod puis carrier externe),
  - `rand()` bruit blanc,
  - HPF discret (`alpha`, `x_prev/y_prev`),
  - enveloppes itératives.
- **UI**: `CustomControls`/ImGui seulement `RenderControls()`.
- **Remplaçables avec validation**: FM MI, RNG, HPF, expo implicite par constantes de decay.
- **Verdict**: A chaîne MI+cmath+cstdlib; B FM+RNG+HPF; C UI.

## 2.7 `FmTomModel.cpp/.h`

- **Directes**: `DrumModel.h`, `CustomControls.h`, `<cmath>`, `<imgui.h>`.
- **Indirectes**: `<iostream>` via `DrumModel.h`.
- **Critiques son**
  - `std::expf` (3 enveloppes),
  - `std::sin`, `WrapPhase`, `start_phase`.
- **UI**: `CustomControls`/ImGui.
- **Remplaçables avec validation**: expf/sin approximés, wrap.
- **Verdict**: A moteur+cmath; B enveloppes/FM/phase; C UI.

## 2.8 `FmRimshotModel.cpp/.h`

- **Directes**: `DrumModel.h`, `CustomControls.h`, `<cmath>`.
- **Indirectes**: `<iostream>`.
- **Critiques son**
  - `std::expf`, `std::sin`, modulateur fixe 1000Hz,
  - HPF (`alpha`, états).
- **UI**: `CustomControls` seulement.
- **Remplaçables avec validation**: expf/sin/HPF.
- **Verdict**: A moteur+cmath; B mod fixe+double carrier+HP; C UI.

## 2.9 `FmClapModel.cpp/.h`

- **Directes**: `DrumModel.h`, `CustomControls.h`, `<cmath>`, `<imgui.h>`.
- **Indirectes**: `<iostream>`.
- **Critiques son**
  - `std::expf`, `std::sin`, feedback `bm`,
  - logique `clap_count/clap_interval/clap_stage/clap_timer` (timing structurel),
  - HPF.
- **UI**: `CustomControls`/ImGui.
- **Remplaçables avec validation**: scheduler clap, expf/sin, HPF.
- **Verdict**: A moteur+cmath; B structure temporelle + FM + HPF; C UI.

## 2.10 `FmCowbellModel.cpp/.h`

- **Directes**: `DrumModel.h`, `CustomControls.h`, `<cmath>`, `<imgui.h>`.
- **Indirectes**: `<iostream>`.
- **Critiques son**
  - `std::expf`, `std::sin`,
  - ratio fixe `fbB = fbA * 1.48f`,
  - mix enveloppes `Ab1/Ab2`.
- **UI**: `CustomControls`/ImGui.
- **Remplaçables avec validation**: expf/sin, ratio policy.
- **Verdict**: A moteur+cmath; B ratio+enveloppes+FM; C UI.

## 2.11 `FmCymbalModel.cpp/.h`

- **Directes**: `DrumModel.h`, `CustomControls.h`, `<cmath>`.
- **Indirectes**: `<iostream>`.
- **Critiques son**
  - `std::expf`, `std::sin`,
  - ratios fixes `{1.0,1.411,1.8,2.7}`,
  - somme 4 paires, HPF, sustain bias.
- **UI**: `CustomControls`.
- **Remplaçables avec validation**: expf/sin, HPF, ordre d’accumulation float.
- **Verdict**: A moteur+cmath; B multi-FM+HP; C UI.

## 2.12 `DrumModel.h`

- **Directes**: `<iostream>`.
- **Critiques son**
  - contrat de cycle de vie (`Init/Trigger/Process`) structurellement critique.
- **Non sonore**
  - `RenderControls()`;
  - `saveParameters/loadParameters` (persistence, pas calcul audio direct).
- **Verdict**
  - A: requis compilation tant que tous moteurs héritent exactement cette interface.
  - B: partiel (seulement via contrat runtime `Init/Trigger/Process`).
  - C: `iostream` non sonore mais requis ici pour signatures.

---

## 3) Focus spécifique `mi/*` (preuve détaillée)

## 3.1 Chaîne prouvée depuis `Process()` FM

Chemin prouvé:
1. `FmKickModel::Process()` / `FmSnareModel::Process()` appelle `plaits::fm::RenderOperators`.
2. `RenderOperators` est défini dans `mi/operator.h`.
3. `RenderOperators` appelle `SinePM(...)`.
4. `SinePM` est défini dans `mi/sine_oscillator.h`.
5. `SinePM` lit `lut_sine[]` provenant de `mi/resources.h`.
6. Interpolation/indexing dépend de types et utilitaires (`mi/dsp.h`, `mi/stmlib.h`).

=> Cette chaîne est **B (fidélité)** pour `FmKickModel` et `FmSnareModel`.

## 3.2 Matrice `mi/*` demandée (oui/non)

| Fichier `mi/*` | Utilisé direct | Utilisé indirect | Requis compilation | Requis comportement sonore réel (moteurs présents) | Excluable sans impact sonore | Justification de preuve |
|---|---:|---:|---:|---:|---:|---|
| `operator.h` | Oui | Non | Oui | Oui | Non | Appel explicite `RenderOperators` dans `Process()` FM. |
| `sine_oscillator.h` | Non | Oui | Oui | Oui | Non | `RenderOperators` -> `SinePM`. |
| `dsp.h` | Non | Oui | Oui | Oui | Non | Utilitaires interpolation employés par chaîne sine/FM. |
| `parameter_interpolator.h` | Non | Oui | Oui (header chain) | Non prouvé ici | Incertain | Référencé par classes oscillator non appelées dans chemins actuels FM sample-by-sample. |
| `resources.h` | Non | Oui | Oui | Oui | Non | `lut_sine` consommée dans `SinePM`. |
| `resources.cc` | Non | Non | Non | Non | Oui | Pas de symbole requis par chemins moteur observés. |
| `rsqrt.h` | Non | Oui | Oui (header chain) | Non prouvé ici | Incertain | Utilisé par `FastSineOscillator` non appelé dans moteurs audités. |
| `stmlib.h` | Non | Oui | Oui | Oui indirect | Non | Base types/macros traversée par chaîne active. |
| `algorithms.h/.cc` | Non | Non | Non | Non | Oui | Absence de référence/include depuis moteurs. |
| `dx_units.h/.cc` | Non | Non | Non | Non | Oui | Absence de référence/include depuis moteurs. |
| `envelope.h` | Non | Non | Non | Non | Oui | Non référencé. |
| `lfo.h` | Non | Non | Non | Non | Oui | Non référencé. |
| `patch.h` | Non | Non | Non | Non | Oui | Non référencé. |
| `voice.h` | Non | Non | Non | Non | Oui | Non référencé. |

## 3.3 Cas prudents (incertains mais balisés)

- `parameter_interpolator.h` et `rsqrt.h`:
  - **prouvé compilation**: oui (inclusion via `sine_oscillator.h`).
  - **prouvé impact sonore dans chemins actuels**: non démontré.
  - **prudence**: ne pas les retirer tant que la chaîne MI n’est pas refactorisée proprement/testée.

---

## 4) Classement final demandé

## 4.1 À conserver absolument (preuve forte A+B)

- Tous les moteurs et leurs équations DSP:
  - `TRX*`, `Fm*`, `DrumModel.h` (au moins contrat `Init/Trigger/Process`).
- Chaîne MI réellement appelée:
  - `mi/operator.h`, `mi/sine_oscillator.h`, `mi/resources.h`, `mi/dsp.h`, `mi/stmlib.h`.
- Dépendances math/RNG réellement invoquées dans `Process()`.

## 4.2 À conserver tant qu’aucun remplacement validé n’existe

- RNG (`rand`, `default_random_engine`) et policy seed.
- `sin/exp/tanh` exacts utilisés actuellement.
- filtres discrets (formes HP/LP) et ordre des opérations.
- `mi/parameter_interpolator.h`, `mi/rsqrt.h` tant que chaîne include MI inchangée.

## 4.3 Excluable sans impact sonore prouvé (catégorie C)

- UI desktop: `imgui.h`, `CustomControls.*`, `RenderControls` path.
- hôte/build/assets desktop: `main.cpp`, `glad.*`, `khrplatform.h`, `stb_image.h`, ressources images/audio.
- `mi` non référencé par chemins moteurs actuels:
  - `algorithms.*`, `dx_units.*`, `envelope.h`, `lfo.h`, `patch.h`, `voice.h`, `resources.cc`.

## 4.4 Incertain / à vérifier explicitement

- Impact sonore concret de retirer `parameter_interpolator.h` et `rsqrt.h` **sans toucher API** (actuellement dépendances de chaîne, pas de preuve d’appel runtime dans ces moteurs).
- Effet exact de tout changement RNG/approx math sur la fidélité perçue.

Vérification recommandée pour lever incertitudes:
1. instrumenter build pour tracer symboles réellement instanciés (`nm`, map linker);
2. snapshot WAV A/B par moteur avant/après modification ciblée;
3. comparer enveloppes + spectres + écoute ABX.

---

## 5) Conclusion courte

### Déjà prouvé
- Les chemins audio des 11 moteurs sont identifiés et localisés dans `Process()`.
- La chaîne MI réellement impliquée dans le son (`FmKick/FmSnare`) est prouvée jusqu’à `SinePM` et `lut_sine`.
- Les dépendances UI/build non sonores sont isolées.

### Reste à confirmer dans cette base exacte
- Si `parameter_interpolator.h` et `rsqrt.h` peuvent être retirés sans refactor de `sine_oscillator.h` ni impact build/runtime.
- Niveau de dérive sonore acceptable pour d’éventuels remplacements embarqués (RNG/trig/exp/filtres), à valider par protocole A/B.
