# Fiabilite a froid du streamer

Le backend contigu conserve une lecture SD multibloc par page physique. Le
fallback FatFs utilise par defaut des sous-lectures de 16 Kio. Aucun
regroupement supplementaire de pages n'est active sans mesure materielle
demonstrant un meilleur temps maximal ; le debit moyen seul ne suffit pas.

## Trace d'evenements stable

Le firmware Release expose `g_sample_stream_event_trace`, un anneau fixe de
128 evenements avec ABI stable, sequence monotone et compteur de debordement.
Chaque evenement porte la frame audio absolue, le cycle DWT, l'identite
source/page, la generation de voix et le resultat. `cause_sequence` relie le
service, la selection, l'I/O, la publication et le miss de consommation ; la
causalite d'un underrun est donc lisible avec le seul vocabulaire actif.

La trace n'utilise ni texte, ni fichier, ni UART, ni USB, ni affichage. Les
cycles DWT restent disponibles pour les mesures physiques. La surcharge
volontaire se mesure avec `BRICK6_STREAM_BENCH=1` ; elle est independante de la
trace evenementielle et du firmware produit.
