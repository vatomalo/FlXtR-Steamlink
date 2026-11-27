# SoM hybrid builds

Generated from the `builds` list in `data/perks/perks.json`. No build is programmed as a class: each is a primary discipline plus inherited perks. The capacity needed is the sum of the costs; a build is playable once every perk in it is `IMPLEMENTED`.

## Phantom Samurai

Primary: **Samurai** (melee). Capacity 11. Playable perks 2/5.

A supernatural swordsman who dodges into counters and leaves shadows behind.

- **Shadow Step** (Ninja, 1): Your dodge rolls travel 4 frames further. *REQUIRES_ENGINE_HOOK*
- **Afterimage** (Ninja, 2): A perfect dodge leaves an afterimage that bursts half a second later for half your last hit on monsters within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Shadow Clone** (Ninja, 4): Every 4th hit of a combo is repeated by a shadow for 40% of its damage. Shadow hits never make shadows. *IMPLEMENTED*
- **Bushido** (Samurai, 2): A perfect block makes your next successful hit within 3 seconds deal 35% more damage. *IMPLEMENTED*
- **Seamless Transition** (Weaponmaster, 2): Switching weapons keeps your combo count. *REQUIRES_NEW_EFFECT_PRIMITIVE*

## Gravity Monk

Primary: **Monk** (crowd control). Capacity 14. Playable perks 2/5.

Pulls monsters together, staggers the crowd and bursts it with Qi.

- **Gravity Well** (Mentalist, 4): Combo finishers pull every monster within 48 pixels 12 pixels toward the finisher's target, where the ground allows. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Rising Dragon** (Monk, 3): Combo finishers stagger every monster within 32 pixels (SoM monsters cannot be launched). *IMPLEMENTED*
- **Kinetic Conversion** (Mentalist, 3): 5% more damage for every 8 pixels you moved in the last second, up to 25%. *REQUIRES_ENGINE_HOOK*
- **Qi Burst** (Monk, 2): Every 5th hit releases a shockwave: a quarter of that hit to the other monsters within 32 pixels. *IMPLEMENTED*
- **Sky Piercer** (Swordsman, 2): Bash (R2+A) through two monsters hits both for 20% more (SoM has no airborne monsters). *REQUIRES_ENGINE_HOOK*

## Death's Orchestra

Primary: **Dancer** (summoner). Capacity 11. Playable perks 2/5.

An undead band whose hits keep the dancer's combo alive.

- **Reanimation** (Necromancer, 4): A monster killed by your dark spell rises as an undead familiar for 20 seconds (one at a time). *REQUIRES_ENGINE_HOOK*
- **Soul Harvest** (Necromancer, 1): Each kill heals you 3% of your max HP. *IMPLEMENTED*
- **Synchrony** (Dancer, 3): An ally's hit within a second keeps your combo alive. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Pack Tactics** (Summoner, 1): 15% more damage while an ally or familiar is within 48 pixels of your target. *IMPLEMENTED*
- **Spirit Resonance** (Summoner, 2): Each familiar hit makes your next hit 15% stronger, up to 3 times. *REQUIRES_ENGINE_HOOK*

## Celestial Gunslinger

Primary: **Archer** (ranged). Capacity 14. Playable perks 1/5.

Arrows mark monsters with stars and the kills cascade.

- **Ricochet** (Archer, 4): A ranged hit bounces once to another monster within 64 pixels for half the damage. Bounces never bounce. *IMPLEMENTED*
- **Constellation** (Warlock, 2): Three hits on one monster mark it with a star. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Supernova** (Warlock, 3): Killing a star-marked monster bursts for 30% of the killing blow on the monsters within 40 pixels. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Explosive Rounds** (Archer, 2): Critical ranged hits explode: 30% to the other monsters within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Astral Pursuit** (Warlock, 3): Your drawn bolts turn toward star-marked monsters. *REQUIRES_ENGINE_HOOK*

## Immortal Weaponmaster

Primary: **Weaponmaster** (defensive). Capacity 13. Playable perks 2/5.

Thrives on long fights: kills heal and shield, one death is refused.

- **Undying** (Necromancer, 6): Once every 5 minutes, a blow that would kill you leaves you at 1 HP. *REQUIRES_ENGINE_HOOK*
- **Soul Harvest** (Necromancer, 1): Each kill heals you 3% of your max HP. *IMPLEMENTED*
- **Iron Body** (Monk, 1): Your hurt stagger ends a quarter sooner. *REQUIRES_ENGINE_HOOK*
- **Unbroken Rhythm** (Weaponmaster, 3): One whiff per combo does not end it. *REQUIRES_ENGINE_HOOK*
- **Bone Armour** (Necromancer, 2): Each kill gives a shield of 5% of your max HP for 10 seconds, up to 3 times. *IMPLEMENTED*

## Phantom Shinobi

Primary: **Ninja** (mobility). Capacity 10. Playable perks 1/5.

Moves through slowed time and leaves afterimages.

- **Temporal Distortion** (Mentalist, 2): Landing 3 hits within a second slows every monster within 64 pixels to half speed for 2 seconds. You keep your speed. *IMPLEMENTED*
- **Temporal Pursuit** (Mentalist, 2): After hitting a slowed monster, your next dodge toward it covers double distance, where the ground allows. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Afterimage** (Ninja, 2): A perfect dodge leaves an afterimage that bursts half a second later for half your last hit on monsters within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Shadow Step** (Ninja, 1): Your dodge rolls travel 4 frames further. *REQUIRES_ENGINE_HOOK*
- **Chronostasis** (Mentalist, 3): An Intercept or a perfect dodge freezes that attacker for one second (a third of a second on bosses). *REQUIRES_ENGINE_HOOK*

## Astral Fist

Primary: **Monk** (melee). Capacity 15. Playable perks 3/5.

Freezes a monster and pours a glove combo into it that lands all at once.

- **Chronostasis** (Mentalist, 3): An Intercept or a perfect dodge freezes that attacker for one second (a third of a second on bosses). *REQUIRES_ENGINE_HOOK*
- **Paradox** (Mentalist, 4): Hits on a frozen monster are stored and all land when it thaws, 20% stronger. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Flowing Fist** (Monk, 2): Each glove hit within a second of the last refills 10% of your weapon gauge. *IMPLEMENTED*
- **Hundred Hands of Time** (Monk, 4): Each Intercept during bullet time adds half a second to it, up to 3 seconds in all. *IMPLEMENTED*
- **Chrono Counter** (Mentalist, 2): A perfect block starts one second of bullet time (the Monk's). *IMPLEMENTED*

## Void Swordsman

Primary: **Samurai** (melee). Capacity 20. Playable perks 3/5.

Six cuts on a frozen foe, the sword is sheathed, time resumes and they all land.

- **Okuri Kaze** (Samurai, 4): Your hits during bullet time are held back and all land together when it ends, 10% stronger. *IMPLEMENTED*
- **Paradox** (Mentalist, 4): Hits on a frozen monster are stored and all land when it thaws, 20% stronger. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Time Fracture** (Mentalist, 3): Combo finishers freeze every monster within 32 pixels for half a second. *IMPLEMENTED*
- **Bushido** (Samurai, 2): A perfect block makes your next successful hit within 3 seconds deal 35% more damage. *IMPLEMENTED*
- **Time Collapse** (Mentalist, 7): Your stored damage no longer lands by itself; L2+R2 releases all of it at once, 50% stronger (20-second cooldown). *REQUIRES_NEW_EFFECT_PRIMITIVE*

## Psi Gunner

Primary: **Archer** (ranged). Capacity 17. Playable perks 2/5.

Arrows and enemy shots hang in the air, then fly.

- **Temporal Field** (Mentalist, 4): Combo finishers leave a 48-pixel field for 3 seconds; monsters inside move at half speed. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Frozen Arsenal** (Mentalist, 4): Monster projectiles that enter your time field stop for a second, then fly back toward their shooter. *REQUIRES_ENGINE_HOOK*
- **Last Bullet** (Archer, 2): Every 6th ranged hit deals 60% more damage. *IMPLEMENTED*
- **Steady Breath** (Archer, 3): A perfect dodge starts one second of bullet time (the Monk's). *REQUIRES_ENGINE_HOOK*
- **Piercing Shot** (Archer, 4): A ranged hit also strikes the monster behind the target in a line (48 pixels) for 60%. *IMPLEMENTED*

## Psychic Arsenal

Primary: **Weaponmaster** (melee). Capacity 11. Playable perks 5/5.

Weapon switches inside slowed time.

- **Psychic Acceleration** (Mentalist, 1): While any time effect of yours is running, your weapon gauge is topped up once a second. *IMPLEMENTED*
- **Dancing Steel** (Weaponmaster, 4): Switching weapons mid-combo releases a radial cut: 30% of your last hit to every monster within 32 pixels. *IMPLEMENTED*
- **Arsenal Mastery** (Weaponmaster, 2): The first hit within 2 seconds of a weapon switch deals 25% more damage. *IMPLEMENTED*
- **Temporal Distortion** (Mentalist, 2): Landing 3 hits within a second slows every monster within 64 pixels to half speed for 2 seconds. You keep your speed. *IMPLEMENTED*
- **Chain Reaction** (Weaponmaster, 2): A hit with a different weapon type than your last hit staggers the monster. *IMPLEMENTED*

## Soul Weaver

Primary: **Necromancer** (summoner). Capacity 17. Playable perks 1/5.

Delays death and lets spirits take the blows.

- **Borrowed Time** (Mentalist, 3): Damage you take is put back at once and taken again over 3 seconds instead (never more than you lost). *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Undying** (Necromancer, 6): Once every 5 minutes, a blow that would kill you leaves you at 1 HP. *REQUIRES_ENGINE_HOOK*
- **Death Pact** (Necromancer, 3): Below 30% HP, your familiar deals 50% more damage. *REQUIRES_ENGINE_HOOK*
- **Guardian Spirit** (Summoner, 3): A fifth of the damage you take goes to your familiar instead. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Spectral Blade** (Necromancer, 2): Every 5th hit adds a ghostly slash for 30%. *IMPLEMENTED*

## Gravity Sovereign

Primary: **Mentalist** (crowd control). Capacity 16. Playable perks 1/5.

Bends gravity and time around every finisher.

- **Gravity Well** (Mentalist, 4): Combo finishers pull every monster within 48 pixels 12 pixels toward the finisher's target, where the ground allows. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Time Fracture** (Mentalist, 3): Combo finishers freeze every monster within 32 pixels for half a second. *IMPLEMENTED*
- **Orbital Guard** (Warlock, 4): Two stars circle you at 24 pixels; each hits a monster it touches once a second for 10%. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Constellation** (Warlock, 2): Three hits on one monster mark it with a star. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Kinetic Conversion** (Mentalist, 3): 5% more damage for every 8 pixels you moved in the last second, up to 25%. *REQUIRES_ENGINE_HOOK*

## Reality Weaver

Primary: **Mage** (magic). Capacity 15. Playable perks 0/5.

Elemental spells cast into time fields.

- **Temporal Field** (Mentalist, 4): Combo finishers leave a 48-pixel field for 3 seconds; monsters inside move at half speed. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Elemental Convergence** (Mage, 2): Casting a different element than your last spell adds 15% damage, up to 3 times. *REQUIRES_ENGINE_HOOK*
- **Frostbite** (Mage, 2): Water spells slow the monster to half speed for 1.5 seconds. *REQUIRES_ENGINE_HOOK*
- **Arcane Echo** (Mage, 3): Every 4th spell echoes half a second later on the same monster for 40%. *REQUIRES_ENGINE_HOOK*
- **Overcharge** (Mage, 4): Spell damage beyond the monster's remaining HP explodes onto the monsters within 32 pixels. *REQUIRES_ENGINE_HOOK*

## Chrono Sovereign

Primary: **Monk** (melee). Capacity 23. Playable perks 3/5.

A martial artist bending space, gravity and time; the keystone extends every time effect.

- **Chrono Sovereignty** (Mentalist, 8): Every combo milestone adds a quarter second to each time effect you own (up to 2 seconds each); your time effects cost 25% more energy. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Time Fracture** (Mentalist, 3): Combo finishers freeze every monster within 32 pixels for half a second. *IMPLEMENTED*
- **Event Horizon** (Warlock, 4): Finishers pull the monsters within 48 pixels toward the point of impact, where the ground allows. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Thunder Step** (Monk, 4): Intercepts send a 24-pixel shockwave that staggers the monsters around the one you stopped. *IMPLEMENTED*
- **Hundred Hands of Time** (Monk, 4): Each Intercept during bullet time adds half a second to it, up to 3 seconds in all. *IMPLEMENTED*

## Shadow Duelist

Primary: **Rogue** (melee). Capacity 13. Playable perks 4/5.

Fights one monster at a time, always from behind.

- **Backstab** (Rogue, 1): 35% more damage from behind a monster. *IMPLEMENTED*
- **Silent Death** (Ninja, 1): 40% more damage on a monster that faces away from you and is not attacking. *IMPLEMENTED*
- **Duelist** (Samurai, 1): 15% more damage on a monster with no other monster within 64 pixels. *IMPLEMENTED*
- **Feint** (Rogue, 4): Shadowstepping behind a monster makes its next attack strike its nearest fellow monster instead. *REQUIRES_ENGINE_HOOK*
- **Knife in the Dark** (Rogue, 6): Hits from behind are always critical; hits from the front deal 25% less. *IMPLEMENTED*

## Iron Fortress

Primary: **Swordsman** (defensive). Capacity 13. Playable perks 2/5.

Blocks everything and grows dangerous near death.

- **Shield Wall** (Swordsman, 2): Blocking gives the nearest ally a shield of 10% of the blocked blow. *REQUIRES_ENGINE_HOOK*
- **Iron Body** (Monk, 1): Your hurt stagger ends a quarter sooner. *REQUIRES_ENGINE_HOOK*
- **Reinforced Armour** (Artificer, 1): 8% of each hit you take is given back. *IMPLEMENTED*
- **Nagashi** (Samurai, 3): Blocks within a second of each other each raise your next counter's damage by 20%, up to 3 times. *REQUIRES_ENGINE_HOOK*
- **Last Stand** (Swordsman, 6): Below a quarter of your HP you take half damage and deal 30% more; above half HP you deal 15% less. *IMPLEMENTED*

## Battle Chaplain

Primary: **Cleric** (support). Capacity 9. Playable perks 0/5.

Heals become shields, and whoever hurts an ally pays.

- **Blessing** (Cleric, 2): Your heals also give their target a shield of 5% of its max HP for 5 seconds. *REQUIRES_ENGINE_HOOK*
- **Sanctuary** (Cleric, 1): Allies within 48 pixels of you get a tenth of each hit back. *REQUIRES_ENGINE_HOOK*
- **Mercy** (Cleric, 1): Heals on someone below a quarter of their HP are 50% stronger. *REQUIRES_ENGINE_HOOK*
- **Judgement** (Cleric, 2): 20% more damage on a monster that hurt an ally in the last 3 seconds. *REQUIRES_ENGINE_HOOK*
- **Overflow** (Universal, 3): Healing above your max HP becomes a shield of up to 10% of max HP. *REQUIRES_NEW_EFFECT_PRIMITIVE*

## Pack Leader

Primary: **Summoner** (companion). Capacity 11. Playable perks 1/5.

Familiar, companions and P2 focus the same marked monster.

- **Shared Instinct** (Summoner, 2): Your familiar attacks the monster you are locked on to. *REQUIRES_ENGINE_HOOK*
- **Tag Team** (Summoner, 2): Hitting your familiar's target within a second of its hit: 20% more damage. *REQUIRES_ENGINE_HOOK*
- **Pack Tactics** (Summoner, 1): 15% more damage while an ally or familiar is within 48 pixels of your target. *IMPLEMENTED*
- **Hunter's Mark** (Hunter, 2): Three hits mark a monster for 10 seconds; the whole party deals 10% more to it. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Duet** (Dancer, 4): When two players hit the same monster within half a second, both hits deal 25% more. *REQUIRES_ENGINE_HOOK*

## Glass Cannon Archer

Primary: **Archer** (glass cannon). Capacity 11. Playable perks 3/5.

Huge ranged hits; one mistake hurts.

- **Dead Man's Hand** (Archer, 6): Ranged hits deal 50% more, melee hits 50% less, and you cannot guard. *REQUIRES_ENGINE_HOOK*
- **Deadeye** (Archer, 1): 20% more damage on a monster more than 64 pixels away. *IMPLEMENTED*
- **Last Bullet** (Archer, 2): Every 6th ranged hit deals 60% more damage. *IMPLEMENTED*
- **Wide Focus** (Mage, 1): 10% more spell damage while no monster is within 32 pixels of you. *REQUIRES_ENGINE_HOOK*
- **Full Force** (Universal, 1): At full HP your hits deal 10% more damage. *IMPLEMENTED*

## Blood Mage

Primary: **Warlock** (glass cannon). Capacity 18. Playable perks 0/5.

Pays for spells in HP and turns crits into explosions.

- **Blood Price** (Warlock, 2): With too little MP, your spells cost 8% of your max HP instead. *REQUIRES_ENGINE_HOOK*
- **Mana Shell** (Mage, 6): All damage drains MP first (1 MP per 3 HP); at 0 MP you take 25% more damage. *REQUIRES_ENGINE_HOOK*
- **Soul Reservoir** (Necromancer, 2): Kills store a soul (up to 5); your next spell spends them for 10% more damage each. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Spellblade** (Mage, 2): Casting a spell makes your next melee hit within 3 seconds 30% stronger. *REQUIRES_ENGINE_HOOK*
- **Black Star** (Warlock, 6): Critical hits no longer deal extra damage: they detonate for half the hit on the monsters within 32 pixels. *REQUIRES_ENGINE_HOOK*

## Whirling Dervish

Primary: **Dancer** (mobility). Capacity 15. Playable perks 0/5.

Never stops moving; every dodge is an attack.

- **Pirouette** (Dancer, 4): A perfect dodge spins: a quarter of your last hit to the monsters within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Waltz** (Dancer, 2): Dodging past a monster within 16 pixels throws it off balance: your next hit on it deals 25% more. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Endless Dance** (Dancer, 7): Your combo never ends while you hit or dodge every 1.5 seconds; a hit taken resets it and stuns you half a second. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Shadow Step** (Ninja, 1): Your dodge rolls travel 4 frames further. *REQUIRES_ENGINE_HOOK*
- **Momentum** (Weaponmaster, 1): Each hit in a chain adds a frame to your dodge, up to 4, for 3 seconds. *REQUIRES_ENGINE_HOOK*

## Demolitionist

Primary: **Artificer** (crowd control). Capacity 17. Playable perks 0/5.

Everything explodes, the bomber included.

- **Chain Detonation** (Artificer, 4): Your bombs and mines set each other off within 32 pixels. *REQUIRES_ENGINE_HOOK*
- **Mad Bomber** (Artificer, 6): Your explosions deal 60% more but also hit you for 10% of their damage. *REQUIRES_ENGINE_HOOK*
- **Improvised Explosives** (Artificer, 2): Critical hits drop a small bomb that bursts a second later: a quarter of the hit within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Explosive Rounds** (Archer, 2): Critical ranged hits explode: 30% to the other monsters within 24 pixels. *REQUIRES_ENGINE_HOOK*
- **Combustion** (Mage, 3): A fire spell kill sets the monsters within 32 pixels alight for 20% of the killing blow. *REQUIRES_ENGINE_HOOK*

## Saint of the Hunt

Primary: **Hunter** (melee). Capacity 14. Playable perks 2/5.

Built for undead and bosses only.

- **Night Hunter** (Hunter, 6): 40% more damage against undead and bosses, 25% less against everything else. *IMPLEMENTED*
- **Stake Through the Heart** (Hunter, 3): An undead monster below 20% of its HP dies to your next hit. *IMPLEMENTED*
- **Sacred Flame** (Cleric, 1): 30% more damage against undead. *REQUIRES_ENGINE_HOOK*
- **Hamstring** (Hunter, 2): Whip hits on a marked monster slow it to half speed for a second. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Hunter's Mark** (Hunter, 2): Three hits mark a monster for 10 seconds; the whole party deals 10% more to it. *REQUIRES_NEW_EFFECT_PRIMITIVE*

## Puppeteer

Primary: **Mentalist** (crowd control). Capacity 17. Playable perks 0/5.

Monsters fight each other while the Mentalist watches.

- **Puppet Master** (Mentalist, 7): Misdirection lasts 3 swings instead of one, but your own hits deal 30% less. *REQUIRES_ENGINE_HOOK*
- **Feint** (Rogue, 4): Shadowstepping behind a monster makes its next attack strike its nearest fellow monster instead. *REQUIRES_ENGINE_HOOK*
- **Lingering Whisper** (Mentalist, 2): Whisper marks last twice as long and a monster can carry 4. *REQUIRES_ENGINE_HOOK*
- **Mind Break** (Mentalist, 2): Each Whisper mark lowers the monster's magic defence by 10%. *REQUIRES_ENGINE_HOOK*
- **Shared Instinct** (Summoner, 2): Your familiar attacks the monster you are locked on to. *REQUIRES_ENGINE_HOOK*

## Unbroken Monk

Primary: **Monk** (defensive). Capacity 15. Playable perks 3/5.

Shrugs off hits with Chi and answers every block.

- **Diamond Body** (Monk, 6): While your class energy is above half, hits take 40% less HP, paid from the energy instead. *IMPLEMENTED*
- **Ukemi Spring** (Monk, 3): A successful ukemi gives back an AP pip and makes your next hit 25% stronger. *REQUIRES_ENGINE_HOOK*
- **Ukemi Master** (Universal, 2): A successful ukemi makes your next hit 20% stronger. *REQUIRES_ENGINE_HOOK*
- **Reversal** (Monk, 3): A perfect block strikes back at once with a palm for half your last hit. *IMPLEMENTED*
- **Inner Peace** (Monk, 1): Out of a fight (no monster near for 3 seconds) you heal 1% of your max HP each second. *IMPLEMENTED*

## Clean Blade

Primary: **Samurai** (melee). Capacity 14. Playable perks 4/5.

Rewards a fight without being touched.

- **Sword Saint** (Samurai, 3): Each sword hit in a row without being hurt adds 4% sword damage, up to 5 times. Getting hurt clears it. *IMPLEMENTED*
- **Clean Fight** (Universal, 2): Each fight won without being hurt gives 5% more damage in the next fight, up to 3 times. *IMPLEMENTED*
- **Ichi no Tachi** (Samurai, 6): Your first hit on each monster deals double damage; every later hit on it deals 30% less. *IMPLEMENTED*
- **Iaijutsu Focus** (Samurai, 1): The first hit after 3 seconds without landing one deals 20% more damage. *IMPLEMENTED*
- **Mushin** (Samurai, 2): A perfect dodge makes your hits critical 25% more often for 4 seconds. *REQUIRES_ENGINE_HOOK*

## Tempo Weaver

Primary: **Weaponmaster** (melee). Capacity 19. Playable perks 2/5.

Never the same weapon twice.

- **Endless Arsenal** (Weaponmaster, 7): Every weapon switch refills the gauge and adds 15% damage, but two hits in a row with the same weapon deal 40% less. *IMPLEMENTED*
- **Thousand Weapons** (Weaponmaster, 4): A combo finisher gives every weapon type used in that combo 10% more damage for 10 seconds. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Pierce then Cleave** (Weaponmaster, 3): A spear hit followed within a second by an axe hit: the axe hit deals 20% more and staggers. *REQUIRES_ENGINE_HOOK*
- **Arsenal Overflow** (Weaponmaster, 3): Hitting with 3 different weapons in one combo restores 15 class energy. *IMPLEMENTED*
- **Perfect Tempo** (Dancer, 2): A swing pressed within 6 frames of the gauge filling deals 15% more damage. *REQUIRES_ENGINE_HOOK*

## Lich King

Primary: **Necromancer** (summoner). Capacity 18. Playable perks 0/5.

A fragile master behind a strong undead familiar.

- **Lich Pact** (Necromancer, 7): Your max HP counts as 30% lower; your familiar deals double damage and heals you 10% of its hits. *REQUIRES_ENGINE_HOOK*
- **Reanimation** (Necromancer, 4): A monster killed by your dark spell rises as an undead familiar for 20 seconds (one at a time). *REQUIRES_ENGINE_HOOK*
- **Grave Marks** (Necromancer, 3): Your hits mark a monster; marked monsters take 15% more from familiars. *REQUIRES_NEW_EFFECT_PRIMITIVE*
- **Familiar Bond** (Summoner, 1): Your familiar has 30% more HP. *REQUIRES_ENGINE_HOOK*
- **Echo Summon** (Summoner, 3): Every 4th familiar strike repeats for half its damage. *REQUIRES_ENGINE_HOOK*
