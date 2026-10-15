# Contrat MSP et garde MPU

Le STM32H743 utilise la MSP pour le thread principal et les IRQ. Le linker
materialise trois zones distinctes en haut de SRAM D1 :

1. `.ram_d1_reclaimed`, arene opt-in pour les futures allocations D1 ;
2. `.msp_guard`, page MPU de 4 Kio sans acces ;
3. `.msp_stack`, pile MSP descendante de 24 Kio terminee par `_estack`.

Le build Release place le heap borne entre `0x24060780` et `0x24061780`,
l'arene recuperee entre `0x24061780` et `0x24079000`, la garde entre
`0x24079000` et `0x2407A000`, puis la MSP jusqu'a `0x24080000`. La capacite D1
recuperee est donc exactement 96 384 octets avec le placement statique courant.
Les futures donnees doivent utiliser `D1_RECLAIMED`; les assertions linker
interdisent tout chevauchement avec la garde ou la pile.

`compile_geom` n'alloue plus ses tableaux temporaires sur la MSP. Un scratch de
4 Kio reutilise l'union Groove existante, sans augmenter le workspace de
persistance. `project_remove_tree` valide toute l'arborescence avant suppression
et refuse une profondeur superieure a deux trames.

## Protection et diagnostic

La region MPU 7 couvre la garde de 4 Kio, alignee, sans acces et non executable.
`MEMFAULTENA` est active avant le MPU. Un franchissement provoque MemManage,
avec escalade HardFault si l'empilement de l'exception echoue; les handlers nus
demandent un reset systeme sans appeler le heap, FatFs ou un service AUDIO.

Le startup peint la pile avec `0xA55AA55A` avant le premier appel C. Le
superloop examine 64 mots par passage, hors IRQ, et publie :

- `g_msp_stack_capacity_bytes` ;
- `g_msp_stack_high_water_bytes`.

## Risque residuel

Les appels indirects critiques restant ouverts interdisent de qualifier les
24 Kio de borne statique formellement demontree. Les campagnes couvrant boot,
Project Save/Restore/Delete, Pattern, Groove, USB host/device, SD, AUDIO et les
imbrications IRQ doivent controler le watermark et conserver une marge revue.
