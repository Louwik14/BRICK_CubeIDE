# Z6 — État et persistance

Les formats courants sont stricts: Pattern v6, Project v6, Kit v4 et Patch v4. Les banques rejettent toute version, taille d'en-tête ou taille de payload différente; aucune migration d'un ancien format n'est tentée.

Pattern stocke les configurations, paramètres, routes, Note FX et huit séquences homogènes indexées `0..7`. Project embarque ce Pattern et ses états globaux, multi et macro indexés par slot. Kit contient exactement huit payloads de piste. Patch représente une seule piste assignable et ne porte ni rôle ni ordinal.

Le Master global n'est pas un slot de Pattern, Kit ou Patch et ne possède aucun état de piste. Looper et External sont persistés comme moteur et configuration du slot correspondant, avec l'entrée External exacte.

La capture et l'application séparent état persistant et runtime transitoire. Toute validation précède la mutation. Un Pattern ou Project appliqué avec succès invalide Undo/Redo; un rejet laisse l'état courant intact.
