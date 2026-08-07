# Contrat de temps audio du streamer

Le temps du streamer est un compteur monotone 64 bits de frames de sortie. L'IRQ
audio en est l'unique écrivain et ne fait que l'avancer puis réveiller le tasklet.
Les lecteurs main-context utilisent une séquence anti-tearing. Le transport ou
le séquenceur ne peuvent ni reculer ni remettre à zéro cette horloge.

Chaque besoin de voix porte une deadline absolue de consommation. La distance
source est convertie en frames de sortie avec le pas Q16 avant d'être ajoutée au
temps courant. Chaque remplacement de snapshot recalcule l'horizon borné de la
voix. Les pages physiques ne fusionnent ni ne possèdent ces deadlines, et le
scheduler choisit la voix la moins avancée plutôt qu'une file globale par date.

La commande `sample_stream_io_command_t` et le token de chargement sont
transportables et sans pointeur. La trace Release est un ring borné avec séquence,
frame audio, cycle DWT, source/voix/génération et identité clé/page. Les événements
causaux stables relient sélection, chargement, publication et miss de consommation.
