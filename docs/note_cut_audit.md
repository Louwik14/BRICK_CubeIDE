# Trace RAM des coupures de notes

Cette instrumentation temporaire est active en Release/LTO. Reproduire la
coupure, interrompre immédiatement le firmware avec GDB, puis exécuter le bloc
suivant. Les deux fichiers binaires sont écrits dans le répertoire courant de
GDB. Ne pas redémarrer la carte avant le dump.

```gdb
shell cls
set pagination off
set logging file note_audit_gdb.txt
set logging overwrite on
set logging enabled on
p/x *(unsigned int *)&g_note_audit_control_sequence
p/x *(unsigned int *)&g_note_audit_audio_sequence
p/x &g_note_audit_control
p/x &g_note_audit_audio
dump binary memory note_audit_control.bin (char*)&g_note_audit_control ((char*)&g_note_audit_control+24576)
dump binary memory note_audit_audio.bin (char*)&g_note_audit_audio ((char*)&g_note_audit_audio+12288)
set logging enabled off
```

Chaque enregistrement occupe 24 octets little endian : `sequence` u32,
`tick` u32, `id` u32, `aux` u32, `event` u16, `track` u8, `note` u8,
`detail` u8, `held_count` u8, `held_mask` u16. Les anneaux ont respectivement
1024 et 512 entrées. Trier chaque anneau par `sequence` non nulle, puis
conserver les entrées dont la séquence se trouve dans les dernières 1024 ou
512 valeurs du compteur. La séquence est écrite en dernier; une entrée à
zéro est incomplète. Les séquences CONTROL et AUDIO sont indépendantes. Le
`tick` CONTROL vaut `HAL_GetTick()`; le `tick` AUDIO vaut zéro. Le handle de
sortie relie `WINDOW.id` à `AUDIO_COMMAND.id`.

Décodage CSV :

```sh
python tools/decode_note_audit.py note_audit_control.bin note_audit_audio.bin > note_audit.csv
```

| event | sens | champs importants |
| --- | --- | --- |
| 1 `HALL_EDGE` | seuil physique franchi | `track` clé, `detail` press, `id` raw bas 16 / seuil haut 16, `aux` masque held complet |
| 2 `HALL_DROP` | entrée refusée | `aux` 1/2 ingress fermée, 3 file pleine |
| 3 `HALL_POP` | entrée remise au clavier | `id` ingress serial, `aux` injection autorisée bit 16 / déjà injectée bit 0 |
| 4 `KEY` | entrée clavier | `track` clé, `detail` press, `held_count`, `held_mask` |
| 5/6 `KEY_ON` / `KEY_OFF` | note produite | `track` clé, `note` hauteur, `id` ingress serial |
| 7 `OCCURRENCE` | événement remis au séquenceur | `id` occurrence, `detail` ON/OFF, `aux` ingress serial |
| 8 `INGRESS_FAIL` | identité absente ou admission refusée | `id` occurrence quand disponible |
| 9 `OUTPUT` | intention musicale ou nouvelle liaison | `id` semantic id et `aux` causal id; `detail=0x81`: `id` handle et `aux` semantic id |
| 10 `VICTIM` | sortie sacrifiée | `id` handle, `aux` semantic id; `detail` index bas 4, raison haut 4 (0 nouvel ON, 1 réduction, 2 admission Multi) |
| 11 `PUBLISH_FAIL` | commande refusée | `detail` précise la phase |
| 12 `PANIC` | arrêt global ou sa cause | `aux` 1 MIDI CC120/123, 2 ledger, 3 ingress, 4 transport stop, 5 runtime, 6 chargement projet |
| 13 `AUDIO_COMMAND` | commande reçue par l'adaptateur | `id` handle, `detail` ON/OFF, `aux` moteur haut 16 / sortie déjà active bas 16 |
| 14 `AUDIO_VOICE` | allocation ou remplacement | `id` handle, `aux` ancien handle; `detail` voix bas 7 / ancienne voix held bit 7 |
| 15 `AUDIO_PANIC` | arrêt global appliqué | `detail` type de panic |
| 16 `INGRESS` | événement admis | `id` occurrence, `detail` ON/OFF |
| 17 `WINDOW` | transition publiée | `id` handle, `detail` START/STOP/RETRIGGER, `aux` échantillon bas 32 |
| 18 `HALL_QUEUED` | front ajouté à la file | `id` ingress serial |
| 19 `OWNER` | pile propriétaire par hauteur | `detail` 1 push, 0 pop, 2 débordement; `track` piste |
| 20 `AUDIO_TRANSPORT` | transport reçu | `detail` type |
| 21 `OUTPUT_DEATH` | identité retirée | `id` handle, `aux` semantic id |

Lire la première divergence dans cet ordre : front Hall réellement émis
pendant le maintien (raw/seuil), perte d'un front, changement d'état du
clavier, occurrence NOTE_OFF, intention STOP ou victime, publication, puis
commande/audio voice ou PANIC. Le masque clavier ne couvre que les 16
premières clés; `held_count` les couvre toutes. Le masque Hall dans `aux`
couvre jusqu'à 32 clés.
