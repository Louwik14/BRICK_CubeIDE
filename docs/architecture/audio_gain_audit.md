# Audit des gains audio et Preview

Cet audit est derive du code au 13 octobre 2026. Les valeurs ci-dessous sont
des coefficients calcules, pas des mesures acoustiques. Aucun niveau RMS ou
LUFS n'a ete mesure.

## Chaine commune

La chaine instrument est:

`moteur/reader -> gain de voix et velocite -> filtre/VCA -> gain/pan de piste -> trim nominal 0,125 -> groupe/FX/retours -> bus master/dynamics -> gain master -> compensation de sortie -> saturation S24 codec`.

Le gain de piste `PARAM_MIX_LEVEL` vaut 1 par defaut et couvre 0..2. Le trim
commun `MIXER_TRACK_NOMINAL_TRIM = 0,125` (-18,06 dB) est applique apres le
fader a toutes les pistes. Il constitue la reserve de sommation de huit pistes;
la polyphonie est sommee avant ce trim. Les groupes appliquent leur fader et
leurs FX a un signal deja au niveau nominal. Le master physique est quadratique
(`pot^2`), puis multiplie tout le bus, Preview comprise. La conversion codec
sature a [-1, +1[; le bus float, les sommes polyphoniques et les FX ne sont pas
automatiquement limites.

## Moteurs

| Moteur | Niveau/gains calcules avant trim de piste | Velocite et sommation | Risque identifie |
|---|---|---|---|
| Prism (Braids) | mix des 2 oscillateurs normalise si leur somme de niveaux depasse 1, clamp a +/-1, puis trim moteur 0,30 et volume 0..1 | courbe `0,2 + 0,8*v`; voix poly sommees avant 0,125 | plafond par voix previsible; polyphonie peut depasser le bus |
| Stack | somme de 3 oscillateurs et du bruit, chacun pondere, puis 0,30; aucune normalisation globale | velocite lineaire; voix poly sommees | plusieurs sources a plein niveau peuvent depasser Prism avant le fader |
| FM | bus MSFA Q24 converti par `1/2^24`; pas de trim moteur ni clamp | velocite appliquee par sensibilite de chaque operateur, pas comme VCA global; carriers et voix peuvent se sommer | niveau fortement dependant de l'algorithme/patch; marge fournie seulement par 0,125 |
| Wavetable | somme de 2 oscillateurs, gains de balance, volume 0..1, velocite, puis `0,30 * 0,4216965 = 0,126509` | velocite lineaire; voix poly sommees | centre de balance peut sommer deux formes; reserve commune ensuite |
| Acid | modele non lineaire, sortie divisee par 32768 puis clamp +/-1 | velocite ignoree; accent pilote le niveau | saturation interne explicite, caractere preserve |
| Drum MD | chaque modele applique enveloppes et velocite lineaire, puis hard-clip +/-1 | monophonique par instance | transitoires volontairement clippees dans le moteur |
| Classic STREAM | amplitude WAV conservee, puis `SAMPLER_GAIN` (defaut 1), velocite `v/127`, VCA de piste et fader | une voix de piste; Clip force son gain de velocite a 1 | un fichier proche de 0 dBFS atteint 0,125 au bus a vel 127/gains unitaires |
| Multi | amplitude WAV, gain Multi et gain de voix; velocite lineaire seulement pour une zone a couche unique, sinon la selection de couche porte la dynamique; VCA par voix | voix Multi sommees avant le trim de piste | empilement de zones/voix peut depasser le nominal |
| Entrees externes | float issu du S24, puis meme filtre/VCA/fader/trim 0,125 | sans velocite | niveau depend du gain analogique et de `POST_GAIN` |

Prism, Stack et Wavetable ont des trims historiques explicites. Ils ne sont pas
directement comparables aux modeles FM/303 sur la seule valeur de crete: le
contenu harmonique, les enveloppes, le nombre de carriers/sources et la
saturation modifient fortement le niveau percu. Aucun coefficient moteur n'a
donc ete modifie sans campagne de mesure.

## Preview

La Preview de patch utilise les instances moteur dediees, la meme velocite que
la note de Preview (100 par defaut), le VCA/filtre et les FX du patch, puis
applique deja le trim nominal 0,125. Elle contourne volontairement le fader,
le pan, les sends et les groupes de la piste, puis rejoint le bus avant la
dynamique master et le gain master.

La Preview du Sample Browser suivait un autre chemin: decode WAV ->
resampling lineaire -> gain Preview (100 % par defaut) -> bus master. Elle
conservait correctement l'amplitude du fichier, mais omettait le trim nominal
0,125. Le meme sample pleine echelle etait donc 8 fois plus fort (+18,06 dB)
que sa lecture STREAM a velocite 127, `SAMPLER_GAIN=1` et fader=1. C'est la
cause demontree du symptome. Le correctif applique le trim commun au point
d'injection, sans ajouter normalisation, limiteur ou traitement dynamique. Le
reglage Preview 0..100 % reste un ajustement utilisateur additionnel.

## Fichiers WAV et Recorder

Le format canonique est stereo FLOAT32/48 kHz. Un fichier deja canonique est
copie bit pour bit. PCM16/24/32 est converti avec les diviseurs pleine echelle
usuels; FLOAT32 est relu sans changement de gain. Le resampling est lineaire et
n'ajoute pas de normalisation. Le Recorder copie directement ses buffers float
dans un WAV IEEE FLOAT32, sans attenuation ni normalisation. Les samples
importes et enregistres conservent donc leur amplitude source, y compris les
valeurs FLOAT32 hors [-1, 1]. Les differences de niveau entre fichiers sont
naturelles; elles ne sont pas un defaut du convertisseur. Les valeurs hors
plage peuvent saturer seulement a la sortie codec (ou dans un traitement qui
sature explicitement).

## Reference et suite recommandee

La reference interne retenue est le signal d'une piste apres le trim 0,125:
un signal source a crete 1, avec gain de voix, VCA et fader unitaires, donne une
crete nominale de 0,125 (-18,06 dBFS) sur le bus. Cela laisse 18,06 dB pour les
huit pistes, mais pas une garantie de non-clipping lorsque polyphonie, fader
jusqu'a 2, groupes, retours ou FX ajoutent du gain.

Pour harmoniser davantage les moteurs, mesurer hors FX a velocite 127 puis 100,
fader 1 et une voix: peak, RMS fenetre et facteur de crete sur un jeu fixe de
patches representatifs. Ajuster ensuite seulement les trims de sortie moteur,
avec compatibilite de presets explicite. Priorites de mesure: FM selon nombre
de carriers, Stack avec 1 puis 3 oscillateurs, Acid avec accent et
resonance, puis Drum par modele. Aucune valeur artistique massive n'est
modifiee par cet audit.

## Impact du correctif

Le correctif ajoute une multiplication float par bloc actif de Preview SD (le
coefficient est calcule avant la boucle). RAM inchangee; Flash: impact
negligeable, determine par le build Release. Aucun changement de persistance,
de migration, de Streamer, de Recorder ou de format de fichier.
