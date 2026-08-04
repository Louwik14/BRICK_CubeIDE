# Démarrage audio Low-Cost robuste

## Cause et correction

Le TLV320 est configuré alors que le TX SAI fournit déjà les horloges nécessaires.
Une activation initiale pouvait laisser l'interface série dans une phase incorrecte.
La correction conservée est la frontière validée sur matériel une fois le codec prêt:

1. arrêt RX;
2. arrêt TX;
3. démarrage TX;
4. démarrage RX.

Cette séquence utilise uniquement les API HAL normales. Elle n'ajoute ni `CLRFR`,
ni purge explicite, ni remise à zéro des buffers, ni second accès codec, ni
modification du clock tree.

## Séquence de boot

Le backend Low-Cost exécute au maximum trois tentatives complètes:

1. arrêt des flux résiduels;
2. démarrage TX silence pour fournir MCLK/BCLK/WCLK;
3. attente de 1 ms;
4. détection, reset logiciel et configuration complète du TLV320;
5. vérification des écritures critiques et des états ready DAC/ADC/sorties;
6. démarrage RX initial;
7. frontière finale arrêt RX, arrêt TX, démarrage TX, démarrage RX;
8. publication de l'état `AUDIO_INIT_READY`.

Une tentative échouée est suivie d'un délai de 10 ms. `TLV320AIC3204_InitDefault()`
reprend chaque tentative depuis la détection du périphérique puis effectue un reset
logiciel complet. Les transactions I²C et les readbacks critiques propagent leurs
erreurs. Aucune récupération destructive du périphérique I²C n'est effectuée:
les appels HAL synchrones libèrent normalement le handle sur erreur et aucune
preuve d'un handle restant bloqué n'a été observée.

Après le troisième échec, RX et TX sont arrêtés, l'état devient
`AUDIO_INIT_ERROR` et le code d'erreur détaillé reste disponible dans
`board_audio_boot_diag_t`.

## Autorité d'état

L'état public suit:

`AUDIO_INIT_NOT_STARTED -> AUDIO_INIT_CODEC -> AUDIO_INIT_SAI_SYNC -> AUDIO_INIT_READY`

Toute sortie sans succès mène à `AUDIO_INIT_ERROR`. Les callbacks RX ne lancent
le moteur audio et ne produisent ses ticks qu'en état READY. En erreur, un tick
de secours basé sur `HAL_GetTick()` maintient les contrôles et l'OLED actifs;
l'écran affiche `AUDIO INIT ERROR`, le code de boot et demande un redémarrage.

## Validation matérielle requise

- au moins 50 boots Low-Cost normaux;
- plusieurs débranchements/rebranchements USB rapides;
- aucune réapparition du son bruité;
- aucun boot silencieux non signalé;
- vérification de l'écran d'erreur lorsque le TLV320 est volontairement inaccessible.
