# Façade Pattern CONTROL canonique

La capture, la validation et l'application du Pattern canonique sont portées par `persistent_pattern_control`. La façade travaille directement avec les autorités CONTROL et ne traverse pas les structures V1.

La topologie est résolue par `entity_topology`: l'entité 7 reste une piste normale hors configuration GROUP; en mode GROUP, elle devient master et les entités 8 à 15 deviennent ses enfants. Le routage logique looper/source appartient à `control_routing`; sa résolution AUDIO reste hors persistance.

Le disque V1 reste inchangé et actif pour cette passe. La façade canonique n'est pas encore raccordée aux banques Pattern/Project.
