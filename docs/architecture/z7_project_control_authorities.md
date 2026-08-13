# Autorités Project CONTROL

`project_control` possède désormais l'intention utilisateur des macros/scènes et des références d'assets par entity. Les destinations de macros sont conservées dans l'autorité sous forme de clés paramètre stables. Une référence d'asset contient uniquement un identifiant logique dérivé de l'entity, un kind stable et un chemin; aucun slot de pool, index global, état de chargement ou handle runtime n'y figure.

`persistent_project_control` capture, valide et applique le working Pattern canonique, la sélection Pattern active, les macros et le manifeste logique d'assets. La banque reste streamable et n'est pas dupliquée en RAM. `pattern_live_ram` reste l'autorité CONTROL de la sélection Pattern active.

`project_v1` conserve le format et l'orchestration disque historiques. Ses blocs macros, autoload, diagnostics, queue et `bank_has_data` restent des représentations V1 de transfert ou des états runtime; ils ne sont pas repris comme autorités du Project canonique. La résolution chemin logique vers pool/slot demeure dans le chemin runtime V1 et une référence absente reste conservée dans `project_control`.
