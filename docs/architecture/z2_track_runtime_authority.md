# Z2 - Autorite des pistes

`track_state` est l'autorite canonique des seize configurations indexees par
`brick_entity_id_t`. Chaque GROUP child conserve sa famille, son type et sa
configuration MIDI. `entity_topology` est l'unique autorite d'activite, role
MAIN/GROUP_MASTER/GROUP_CHILD et relation parent/membre. `track_runtime`
projette cette identite sans reconstruire un child en Sampler/RAM.

La realisation AUDIO est portee par `track_audio_binding_t`: entity, moteur,
instance, cible mixer, etat et generation. Mixer target, instance et bus ne
sont jamais des identites logiques. Une mutation suit validation, commit
canonique, puis refresh runtime.

Le GROUP master est lie au bus post-somme AUDIO, sans moteur de notes ni
emission. Il expose un TONE minimal de filtre, ENV3 comme source, MOD et MIX.
PLAY reste stocke mais masque; Keyboard et MIDI FX sont absents.

La valeur `track_sound_state.mix_mute` est l'unique base CONTROL par entity.
Le mute effectif child est derive du mute local ou parent, sans modifier la
base child. CONTROL projette cette valeur vers AUDIO afin de couper dry et
sends pre-somme.

Le snapshot de piste accepte toute entity active. Le clipboard d'un child
reste local. Celui du GROUP master capture et restaure le master et ses huit
children. Seuls les formats persistants Pattern/Project restent top-level
jusqu'au chantier 5.
