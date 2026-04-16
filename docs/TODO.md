# TODO trié par dépendances

## 1. Base track / UI
~~- Créer le type de track MIDI et son fonctionnement~~
 

~~- Mettre en place la track Hybrid et son fonctionnement~~  
  
- Retravailler l’UI et essayer d’avoir un ensemble plus joli, avec une vraie identité visuelle, en s’éloignant de celle d’Elektron  
  Piste : à traiter après stabilisation des ensembles et workflows principaux, pour éviter de remaquetter une UI encore mouvante.

## 2. Routing / mix
- Créer un système de gestion de sélection de send  
  Piste : faire un deuxième menu `Mix`. Si on appuie une fois sur `Mix`, on arrive sur `Mix 1/X` qui est l’ensemble actuel. Si on rappuie sur `Mix`, on va sur `Mix 2/X`, où `X` est le nombre d’ensembles mix.

- Trouver où placer le Master Comp et son paramétrage  
  Piste : possiblement dans les futurs `Mix/X`, mais peut-être qu’il faudra une UI spéciale. Il faut trouver comment gérer intuitivement les paramètres du comp, quelles pistes iront dedans, laquelle déclenchera éventuellement le sidechain, etc.  
  Autre piste : garder l’UI classique et ajouter, dans la page `Crunch` de l’ensemble `Colors`, un 4e paramètre pour le niveau d’envoi vers le comp.  
  À étudier avec le manuel du Digitakt pour voir comment eux le gèrent.

- Pouvoir transformer une entrée/sortie en insert, et réfléchir à comment le gérer en UI / workflow  
  Piste : définir clairement si l’insert est une propriété de routing global, d’une track, ou d’un slot d’effet, puis trouver une exposition UI cohérente avec le système actuel sans casser la logique track-aware.

## 3. Tracks dépendantes du routing / mix
- Créer le type de track Looper et son fonctionnement  
  Piste : garder les paramètres sonores universels comme `VCA`, `Mix`, `Colors`, mais remplacer `Tone` par un paramétrage propre au looper. Il faut aussi réfléchir à comment choisir quelles pistes vont dans le looper, avec un système intuitif et cohérent avec le workflow actuel.

-~~Résoudre le bug `Rec Len` du rec buffer~~
  
- Ajouter les paramètres de décalage de lecture pour le rec buffer afin de pouvoir caller la loop comme sur l’Octatrack  
  Piste : ajouter un offset de lecture cohérent avec la logique actuelle du buffer, sans casser l’alignement rec / loop / playback.

- Réfléchir à l’usage du mode `KBD` pour le rec buffer  
  Piste : définir un vrai mode de jeu dédié plutôt qu’un simple recyclage du `KBD` classique.

- Étudier un ou plusieurs modes de jeu dédiés pour le rec buffer  
  Pistes :  
  - mode loop comme contrôleur DJ  
  - mode slice où la loop est découpée et où chacun des 16 pads saute vers un des 16 points de la loop  
  - réfléchir à la coexistence ou à la fusion entre mode DJ loop et mode slice

## 4. Infra sample

## 5. Tracks sample
- Créer la track sampleur one shot et son fonctionnement  
  Piste : garder les ensembles de tracks sonores classiques, mais remplacer `Tone` par des paramètres propres au sampleur one shot.

- Créer la track sampleur slice et son fonctionnement  
  Piste : même logique que la track sampleur one shot. On garde les ensembles sonores, et `Tone` sert aux paramètres propres à cette track.

## 7. Séquenceur / performance
- ~~Mettre en place réellement le swing et le quantize du séquenceur~~  


- Trouver comment ajouter une fonction ratchet / roll pour le séquenceur, surtout trouver son workflow  
  Piste : aucune.

- Trouver comment mettre en place la fonction macro des 4 potentiomètres dispo, et surtout leur attribution / workflow  
  Piste : possiblement un hall ensemble, par exemple en appuyant sur `Shift` + un hall.  
  Idéalement, il y aurait plusieurs banques, et les halls switch serviraient à choisir la banque.  
  Pour choisir quel paramètre va sur quel pot, il faudrait idéalement lancer un mode `choose` qui permet de naviguer parmi les ensembles ; dans ce mode, tourner l’encodeur ne modifierait pas le paramètre mais assignerait le paramètre touché au potentiomètre visé. Le workflow reste à clarifier.

## 8. Système global
- Mettre en place d’autres paramètres dans le menu réglage global, et le retravailler  
  Piste :  
  - renommage des projets avec un système classique  
  - possibilité de générer des noms aléatoires pour aller plus vite, via une base cohérente avec la musique électronique  
  - menu `Switch` exposant les différentes pages de calibration déjà créées, avec accès direct pour recalibrer  
  - gestion de la courbe de vélocité  
  - gestion de la luminosité des LED

- Mettre en place un système de preset et de kit, surtout trouver comment le mettre en place  
  Piste : aucune.

- Mettre en place un système de flash firmware utilisateur  
  Piste : utiliser la carte SD. Au boot, le système vérifie s’il y a un firmware à flasher ; si oui, il le flash puis supprime le fichier ou le marque comme traité pour éviter de reflasher à chaque boot.

## 9. Moteurs / DSP / intégration
- Retravailler les algo de drums pour les rendre plus économes en CPU et plus musicales

- Importer un clavier électrique open source et tester son coût CPU

## 10. Extension externe
- Mettre en place le SPI-LINK de Ksoloti pour communiquer avec lui en audio et en contrôle de paramètres

- Créer la track Module, qui sera une sorte de track Hybrid mais où l’audio et l’envoi/réception de paramètres se feront via le SPI-LINK, afin de pouvoir brancher des modules externes reposant sur ce pipeline comme le Ksoloti ou de futurs modules Daisy Seed  
  Piste : idéalement elle suivra la même architecture UI que la track Hybrid. Comme certains modules seront potentiellement des effets, il faudra aussi pouvoir choisir à quel endroit de la chaîne audio on souhaite les placer, avec un moyen cohérent avec le workflow actuel.

## 11. Hardware futur
- Modifier l’archi audio actuelle quand la nouvelle board 4 input stéréo arrivera  
  Piste : passer d’un TDM 8ch sur 2 blocks SAI à une architecture TDM 4ch sur 4 blocks SAI.

- Mettre en place le système de maintien step + clic encodeur pour annuler un paramètre p-locké quand la nouvelle board avec encodeurs cliquables sera disponible