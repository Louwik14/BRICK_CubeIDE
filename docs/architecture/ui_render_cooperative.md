# UI render cooperatif

## Frontiere avec USB

Le renderer cooperatif est autonome. L'ingress USB Audio OUT PC vers BRICK est
publie dans son ring depuis le callback IRQ TinyUSB et ne depend plus de la
cadence UI. Aucun include, callback ou checkpoint USB n'existe dans
`drv_display`, U8g2 ou les pages UI. Les services USB restes differes sont
appeles par l'orchestrateur de superloop, jamais par le domaine UI.

## Pipeline et proprietes des buffers

La superloop Low-Cost appelle, dans cet ordre, `ui_tasklet_poll()`,
`ui_renderer_oled_service_poll()` puis `display_flush_service_poll()`. Le rendu
est demande toutes les 16 ms. Une page normale produit la frame dans l'ordre
suivant : clear du framebuffer, header, corps de page/widgets, footer, popup.
Le flush DISPLAY est lui aussi cadence a 16 ms.

`drv_display.c` possede les deux buffers de 1024 octets :

- `buffer` en SDRAM est l'unique cible U8g2 et le back-buffer logique de l'UI ;
- `flush_snapshot` en RAM DMA est copie seulement au debut d'un flush complet,
  puis compare au framebuffer suivant. Le rectangle englobant les octets
  modifies est compacte dans `flush_transfer` et emis par un DMA SPI unique.
  Le SSD1309 est place en adressage horizontal avec une fenetre de colonnes et
  pages ajustee au rectangle; une image entierement modifiee reste un transfert
  continu de 1024 octets et une image identique ne lance aucun DMA.

Il n'existe pas de front-buffer logiciel supplementaire. L'OLED est le front
buffer physique. La copie `buffer -> flush_snapshot` fournit l'atomicite
necessaire vis-a-vis du DMA : le rendu suivant peut modifier `buffer` sans
modifier l'image continue deja capturee par le flush en cours. La fin du DMA
est constatee au passage de superloop suivant ; un flush normal demande donc
un appel de lancement et un appel de finalisation.

## Contrat cooperatif retenu

Le renderer template est une machine d'etats a frontieres semantiques :

`HEADER -> PREPARE -> SPECIAL`, ou
`HEADER -> PREPARE -> AUDIO_FX -> SLOT_0..3 -> GROUP`, puis
`FOOTER -> FINALIZE`.

Un appel de superloop execute exactement un etat. `PREPARE` fige la structure de
page, la famille, les quatre identifiants de parametre et le contexte p-lock.
Les quatre slots sont donc reprenables sans dupliquer leur logique. Les branches
plein ecran (sampler, wavetable, graphe live) restent un quantum indivisible
parce que leurs primitives partagent un cache et un layout. La machine ne
pretend pas preempter une primitive U8g2 deja entree et n'utilise aucun budget
DWT comme ordonnanceur.

Les fonctions de page, le popup et le demarrage du flush exigent une frame
complete. Le contrat `render_pending/render_cancel` rend cette attente explicite
pour les pages fractionnees. Une frame generique demande au maximum dix
passages courts et conserve l'echeance de rendu de 16 ms sans second
framebuffer, nouvelle file ou instrumentation permanente.

## Atomicite, annulation et fraicheur

`g_ui_rendering` reste actif pendant toute la construction. DISPLAY continue un
DMA deja demarre depuis `flush_snapshot`, mais ne peut capturer le framebuffer
partiel. Le popup est dessine uniquement apres `FINALIZE`; aucune frame partielle
n'est donc visible.

La page et la structure de navigation sont revalidees avant chaque quantum. Le
page manager marque chaque entree invalide apres `enter` et la restauration de
sous-page : les changements provenant des services Hall/track suivent donc le
meme contrat que ceux provenant de `ui_core_tick()`. Un changement structurel
de page, track, famille, sous-page ou banque annule le job, efface le
back-buffer partiel et redemarre depuis la derniere demande. Une generation
d'invalidation couvre tout delta encodeur ou evenement UI et coalesce les
demandes rapides sans accumuler de dette de frames. Une variation de cette
generation pendant un job ne l'annule pas : la frame coherente en cours atteint
`FINALIZE`, puis le prochain lancement capture la valeur la plus recente sans
attendre l'echeance periodique. La generation joue ainsi le role de `rerender
pending` sans file de frames ni historique de valeurs.

Les callbacks et caches U8g2 restent utilises dans leur ordre normal; aucun etat
U8g2 n'est suspendu au milieu d'une primitive. Le popup prend sa valeur la plus
recente a la fin de la frame. Les pages non-template conservent leur rendu
atomique historique.

## Composition finale

Il n'existe qu'une machine d'etats UI : `ui_renderer_template_job_t`. Le curseur
MIDI FX est une extension de page executee apres `FINALIZE`, sous les memes
callbacks `render_pending/render_cancel`; ce n'est pas un second renderer. La
generation OLED est l'unique coalescence des demandes. La cancellation,
centralisee par `ui_renderer_oled_cancel_active()`, est reservee aux changements
structurels; une invalidation de valeur ne jette jamais une frame active.

L'ancien point d'entree synchrone `ui_renderer_template_draw()` n'est pas
conserve. Le rendu ne porte aucune politique USB; apres chaque quantum, il rend
naturellement la main a la superloop pour les services USB differes.

## Convention de profilage des widgets

Les compteurs `g_ui_render_prof.renderer.widget` sont hierarchiques. Le compteur
`virtual_slot` mesure le wrapper virtuel complet (selection, valeur, widget et
label). Le type concret rendu dans ce wrapper est aussi mesure dans sa categorie
propre; ces temps se recouvrent donc volontairement et ne doivent pas etre
additionnes. `enum_fallback` couvre le dessin enum utilise faute de widget
virtuel concret. Pour les pages CFG, `custom_track_cfg` mesure le widget CFG
complet, tandis que `track_cfg_text` et `track_cfg_bitmap` mesurent exclusivement
le sous-chemin concret texte ou icone. `algorithm_bitmap` reste reserve a
`uiw_draw_algo_icon()` et ne designe pas les icones CFG.
