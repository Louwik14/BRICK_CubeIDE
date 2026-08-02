# Réverbération Mutable : stabilité et géométries

Le chemin de production somme les sends stéréo en mono puis appelle la variante
mono-vers-stéréo de RevB. La variante stéréo en place reste cohérente, mais n'est
pas appelée par le mixeur courant.

`FORMAT_32_BIT` stocke bien les 32768 éléments du délai sous forme de `float`
(buffer aligné sur 32 octets en `AUDIO_WARM`, 128 Kio). Les réservations maximales
occupent 23528 échantillons en mode TBD et 21617 en mode Mutable ; les index sont
masqués sur la capacité puissance de deux. Les excursions des LFO (±54,42177 ou
±50 échantillons, ±43,53742 ou ±40) restent à l'intérieur des lignes concernées.
Aucun accès hors limites, état non initialisé ou défaut de taille/alignment n'a
été identifié.

Le diagnostic historique est partiellement confirmé : le format float, à la
différence du format 16 bits Mutable, ne saturait pas les écritures et pouvait
donc conserver indéfiniment un Inf/NaN produit en amont. En revanche, le chemin
actuel ne pousse déjà pas la diffusion à 1 : `SIZE` donne 0,45..0,90. `DECAY`
donne 0,20..0,98 avant le mapping interne `0,01 + 0,97*x`, soit 0,204..0,9606.
`DAMP` est intentionnellement inversé avant `set_lp` : 0 donne un coefficient LP
de 1 (aucun amortissement), 1 un coefficient proche de 0 (amortissement maximal).
Le gain d'entrée vaut 1. Les smoothers restent bornés par leurs cibles 0..1.

La diffusion est désormais bornée à 0..0,90 au niveau du moteur après tous les
mappings, et les coefficients LP/filtres à 0..1. Les écritures float et états
one-pole non finis sont remis à zéro ponctuellement : aucun balayage permanent ni
saturation de la boucle n'est ajouté, et une contamination ne peut plus persister.

`MUTABLE=OFF` (défaut) conserve exactement les longueurs TBD redimensionnées à
48 kHz et les interpolations 6815,2383±54,42177 / 4854,4219±43,53742.
`MUTABLE=ON` sélectionne les longueurs Rings 48 kHz et 6261±50 / 4460±40.
Deux instanciations d'une même fonction template évitent la divergence du DSP.
Le changement de mode efface le buffer partagé et tous les états internes et de
sortie via `Clear`; le paramètre est un booléen global persistant et non destiné à
la modulation rapide. L'effacement n'a lieu que sur une action de configuration,
pas dans le traitement audio périodique.
