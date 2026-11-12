# Combat

## Animations

Animation names consist of four characters:

1. Animation group: "g", "m", "c", "b" or "f"
2. Weapon type: 1-9
3. Animation type: "w", "r", "a", "g", "p" or "d"
4. Animation variant: 1-6 

## Weapon types

|Ordinal|Description|
|-|-|
|1|Stun baton|
|2|Sword|
|3|Double-bladed sword|
|4|Dual swords|
|5|Blaster pistol|
|6|Dual pistols|
|7|Blaster rifle|
|8|Hand-to-hand|
|9|Heavy weapon|

## Animation types

|Letter|Description|
|-|-|
|w|drawing a weapon|
|r|ready|
|a|attacking|
|g|dodging an attack|
|p|parrying an attack|
|d|taking damage|

### Generic

- g?w?
- g?r?
- g?a?
- g?g?
- g?d?

### Melee

These animations are used when combatants are fighting each other, but only attacker is wielding a melee weapon.

- m?a?
- m?g?
- m?d?

### Cinematic Melee

These animations are used when both combatants are wielding a melee weapon.

- c?a?
- c?p?
- c?d?

### Blasters

- b?a?

### Feats

These animations are used when attacker is using a feat. Animation variant depends on the feat being used:

|Ordinal|Feat|
|-|-|
|1|Critical Strike / Sniper Shot|
|2|Flurry / Rapid Shot|
|3|Power Attack / Power Blast|

- f?a?
- f?p?
- f?d?
