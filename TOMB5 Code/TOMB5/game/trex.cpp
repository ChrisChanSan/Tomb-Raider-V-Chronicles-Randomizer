/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "../tomb5/pch.h"
#include "balance.h"
#include "trex.h"
#include "box.h"
#include "objects.h"
#include "collide.h"
#include "lara.h"
#include "effects.h"
#include "control.h"
#include "items.h"
#include "sound.h"
#include "../specific/function_stubs.h"

//----------------------------------------------------------------------------
// Sound slots of the TR3 sounds. The importer (TR3Insert) ALWAYS stores them
// from 445 on: the original TR3 numbers are partly taken in TR5 (89 in
// every level), while slots 445-449 are free everywhere. The same kind of
// fixed agreement as SFX_SMASH_ROCK for the Hydra.
//----------------------------------------------------------------------------
#define SFX_TREX_ATTACK   445
#define SFX_TREX_ROAR     446
#define SFX_TREX_FOOTSTEP 447
#define SFX_TREX_SNIFF    448
#define SFX_TREX_DEATH    449

//----------------------------------------------------------------------------
// T-Rex from Tomb Raider 3, ported from the tomb3 decompilation
// (game/trex.cpp, GPLv3 - the same license as TOMB5 and this project).
//
// The AI substructure is the same in both games: CreatureActive,
// CreatureAIInfo, GetCreatureMood, CreatureMood, CreatureTurn and
// CreatureAnimation exist word for word in TOMB5's box.cpp. That is why
// the state machine could be taken over unchanged.
//
// THREE SPOTS were deliberately left out because TR5 has no counterpart:
//
// 1. The prey search. TR3 searches the neighboring rooms via
//    GetNearByRooms/draw_rooms for RAPTOR and FLARE_ITEM, so the T-Rex
//    eats raptors and snaps at thrown flares. Neither exists in TR5 (no
//    raptor, a different room list), so Lara is always the target. If the
//    vulture or a raptor joins later, this can come back.
//
// 2. CreatureKill with EXTRA_DINOKILL. TR3 has its own Lara animation for
//    being eaten; TR5 does not, and a wrong index would force Lara into a
//    foreign animation. Instead the bite deals lethal damage - the same
//    result without the special animation.
//
// 3. Lara's state query (AS_STOP/AS_DUCK) while sniffing. The numbers
//    differ between the games; what is checked now is only whether Lara
//    holds a torch and whether the T-Rex was just hit.
//----------------------------------------------------------------------------

static BITE_INFO trex_hit = { 0, 32, 64, 13 };

void DinoControl(short item_number)
{
    ITEM_INFO* item;
    CREATURE_INFO* rex;
    AI_INFO info;
    long x, y, z;
    short angle;

    if (!CreatureActive(item_number))
        return;

    item = &items[item_number];
    rex = (CREATURE_INFO*)item->data;
    angle = 0;

    if (item->hit_points <= 0)
    {
        if (item->current_anim_state == DINO_STOP)
            item->goal_anim_state = DINO_DEATH;
        else
            item->goal_anim_state = DINO_STOP;
    }
    else
    {
        // Without the TR3 prey search, Lara is always the target (see the header).
        rex->enemy = lara_item;

        CreatureAIInfo(item, &info);
        GetCreatureMood(item, &info, 1);

        if (!item->item_flags[0] && !item->item_flags[1] && rex->enemy == lara_item)
            rex->mood = BORED_MOOD;

        CreatureMood(item, &info, 1);

        if (rex->mood == BORED_MOOD)
            rex->maximum_turn >>= 1;

        angle = CreatureTurn(item, rex->maximum_turn);

        if (item->touch_bits)
            lara_item->hit_points -= EnemyDamage(item->current_anim_state == DINO_RUN ? balance.dmgTRexKickRun : balance.dmgTRexKick);

        rex->flags = rex->mood != ESCAPE_MOOD && !info.ahead &&
                     info.enemy_facing > -0x4000 && info.enemy_facing < 0x4000;

        if (!rex->flags && info.distance > 0x225510 && info.distance < 0x1000000 && info.bite)
            rex->flags = 1;

        // This bracket decides whether the T-Rex settles down. TR3 demands:
        // no flare, Lara stands or crouches AND holds the state, no hit.
        // Only then does the counter run down; as soon as Lara moves it
        // jumps back to 120 and the dino stays awake.
        //
        // On the first attempt I had left out the standstill because TR5
        // has different state numbers - the counter thereby ALWAYS drained,
        // the dino was permanently BORED_MOOD and just trotted around
        // instead of hunting Lara (seen in game). Instead of the state
        // numbers, Lara's speed is now checked: the same statement ("she is
        // not moving") and stable across game boundaries.
        if (lara.gun_type != LG_FLARE && !lara_item->speed && !item->hit_status)
        {
            if (item->item_flags[0] > 0)
                item->item_flags[0]--;
        }
        else
        {
            item->item_flags[0] = 120;
            item->item_flags[1] = 3;
        }

        switch (item->current_anim_state)
        {
        case DINO_STOP:

            if (item->required_anim_state)
                item->goal_anim_state = item->required_anim_state;
            else if (rex->mood == BORED_MOOD || rex->flags)
                item->goal_anim_state = DINO_WALK;
            else if (rex->mood == ESCAPE_MOOD)
            {
                if (lara.target != item && info.ahead && !item->hit_status)
                {
                    if (GetRandomControl() < 256)
                        item->goal_anim_state = DINO_ROAR;
                    else
                        item->goal_anim_state = DINO_STOP;
                }
                else
                    item->goal_anim_state = DINO_RUN;
            }
            else if (info.distance < 0x225510 && info.bite)
            {
                if (item->item_flags[0])
                    item->goal_anim_state = DINO_ATTACK2;
                else if (GetRandomControl() & 1)
                {
                    if (item->item_flags[1])
                        item->goal_anim_state = DINO_LONGROARSTART;
                }
                else if (item->item_flags[1])
                    item->goal_anim_state = DINO_SNIFFSTART;
            }
            else
                item->goal_anim_state = DINO_RUN;

            break;

        case DINO_WALK:
            rex->maximum_turn = 364;

            if (rex->mood != BORED_MOOD || !rex->flags)
                item->goal_anim_state = DINO_STOP;
            else if (info.ahead && GetRandomControl() < 0x100)
            {
                item->required_anim_state = DINO_ROAR;
                item->goal_anim_state = DINO_STOP;
            }

            break;

        case DINO_RUN:
            rex->maximum_turn = 728;

            if (info.distance < 0x1900000 && info.bite)
                item->goal_anim_state = DINO_STOP;
            else if (rex->flags)
                item->goal_anim_state = DINO_STOP;
            else if (rex->mood == ESCAPE_MOOD || !info.ahead || GetRandomControl() >= 256)
            {
                if (rex->mood == BORED_MOOD)
                    item->goal_anim_state = DINO_STOP;
                else if (rex->mood == ESCAPE_MOOD && lara.target != item && info.ahead)
                    item->goal_anim_state = DINO_STOP;
            }
            else
            {
                item->required_anim_state = DINO_ROAR;
                item->goal_anim_state = DINO_STOP;
            }

            break;

        case DINO_ROAR:
            rex->maximum_turn = 0;
            break;

        case DINO_ATTACK2:
            rex->maximum_turn = 364;

            // The bite. In TR3, CreatureKill follows here with its own
            // Lara animation - the snap with which he eats her in one go.
            // TR5 does not have that animation, and CreatureKill is
            // declared there but called by NO enemy (measured) - a dead
            // shell like the SAS slot. Without the animation the engine
            // falls back to a plain fall.
            //
            // The SOUND hangs on exactly that animation in TR3 and would
            // thus stay mute. It is therefore triggered directly here: the
            // snap is heard even though the special animation is missing.
            if (item->touch_bits & 0x3000)
            {
                SoundEffect(SFX_TREX_ATTACK, &item->pos, 0);
                CreatureEffect(item, &trex_hit, DoBloodSplat);
                lara_item->hit_points -= EnemyDamage(balance.dmgTRexBite);
                lara_item->hit_status = 1;
                rex->maximum_turn = 0;
            }

            if (!(GetRandomControl() & 3))
                item->required_anim_state = DINO_WALK;

            break;

        case DINO_KILL:
            rex->maximum_turn = 0;
            CreatureEffect(item, &trex_hit, DoBloodSplat);
            break;

        case DINO_LONGROARSTART:
            rex->maximum_turn = 0;

            if (item->item_flags[1] > 0 && item->frame_number == anims[item->anim_number].frame_base)
                item->item_flags[1]--;

            item->goal_anim_state = DINO_LONGROAREND;
            break;

        case DINO_SNIFFSTART:
            rex->maximum_turn = 0;

            if (item->item_flags[1] > 0 && item->frame_number == anims[item->anim_number].frame_base)
                item->item_flags[1]--;

            break;

        case DINO_SNIFFMID:
            rex->maximum_turn = 0;

            if (item->frame_number == anims[item->anim_number].frame_base)
            {
                if (GetRandomControl() & 1 && item->item_flags[1] && !item->item_flags[0])
                {
                    item->goal_anim_state = DINO_SNIFFMID;

                    if (item->item_flags[1] > 0)
                        item->item_flags[1]--;
                }
                else
                    item->goal_anim_state = DINO_SNIFFEND;
            }

            break;
        }
    }

    CreatureAnimation(item_number, angle, 0);

    item->collidable = item->hit_points > 0;
}
