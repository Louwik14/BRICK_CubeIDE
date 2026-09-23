# Persistence de calibration Hall

La calibration Hall est une donnee globale de la machine. Au boot,
`hall_calibration_load()` lit `0:/BRICK/HALL.B6C`, valide son format unique et
son CRC, puis charge les calibrations, le profil utilisateur et les reglages de
velocite dans les structures runtime RAM. Le chemin clavier temps reel ne fait
ensuite aucune lecture SD.

La sauvegarde utilise `HALL.TMP`, `f_sync`, fermeture et le remplacement
transactionnel FatFs existant. Une sauvegarde echouee n'est pas confirmee par
l'UI. Un fichier absent ou invalide conserve l'etat non calibre et le workflow
de calibration. Les anciens formats Flash ne sont ni lus ni migres.

La page de calibration conserve une origine explicite. Une calibration rendue
obligatoire par l'absence de `HALL.B6C` reprend la continuation du boot apres
une sauvegarde reussie. Une calibration ouverte manuellement depuis Settings
revient a Settings. En cas d'echec de sauvegarde, aucune des deux continuations
n'est executee et la page reste en calibration pour retenter l'ecriture.
