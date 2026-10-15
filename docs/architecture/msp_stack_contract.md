# Contrat MSP et garde MPU

Le STM32H743 exécute le thread principal et toutes les IRQ sur la MSP. Le
linker matérialise désormais trois zones distinctes en haut de SRAM D1 :

1. `.ram_d1_reclaimed`, arène opt-in pour les futures allocations D1 ;
2. `.msp_guard`, page MPU de 4 Kio sans aucun accès ;
3. `.msp_stack`, pile MSP descendante terminée par `_estack`.

Le build Release du 15 octobre 2026 place le heap borné entre `0x24060780` et
`0x24061780`. La pile provisoire commence à `0x24076000`, la garde occupe
`[0x24075000, 0x24076000)` et `_estack` vaut `0x24080000`. L'arène récupérée
`[0x24061780, 0x24075000)` offre donc exactement 80 000 octets. Elle est vide
dans ce build et les futures données doivent utiliser `D1_RECLAIMED` ; les
assertions linker refusent tout chevauchement avec la garde.

## Dimensionnement provisoire

La cible technique reste 24 Kio de MSP. Elle n'est pas activée tant que les
ensembles de cibles des appels indirects relevés par l'audit statique ne sont
pas fermés. Le build de production réserve donc provisoirement 40 Kio. Avec
les adresses D1 du build courant, passer à 24 Kio porterait l'arène récupérée à
96 384 octets, et non aux 64 096 octets calculés sur l'ancienne MAP. Le palier
de 40 Kio récupère déjà davantage que l'objectif initial de 64 096 octets sans
transformer la borne conditionnelle de 17 600 octets en preuve de sûreté.

`compile_geom` n'alloue plus ses deux tableaux de 4 Kio sur la MSP. Leurs
durées de vie ne se chevauchent pas : le même tableau de 4 Kio sert d'abord aux
distances au nœud, puis aux espacements. Pour une compilation mono-piste il
réutilise l'union Groove fournie ; pour une compilation multi-piste il utilise
le bloc Groove de secours, qui n'est alors pas un résultat actif. La taille du
workspace de persistance reste 268 544 octets.

`project_remove_tree` effectue une première passe sans suppression. Toute
arborescence dépassant la racine et son unique niveau de sous-dossiers est
refusée avant la première suppression. La passe de suppression conserve le
même contrôle afin que la récursion reste bornée à deux trames.

## Protection et diagnostic

La région MPU 7 couvre exactement la garde de 4 Kio. Elle est alignée sur
4 Kio, sans accès privilégié ou non privilégié, non exécutable, et n'entre en
conflit avec aucune des régions 1 à 6. `MEMFAULTENA` est activé avant le
MPU. Un franchissement provoque donc MemManage, avec escalade HardFault si
l'empilement de l'exception échoue ; les handlers nus demandent immédiatement
un reset système sans appeler le heap, FatFs ou un service AUDIO.

Le startup peint toute la pile réservée avec `0xA55AA55A` avant le premier
appel C. Le superloop examine 64 mots par passage, hors IRQ, et publie :

- `g_msp_stack_capacity_bytes` ;
- `g_msp_stack_high_water_bytes`.

Ce watermark sert uniquement à qualifier les scénarios réels. Il ne remplace
pas la fermeture statique des callbacks et ne suffit pas, seul, à autoriser le
passage à 24 Kio.

## Condition de passage à 24 Kio

Le changement de `_Min_Stack_Size` de `0xA000` à `0x6000` n'est autorisé
qu'après fermeture du graphe indirect critique et campagnes couvrant boot,
Project Save/Restore/Delete, Pattern, Groove, USB host/device, SD, AUDIO et les
imbrications IRQ. La MAP doit alors confirmer une garde de `0x1000`, une pile
de `0x6000` et une arène D1 de 96 384 octets avec le placement statique actuel.
