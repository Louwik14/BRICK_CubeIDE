# Z3 — Paramètres, modulation et contrôle

Le registre décrit chaque paramètre par ID, domaine, plage, affichage, persistance et backend. Les paramètres de piste reçoivent un index `0..7`; les paramètres Master sont globaux et n'empruntent aucun slot fictif.

La disponibilité dépend des capacités réelles du moteur projeté. Les p-locks stockent les valeurs complètes par step et utilisent le même catalogue pour les huit pistes. Les destinations incompatibles sont rejetées avant mutation.

LFO, Matrix, ENV3, filtre et VCA gardent une seule autorité de contrôle. External et MIDI conservent leurs restrictions explicites. Aucun paramètre, backend ou état MacroFX n'appartient au produit courant.

## Contrat LFO segmenté et interpolé

`mod_lfo_v1` reste l'autorité unique de phase, de forme, de déclenchement et de
valeur sample-and-hold. La cadence de contrôle par défaut reste la fenêtre
audio `BRICK6_AUDIO_EVENT_GRID_FRAMES` (64 frames dans la configuration
courante), mais une fenêtre est planifiée en segments de longueur variable.

Le planificateur retourne toujours `phase_after` comme autorité de continuité
et produit un contrat `{start, step, frames}`. Les formes sine et triangle sont
interpolées dans leur segment; saw, reverse saw et random-SH segmentent au
wrap; triangle et square segmentent à leur demi-cycle. Square conserve ses
transitions franches. Le mode one-shot force également la frontière de cycle.

La Matrix consomme les bornes start/end une seule fois par fenêtre: les
opérateurs multiplicatifs calculent leurs deux bornes, et le slew avance son
état sur la durée écoulée. Les destinations continues déclarées par le
catalogue reçoivent ensuite ces bornes via leur lissage moteur existant; les
destinations structurelles, discrètes ou à transition marquée restent
appliquées au début de la fenêtre. Aucun setter de destination ni calcul de
forme n'est introduit dans une boucle sample.
