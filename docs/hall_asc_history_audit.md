# Audit historique du filtre Hall ASC

`92b31da52` (19 mars 2026) a introduit ASC entre l'acquisition Hall et le
détecteur. Son message et son diff ne documentent aucun défaut précis de
parasitage entre touches. Le code maintenait une somme et un compteur par
touche, publiait la moyenne d'un bloc non chevauchant de quatre mesures,
puis remettait la somme à zéro. Il ne corrigeait aucune valeur ADC brute,
ne partageait aucun état entre touches et ne distinguait pas une touche
physiquement tenue d'une mesure parasite persistante.

`c50242457` (30 juillet 2026) a explicitement contourné ASC pour la variante
Low-Cost : ses échantillons bruts allaient directement au détecteur. La doc
de l'époque désignait le filtre analogique du PCB comme autorité de lissage
Low-Cost. ASC ne restait actif que pour Premium. `95db50434` (2 août 2026)
a supprimé ASC pour réduire la latence Press/Note On des deux variantes.
La suppression ne peut donc pas avoir retiré une protection ASC du chemin
Low-Cost actuel : cette protection avait déjà été contournée.

Au rythme Low-Cost de 2,8 ms par touche, ASC x4 ne livrait une nouvelle
décision que toutes les 11,2 ms, avec jusqu'à 8,4 ms d'attente supplémentaire
avant la prochaine sortie. Au rythme Premium de 0,8 ms, ces durées étaient
3,2 et 2,4 ms. La moyenne amortissait un pic bref, mais une montée durant
plusieurs blocs franchissait finalement le même seuil, avec retard.

La nouvelle capture directe donne `g_hall_direct_state = 3`, erreur 0 et
pipeline restauré. Pour la clé 2, ADC1 Hall A, mux 4, la fin de la série
DMA à mux fixe vaut environ 6765 à 7208; les 64 lectures injectées directes
de la même entrée, TIM6 et DMA arrêtés, vont d'environ 7334 à 8068.
La montée existe donc dans la lecture ADC indépendante du DMA. Comme les
captures précédentes montraient une hausse soutenue jusqu'à plus de 20 000,
ASC x4 aurait pu retarder un RELEASE, sans supprimer la cause de la hausse.

Conclusion : aucun lien causal démontré entre la suppression d'ASC et le
défaut actuel. Le souvenir d'un ancien parasitage peut concerner un autre
mécanisme. Restaurer ASC sur la seule base de cet historique ralentirait
l'attaque et masquerait des mesures brutes toujours anormales. Les sondes
restent utiles tant que la cause de cette variation analogique n'est pas
établie.
