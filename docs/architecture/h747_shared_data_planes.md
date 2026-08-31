# Contrat des data planes CONTROL/AUDIO

Ce document est l'inventaire normatif des donnees volumineuses partagees. La
FIFO fonctionnelle n'est pas un data plane. Les adresses C obtenues apres
resolution locale d'un ID ne font jamais partie de l'ABI M4/M7.

## Inventaire et ownership

| Data plane | Producteur -> consommateur | Zone / taille bornee | Contenu ABI et pointeurs | Publication | Recyclage |
|---|---|---|---|---|---|
| Sample RAM | M4 loader -> M7 voices | payload dans le page pool; registry non-cacheable `AUDIO_SHARED_REGISTRY_SDRAM`; map ID dans `D2_IPC` | `global_slot`, `ram_slot`, generation et `{region, offset, length}` | payload clean, descriptor immutable, DMB, map `global_slot -> ram_slot` | stop PROGRAM/PARAM, fin des credits lecteurs, withdraw generation, puis pages libres |
| Wavetable/mipmaps | M4 loader -> M7 Wave | payload dans le page pool; registry non-cacheable de 16 896 octets | slot, generation et refs `{region, offset, length}` par bande; aucun `float *` partage | payload clean, descriptor/bandes, DMB, `ready` publie en dernier | stop des voix + fence fonctionnelle, remove generation, puis pages libres |
| Multi | M4 loader/projection -> M7 Sampler | projection non-cacheable `AUDIO_SHARED_MULTI_SDRAM` (47 104 octets) + instruments compacts `D2_IPC` | zones et sources numeriques, IDs sample/instrument, offsets fichier; aucun path/pointeur | samples/zones immutables, DMB, instrument `ready` publie en dernier | stop instrument + fin des credits page, withdraw, puis catalogue/pages recyclables |
| STREAM pages | M4 Storage -> M7 readers | payload cacheable `.sdram_sample_page_pool`, 24 641 536 octets | descriptor M4 avec `data_offset`; token I/O pointer-free; resolution locale seulement | decode dans page, clean payload, clean descriptor, etat `READY` en dernier | un lease seqlocke par lecteur; `EVICTING` puis relecture de leur union avant recyclage |
| Preview PCM | M4 Preview -> M7 MAIN | ring non-cacheable `AUDIO_STORAGE_SHARED_SDRAM`, 2048 x 2 floats (16 384) + deux curseurs `D3_IPC` | samples seulement, aucun pointeur | payload, DMB, `write_count` M4 | M7 publie uniquement `read_count`; active/gain sont AUDIO-locaux via PARAM, sans epoch ni reset croise |
| Recorder PCM | M7 AUDIO -> M4 Storage/SD | ring non-cacheable `SDRAM_RECORDER`, 12 001 x 2 x 32 bits (96 008) + layout 16 octets `D3_IPC` | `head_cursor`, `tail_cursor`, `closed_session`, `capture_fault`; aucun config/etat fonctionnel partage | PCM, DMB, `head_cursor`; fermeture AUDIO publie session/fault | M4 ecrit seulement `tail_cursor` apres copie/commit |
| Looper live take | M7 capture -> M4 recorder, puis M4 map -> M7 reader | Recorder PCM ci-dessus; carte live CONTROL locale puis projection Stream existante | path borne et extents possedes en valeur; aucun pointeur. Le preroll (96 000 octets) est cacheable et M7-prive | meme head/fermeture Recorder; map Stream existante | tail Recorder, puis generation de map et credits pages; retrait apres stop/fence |

Les contexts FatFs, loaders, diagnostics, paths de catalogue, pointeurs de
buffers DMA et function pointers du generic recorder restent prives a M4. Les
voices, pointeurs DSP chauds et le preroll Looper restent prives a M7. Les API
`audio_shared_memory_resolve()` et `sample_page_cache_data_resolve()` creent une
adresse locale seulement apres validation de region/offset; cette adresse ne
traverse aucune commande ou mailbox.

## Placement et coherence

- `.ram_d3_ipc`, moitie haute de SRAM4: shareable non-cacheable, MPU region 5.
- `.ram_d2_ipc`, SRAM3 complete: shareable non-cacheable, MPU region 6.
- `.sdram_recorder`, derniers 256 KiB: shareable non-cacheable, MPU region 4.
- registries RAM/Wavetable/Multi: `.sdram_recorder`, non-cacheable.
- page pool: SDRAM cacheable. Le producteur nettoie le payload et le descriptor
  avant le flag/ID; le consommateur invalide sa cache privee
  avec `BRICK6_H747_DUAL_CORE` avant la copie. Sur H743, ces hooks sont des
  barrieres seulement afin de ne pas invalider une ligne sale du meme coeur
  interrompu.

`volatile` exprime uniquement les acces aux index/doorbells. `DMB` impose
l'ordre. Ni l'un ni l'autre ne remplace le placement non-cacheable ou le
clean/invalidate explicite.

## Synchronisation

Les compteurs SPSC ont un seul writer par direction. Le page cache separe
physiquement les metadata M4 dans `sample_page_cache.c` des resolutions
lecteurs M7 dans `sample_page_cache_audio.c`. M7 ne modifie jamais un
descriptor: il publie d'abord son lease, puis revalide le descriptor. M4
recycle seulement apres `EVICTING` et une relecture stable de tous les leases.
Les sections PRIMASK restantes dans `sample_page_cache.c` serialisent seulement
des writers M4 locaux; elles ne fournissent aucune exclusion inter-core. Le
Recorder n'installe plus de callback `__disable_irq()` dans le generic recorder.

STREAM conserve sa politique de besoins/credits et son scheduler. Recorder,
Preview et Looper conservent leurs semantiques, leur framing et leur longueur
STOP. Les six opcodes et la cadence CONTROL ne changent pas. Les requetes
visuelles typees utilisent PARAM dans la FIFO fonctionnelle existante.

## Controle de purete

`RESOURCE READY` ne vaut jamais `RESOURCE ACTIVE`. Les flags `ready`,
generations, maps, compteurs et seqlocks publient une disponibilite ou un droit
de reutilisation. L'activation audible reste ordonnee par PROGRAM/PARAM/NOTE,
TRANSPORT, RECORD ou PANIC dans la FIFO.

| Data plane | Pur data/ownership | Fonction cachee | Verdict |
|---|---|---|---|
| Sample RAM | oui | non; registry/map rendent la ressource resolvable | DATA PLANE PUR |
| Wavetable/mipmaps | oui | non; PARAM selectionne slot et generation | DATA PLANE PUR |
| Multi | oui | non; PROGRAM/PARAM/NOTE selectionnent et declenchent | DATA PLANE PUR |
| STREAM | oui | non; la fenetre est une projection physique d'execution | DATA PLANE PUR |
| Preview PCM | oui | non; active et gain passent par PARAM | DATA PLANE PUR |
| Recorder PCM | oui | non; start/stop passent par RECORD | DATA PLANE PUR |
| Looper live take | oui | non; RECORD/TRANSPORT ordonnent le lifecycle | DATA PLANE PUR |
| Mod Matrix | non | PARAM canoniques indexes par slot; etat et plans locaux M7 | COMMANDE PURE |
Restore n'est plus un data plane: le Pattern decode est valide directement,
puis installe comme etat CONTROL final. Les seules consequences AUDIO sont les
PROGRAM structurellement differents et les PARAM dont la valeur finale change.

### Retour STREAM exact

M7 publie une seule classe de protection physique: un lease par lecteur,
`{seq, key, registration_epoch, ranges[2]}`. Aucun compteur par slot, pin,
use-count, owner token, curseur ou snapshot de voix ne subsiste.

Il ne publie ni low-water, ni deadline, ni vitesse, ni wake, ni demande I/O
explicite. M4 derive le lookahead, possede scheduler, reservations et lectures
SD. `STREAM M7->M4 = LEASES PHYSIQUES UNIQUEMENT : OUI`.

Les ranges ne decrivent aucune phase musicale. Ils changent seulement lorsque
l'ensemble des pages encore lisibles change: bind, entree de page, wrap,
debut/fin du crossfade Looper et release physique. La publication supprime les
ecritures identiques; aucun heartbeat periodique n'existe.

Hors retours physiques necessaires au recyclage (tail FIFO, leases STREAM et
PCM/framing Recorder), les projections M7->M4 finales sont exactement le niveau
REC, les waveforms audio/synth et le diagnostic Audio.
Aucun ACK de commande, READY musical, binding, programme installe ou PARAM
applique n'est retourne. H743 et H747 partagent exactement cette semantique;
seuls placement, cache, barrieres et visibilite different.

Le retrait RAM/Wavetable n'observe pas le curseur consumer de la FIFO. CONTROL
invalide d'abord la projection, commit le STOP, puis attend localement
`L + 2*H`, avec `L` l'horizon maximal de publication et `H` la taille d'un
demi-buffer AUDIO. Les valeurs actuelles `L=64`, `H=64` donnent 192 samples;
elles sont derivees des constantes contractuelles, jamais d'une duree en ms.
Multi, Classic et Looper ne sont recyclables que lorsque leurs leases physiques
existantes ne referencent plus leurs cles. Aucun `released_generation`, fence
consumer ou ACK fonctionnel M7->M4 n'existe.

Les references de payload partagent uniquement leur ABI `{region, offset,
length}`: construction et resolution CONTROL sont distinctes de la resolution
AUDIO. De meme, CONTROL est l'unique writer du registre de descriptors
Wavetable; AUDIO en est uniquement reader apres publication.

PROGRAM transporte directement son descripteur structurel de quatre octets
dans `command.value`; il ne possede donc ni registre, ni ID, ni data plane.
Aucune decision fonctionnelle ne subsiste hors FIFO et aucun retour fonctionnel
M7->M4 ne subsiste.

## Port H747

Le port H747 place les memes sections dans des regions visibles des deux
coeurs, configure MPU region 5/6 et `.sdram_recorder` sur les deux coeurs, puis
definit `BRICK6_H747_DUAL_CORE` pour activer clean/invalidate des caches prives.
Aucun scheduler, opcode, setter FM, cadence ou chemin DMA live n'est specifique
au port H747.

## Teardown et cache

Preview publie `PREVIEW_ACTIVE=0` avec une fence FIFO. AUDIO avance
`read_count` jusqu'a `write_count`; CONTROL ne reutilise le ring qu'apres ce
drainage. Aucun epoch, active/gain partage ou reset croise ne subsiste. RAM et
Multi suivent le meme contrat: STOP fence, passage du
tail, retrait de projection, puis recyclage.

La reutilisation du dernier tombstone de l'index STREAM publie explicitement la
ligne modifiee avec `intercore_cache_publish`. C'est le meme clean cache que les
insertions ordinaires; le protocole de pages et de credits ne change pas.
