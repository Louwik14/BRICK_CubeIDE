# Streaming needs contract

Le streamer Release accepte uniquement la lecture forward, la loop forward et
les préchargements start/loop. Reverse et ping-pong restent des modes du sampler
RAM et ne sont jamais projetés dans le registre streamer.

Chaque voix Classic ou Multi publie un snapshot pointer-free puis remplace, en
une seule opération, son entrée bornée de besoins persistants. Cette entrée est
l'unique autorité logique : au plus six besoins mobiles, complétés par le
pré-socle loop forward. L'arrêt, le vol ou le changement de sample retire
uniquement l'entrée de la génération concernée.

`sample_stream_sequence_build()` est le générateur pur commun Classic/Multi. Il
calcule les pages forward, le retour de loop et supprime les doublons. Les
deadlines sont exprimées en frames de sortie à partir du ratio Q16 et ne servent
qu'au diagnostic et à la calibration. Le scheduler inspecte seulement les
entrées actives et sert leur premier besoin non READY dans l'ordre fixe des
slots Classic puis Multi. Après chaque page, le curseur passe au slot suivant ;
avance, pitch, deadline, backlog et ancienneté ne modifient jamais cet ordre.

Une voix sans besoin chargeable est sautée sans I/O et le curseur poursuit son
passage. Le curseur, le nombre de slots restant dans le passage et le nombre de
passages restant dans le tour survivent à un retour superloop. Avec huit voix
admises, une voix servie voit donc au plus les sept autres voix servies avant
elle. `SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND=N` signifie exactement N
passages complets : `N=2` produit V1..V8 puis V1..V8, jamais deux pages
consécutives pour une même voix et jamais un simple plafond global `8*N`.

Le cache ne crée, ne détruit et ne reconstruit aucun besoin. Il possède seulement
le cycle physique `FREE -> RESERVED -> LOADING -> READY`, ou `FAILED`. Une page
READY correspondant à un besoin d'au moins une voix n'est pas évictable. Une
page partagée n'a pas d'owner : une seule réservation physique suffit, et la
suppression d'une voix ne l'annule pas tant qu'une autre entrée la demande.

Une page LOADING n'est jamais recyclable. La publication READY valide le token,
le slot, la génération de page et le registration epoch ; une complétion tardive
échoue sans rendre la page visible.

Le start preload et le bulk Multi sont des opérations d'amorçage explicites.
Ils utilisent le même cache, le même transport et la même publication par token,
mais ne constituent ni une fenêtre mobile ni une seconde file de demandes.
