# Modular Magic System

Magic is how a person can influence their enviroment using mana. There are two main types of magic in this world:

- Inner magic: The user uses their mana to enhace its own body and weapon.
- Outter magic: The user uses their mana alongside a catalyst to influence its surroundings.

Inner magic is mostly used by martial artists to make them perform inhuman tasks. Outter magic is used by wizards to pour their mana into a catalyst and invoke powerfull spells capable of creating or destroying the things around them.

While fighters imbue their weapons with powerful modifications, casters create spells to engage in combat. Spells are a mold for mana to manifest in the world through the caster will, and, as such, its the caster responsability to have the knowledge and materials necessary to create a powerful spell.

**Core Concept:** Spells in the game are not pre-defined abilities, but rather modular constructs built by combining physical loot items: **Sigils** and **Keystones**. This system directly ties into the ARPG loot mechanics, where dropping the right magic components is just as important as dropping good weapons or armor. Every enemy that uses spells participate in this same system as the players, so every spell seen in the game should be usable.

## Magic Components as Loot

Players assemble spells using three main components. The first two (Sigils and Keystones) are dropable loot with varying rarities, affixes, and power levels.

### 1. Sigil (The "What")
The Sigil determines the magical element, type, or effect of the spell.
- **Fire & Ice:** Changes the temperature of objects/entities hit. Changing temperature drastically aplies more damage to the enemy. Enemies have different resitance or ~fraqueza~ to this effect.
- **Earth:** Purely physical, solid constructs that deal blunt damage or block paths.
- **Light:** Divine energy focused on healing, shielding, or buffing.
- **Dark:** Necrotic and debbuf effects.
- **Lightning:** Fast, chaining, shocking energy.

*Loot Aspects:* A Sigil drops with randomized affixes. For example, a rare Fire Sigil might roll `+10% Fire Damage`, `Causes burning for 3 seconds`, or `Heals player for 5% of damage dealt`.

### 2. Keystone (The "How")
The Keystone determines the physical properties, trajectory, and behavior of the spell in the world. It contains the data that the game engine's systems read to control the spell.

*Loot Aspects:* Keystones also drop with their own affixes. For example, an epic Keystone might roll `+20% Spell Radius`, `+15% Projectile Speed`, or special behaviors like `Splits into 2 smaller projectiles on impact`.

### 3. Ring (The "Vessel")
The Ring is the base item or UI construct where the player sockets their Sigils and Keystones to actually manifest the spell. More powerful rings might have more sockets or amplify the socketed components.

---

## How Spells are Assembled (Architecture)
Under the hood, spells use a pure Data-Driven architecture.

- **Data Only:** A Keystone acts as a pure configuration object. There is no hard-coded behavior inside it.
- **The Factory:** A generic `Spell-Factory` reads the player's equipped Ring (which contains a Sigil and a list of Keystones).
- **The Behaviors:** It instantiates a generic Spell entity and attaches modular behaviors (like `CircularTrajectory` or `SizeAndFormBehaviour`) based strictly on what the Keystone data says.
- **The Effect:** When the spell's collider hits a valid target, an `Effect System` simply checks the attached Sigil data and applies the corresponding elemental damage and status.

## Examples of Loot Synergy Combinations
Because there are no rigid "Mage" classes, players invent their own attacks:
1. **The Classic Fireball:** A player finds a `Fire Sigil` and a `Straight Trajectory Keystone`. They combine them to shoot a basic fireball projectile.
2. **The Earth Hammer:** A player finds an `Earth Sigil` and an epic `Weapon Form Keystone`. They slot it to summon a giant rock hammer that smashes directly in front of them, blending the Mage and Martial Artist playstyles.
3. **The Orbiting Ice Shield:** A player finds an `Ice Sigil`, a `Caster-Target Keystone`, and a `Circular Trajectory Keystone`. Upon casting, ice cubes now permanently orbit the player, freezing any enemies that get too close in melee range.

This deeply modular system encourages constant experimentation and gives magic users the same thrill of dropping new loot that a sword-wielder feels when dropping a legendary blade!


### Initial Keystones

#### Trigger (1 per spell)

Defines *when* the spell activates its primary payload and trajectory.
- **On Cast (default):** Activates immediately when the player casts it.
- **Proximity / Detection:** The spell is placed as a "ward" or "mine" and activates its Form/Trajectory only when an enemy enters its detection radius.
- **On Impact:** Activates when another spell or entity hits it.

#### Target (1 per spell)

Determines *who* or *what* the spell is anchored to or aiming toward, providing the baseline spatial reference point.
- **Self:** Targets the caster.
- **Locked Target (default):** Locks onto a specific enemy or object.
- **Free Aim:** Aims exactly where the crosshair or input dictates.
- **Ground / Point in space:** Targets a specific coordinate in the environment.

#### Form (1 per spell)

Dictates the *physical shape and collision volume* of the spell as it manifests in the world.
- **Wall:** A flat defensive or blocking structure.
- **Small Cube (default):** A standard, basic magical projectile.
- **Copy Weapon:** Mimics the caster's currently equipped weapon shape.
- **Cone:** A widening projection moving forward.
- **Half Moon:** A wide slashing arc shape.
- **Dragon:** A stylized, complex magical shape.
- **Aura / Sphere:** A volumetric area that envelops the target.
- **Armor / Bubble:** A defensive shell that surrounds the target.
- **Minion / Construct:** A physical entity with its own health pool (used for Summons).

#### Size (1 per spell)

Controls the *overall scale* of the spell's Form, directly influencing hitboxes and area-of-effect boundaries. Currently under refinement, with multiple ideas for how size is determined:
- **Cast Time (idea):** Holding the cast button longer increases the size.
- **Mana Cost (idea):** The amount of mana poured into the spell scales its size.
- **Fixed Defaults (idea):** Keystones simply grant hard-coded base sizes.

#### Spawn (N per spell)

Specifies exactly *where* the spell originates or instantiates relative to the Caster or the Target.
- **In Front of User (default):** Spawns directly ahead of the caster.
- **Above User:** Spawns vertically over the caster's head.
- **Above Target:** Spawns vertically over the target's head.
- **Below Target:** Spawns at the target's feet.
- **Globe Area:** Spawns dynamically along a spherical radius around the target.

#### Trajectory

Controls *how* the spell moves through space over time, giving it a flight path, physics behavior, or an AI controller.
- **Line:** Travels in a straight path from spawn to target.
- **Circling:** Orbits a target (requires a radius affix).
- **Wave:** Moves forward while oscillating side-to-side.
- **Stationary:** Remains fixed in place or moves 1-to-1 with its target.
- **Homing:** Seeks out the detected or locked target.
- **Autonomous (AI):** Used mostly with Minion forms, giving the spell a basic behavior controller instead of a rigid physics path.

#### Duration 

Defines how long the spell lasts and how frequently it applies its effect.
- **Immediate:** Instantaneous effect, no lingering presence.
- **Fixed Duration:** Lasts for a set time limit (e.g., 5 seconds).
- **Channeled / Toggled:** Drains mana constantly while active.
- *Modifier - Tick Rate:* For spells that last over time (like Auras or persistent hazards), determines how often the Sigil effect is applied.

#### Quantity

Describes how many instances of the spell are created. Includes a **Pattern** modifier to determine how they spawn:
- **Simultaneous (Spread):** Casts all copies at the exact same time (e.g., a shotgun blast of 10 ice shots).
- **Sequential (Burst):** Casts the copies one by one with a slight delay between them (e.g., a machine-gun burst of 10 ice shots).

### Sigils

Sigils act as the "Payload" of the spell. The *Effect System* applies whatever the Sigil dictates to any valid entity the spell hits.

#### Ice & Fire (Temperature)
Changes the temperature of objects/entities hit. Drastic changes deal massive damage and apply elemental status effects.

#### Earth
Purely physical, solid constructs. Typically deals blunt damage or creates physical obstacles.

#### Light (Buffs & Healing)
Focuses on positive reinforcement. If a spell with a Light Sigil hits the caster or an ally, the system applies a temporary stat boost (e.g., +Armor, +Speed), a healing over time, or an instant heal.

#### Dark (Debuffs & Necrotic)
Focuses on weakening. If a spell with a Dark Sigil hits an enemy, it applies a stat reduction or detrimental status (e.g., Slow, Weaken, Armor Shred).

## Refinements

- Defensive spells need a bit more playtesting to see how Shield vs Wall feels in typical ARPG mob scenarios.
- Consider how to scale the health/durability of physical forms (Walls, Minions) based on the Keystones used.
- (Resolved) The system now naturally supports detection (Trigger Keystone), Auras (Duration + Form), sequential/simultaneous multishots (Quantity Pattern), Buffs/Debuffs (Sigil variants), and Summons (Minion Form + Autonomous Trajectory).
