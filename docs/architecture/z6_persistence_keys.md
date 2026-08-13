# Z6 - Cles persistantes CONTROL

`persistent_key_catalog` centralise les conversions bidirectionnelles entre
les identites CONTROL internes et les cles persistantes. Les cles parametres
sont des constantes explicites independantes de l'ordinal de `param_id_t`. Le
catalogue expose aussi leur portee, leur persistabilite et leur admissibilite
p-lock. Les parametres du registry conservent leur valeur CONTROL float32.

Les conversions family/type, MIDI, input, clock, Note FX, sources MOD, formes
et triggers LFO, et modes d'enregistrement refusent les valeurs inconnues. Une
destination MOD persistante est toujours le couple entity logique et cle
parametre; la resolution AUDIO reste derivee.

Le role de l'entity 7 depend de la configuration du Pattern. Le type GROUP
active le master et les huit children. Sinon l'entity 7 est une track principale
normale et les children doivent etre inactifs. Aucun role n'est deduit du seul
identifiant 7.

Les indices de step, scene, macro et SUB restent de simples indices bornes :
ce sont deja des identites logiques stables. Division est stockee comme rapport
logique 1/2/4/8, quantification et swing comme pourcentages bornes. Les flags
p-lock n'ont actuellement qu'une valeur contractuelle, zero.
