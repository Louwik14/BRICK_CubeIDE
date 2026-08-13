# Pattern CONTROL canonical - passe 1

Le codec canonique est en version 2; aucune compatibilite avec la version 1
canonique ou les formats V1 historiques n'est requise.

Invariants de la facade Pattern:

- PLAY conserve les positions 8/8/1 et un masque de presence par champ;
- une application efface l'intention CONTROL precedente avant de poser le DTO;
- chaque owner MOD actif transporte LFO, ENV3, MULTI, SLEW et huit routes;
- une route MOD conserve explicitement son etat enabled et sa destination absente;
- les p-locks conservent leurs flags et utilisent des cles stables pour les enums;
- validation des scopes, plages, budgets, valeurs PLAY et destinations MOD avant apply;
- aucun etat de voix, cache DSP ou runtime AUDIO n'entre dans le DTO.

Les formats disque V1 restent actifs jusqu'a la bascule Project/Pattern/Patch.
