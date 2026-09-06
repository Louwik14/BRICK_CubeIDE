# Audit UI current findings — V3

## VERDICT

`NEW FINDINGS`

## NEW FINDINGS

### NF-01 — Un popup inactif réarme le dirty à chaque rendu

`Src/UI/ui_renderer_oled.c:183-187` appelle `ui_roll_popup_render()` après
chaque rendu de page. Lorsque le popup est inactif, sa branche de sortie
`Src/UI/ui_roll_popup.c:45-53` appelle néanmoins `ui_service_dirty_set()`.

Le rendu consomme alors le dirty puis le réarme avant de sortir. La boucle
`StartUiTask` (`Board/Generated/Src/freertos.c:423-455`) retrouve du travail
à chaque itération et peut rester dans une boucle de rendu/flush sans
événement visible. C'est une violation de liveness et du contrat « dirty
uniquement sur changement visible ».

### NF-02 — Dirty OLED et LED déclenché par toute entrée acceptée

`Src/UI/ui_event.c:54-80` appelle systématiquement
`ui_service_dirty_set()` et `ui_service_led_dirty_set()` après l'enqueue,
avant que le dispatch sache si l'entrée change réellement la présentation.
Cela inclut notamment les relâchements, no-op, événements ignorés et entrées
qui seront jetées pendant le boot ou un projet occupé.

Le résultat est un rendu OLED et une présentation LED pour une activité qui
ne produit pas de changement visible. Cela contredit le contrat de dirty et
élargit inutilement le travail de la tâche UI.

### NF-03 — Le traitement Control salit globalement la présentation

`Src/App/control_domain.c:1051-1161` positionne systématiquement le dirty OLED
et LED dès qu'un message a été consommé (`:1157-1161`). Un message refusé parce
que le projet est occupé est pourtant compté comme traité (`:1059-1063`) et
ne change aucune présentation. La même fan-out s'applique aux messages
no-op ou sans changement visible.

Le dirty doit être produit par le changement présenté, pas par la seule
consommation d'une intention Control.

### NF-04 — Les résultats Settings réveillent l'OLED même hors de Settings

`Src/Storage/settings_storage_service.c:96-137` marque certains événements
comme visibles pour l'UI puis appelle globalement `ui_service_dirty_set()`,
sans vérifier que la vue Settings est actuellement affichée.

Le payload est publié avant le wake de fin de service, mais il n'est consommé
qu'au rendu Settings via
`Src/UI/pages/Settings/ui_settings_public.inc:98-108` et
`Src/UI/pages/Settings/ui_settings_render.inc:1060-1063`. Un résultat Storage
arrivant sur une autre page force donc un redraw sans changement visible de
cette page. La lecture stable du dernier résultat reste acceptable ; c'est
le wake global qui est excessif.

### NF-05 — La progression et la fin d'un projet peuvent rester affichées

Le rendu de l'écran occupé lit bien la progression dans
`Src/UI/ui_renderer_oled.c:102-140` et est sélectionné par `:175-182`, mais
`ui_renderer_oled_next_render_wait_ticks()` (`:230-270`) ne programme aucune
cadence lorsque seul `control_domain_project_ui_busy()` est actif. La tâche
UI peut donc dormir indéfiniment après le premier écran de busy.

Le Storage modifie la progression et marque la fin dans
`Src/Storage/project_product.c:457,837-842,1260-1285`. Le chemin de réveil
Storage (`Src/App/brick6_app_init.c:438-447`) réveille Control mais ne produit
pas de dirty/wake UI pour ces changements. La progression visuelle et la
transition de fin ne sont donc pas garanties d'être présentées.

### NF-06 — La file Settings peut perdre silencieusement un résultat terminal

`Src/Storage/settings_storage_service.c:118-129` abandonne l'événement sans
signalement lorsque la file de capacité 16 est pleine. Les événements de
progression peuvent remplir la file avant un résultat terminal `READY` ou
`FAILED`, en particulier lorsque Settings n'est pas affiché et ne consomme
pas la vue.

Le dirty précédemment émis ne garantit alors pas que le résultat terminal
atteigne l'UI ; la présentation peut rester sur une progression obsolète.
Une vue latest-value est compatible avec le modèle, mais elle doit préserver
ou coalescer le terminal au lieu de le perdre.

## OVERARCHITECTURE

### Recovery automatique d'erreur DMA display

`Drivers/Drv_app/Src/drv_display.c:225-236,380-393` transforme une erreur SPI
DMA en état `READY`, efface l'erreur et réinitialise le flush. Puis
`Src/UI/display_flush_service.c:25-36` reprend la continuation et peut
relancer automatiquement le flush.

C'est une mécanique de survie/retry pour une erreur matérielle dont le
caractère normal n'est pas démontré. Si cette erreur est réellement
impossible, un invariant/debug ou un `FAULT` terminal simple suffit ; une FSM
de recovery n'est pas justifiée.

## RÉSULTATS PAR DOMAINE

- **Wait model** : conforme au modèle `INPUT`, `DIRTY`, `DMA_DONE`,
  `DEADLINE_UI`, sinon sommeil. Aucun timeout global ni polling 1 ms trouvé.
- **Input backlog** : file bornée et budget présents ; le débordement est
  compté puis perdu, et les entrées sont volontairement jetées pendant boot
  ou projet busy. Le problème actuel retenu est le dirty émis avant validation
  du changement visible (NF-02).
- **Deadlines / ui_tasklet** : deadlines explicites et orchestration ciblée ;
  aucune boucle `active_page->tick()` générique trouvée. La deadline de
  progression projet manque (NF-05).
- **Settings** : lecture latest-value stable acceptable ; wake non lié à la
  visibilité et perte possible du terminal (NF-04, NF-06).
- **OLED / frame_pending / DMA** : le snapshot DMA et les continuations par
  callback évitent le mélange de frames ; la retry d'erreur est
  l'overarchitecture identifiée. Le réarmement du popup crée en plus une
  boucle de rendu (NF-01).
- **LED** : fin DMA attendue par callback, sans boucle active ; le dirty LED
  trop large est couvert par NF-02 et NF-03.
- **Waveform / meter** : latest-value lu à cadence visuelle lorsqu'il est
  visible ; completion waveform/cache ponctuelle avec dirty ; aucun wake par
  bloc audio trouvé.
- **Calibration** : la calibration physique reste côté Control ; UI limitée
  à la présentation, aux deadlines et aux intents.
- **FatFs / Storage** : aucun appel FatFs direct trouvé côté UI. Les défauts
  retenus concernent le contrat de wake et la conservation des résultats, pas
  une demande de snapshot/binding théorique.
- **Ownership** : navigation, présentation, rendu et LED presentation restent
  côté UI ; Storage, Project, Recorder et calibration physique restent hors
  UI. Aucune décision produit pilotée par une frame OLED n'a été retenue.
- **Erreurs display** : aucune FSM de recovery supplémentaire n'est requise ;
  seule la retry DMA existante est classée OVERARCHITECTURE.

## DOCUMENT

Audit uniquement. Aucun patch code. Aucun build. Ce fichier est le seul
artefact mis à jour.
