# GROUP audio contract

Entity 7 is the GROUP master and logical control owner. Entities 8..15 are
eight independent children with canonical engine configurations. A child may
use any supported child engine and keeps its native mono/stereo path, filter,
VCA, level, pan, mute and sends.

The mixer processes each child independently. Child sends leave before the dry
sum; child dry is redirected into the dedicated GROUP bus and cannot also reach
MAIN. The post-sum bus applies the master filter and master MIX before routing
to MAIN. Parent mute cuts the bus and every child's pre-sum sends without
rewriting local child mute state.

The physical bus is AUDIO-owned. CONTROL and UI use `entity_id` and derive
roles, membership and ensemble ownership from `entity_topology`. MOD belongs
to the master; child-owned ensembles retain the selected child identity.
