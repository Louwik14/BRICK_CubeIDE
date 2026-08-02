# Z3 — Paramètres, modulation et contrôle

Le registre décrit chaque paramètre par ID, domaine, plage, affichage, persistance et backend. Les paramètres de piste reçoivent un index `0..7`; les paramètres Master sont globaux et n'empruntent aucun slot fictif.

La disponibilité dépend des capacités réelles du moteur projeté. Les p-locks stockent les valeurs complètes par step et utilisent le même catalogue pour les huit pistes. Les destinations incompatibles sont rejetées avant mutation.

LFO, Matrix, ENV3, filtre et VCA gardent une seule autorité de contrôle. External et MIDI conservent leurs restrictions explicites. Aucun paramètre, backend ou état MacroFX n'appartient au produit courant.
