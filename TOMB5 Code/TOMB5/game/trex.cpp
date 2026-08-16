#include "../tomb5/pch.h"
#include "trex.h"
#include "box.h"
#include "objects.h"
#include "collide.h"
#include "lara.h"
#include "effects.h"
#include "control.h"
#include "items.h"
#include "../specific/function_stubs.h"

//----------------------------------------------------------------------------
// T-Rex aus Tomb Raider 3, portiert aus der tomb3-Dekompilation
// (game/trex.cpp, GPLv3 - dieselbe Lizenz wie TOMB5 und dieses Projekt).
//
// Der KI-Unterbau ist in beiden Spielen derselbe: CreatureActive,
// CreatureAIInfo, GetCreatureMood, CreatureMood, CreatureTurn und
// CreatureAnimation gibt es in TOMB5s box.cpp wortgleich. Deshalb konnte die
// Zustandsmaschine unveraendert uebernommen werden.
//
// DREI STELLEN wurden bewusst weggelassen, weil TR5 kein Gegenstueck hat:
//
// 1. Die Beutesuche. TR3 durchsucht ueber GetNearByRooms/draw_rooms die
//    Nachbarraeume nach RAPTOR und FLARE_ITEM, damit der T-Rex Raptoren
//    frisst und nach geworfenen Fackeln schnappt. Beides existiert in TR5
//    nicht (kein Raptor, andere Raumliste), also ist Lara immer das Ziel.
//    Faellt der Geier oder ein Raptor spaeter dazu, kann das zurueckkommen.
//
// 2. CreatureKill mit EXTRA_DINOKILL. TR3 hat eine eigene Lara-Animation
//    fuers Gefressenwerden; in TR5 gibt es sie nicht, und ein falscher
//    Index wuerde Lara in eine fremde Animation zwingen. Stattdessen macht
//    der Biss toedlichen Schaden - dasselbe Ergebnis ohne Sonderanimation.
//
// 3. Laras Zustandsabfrage (AS_STOP/AS_DUCK) beim Schnuppern. Die Nummern
//    unterscheiden sich zwischen den Spielen; geprueft wird jetzt nur, ob
//    Lara eine Fackel haelt und ob der T-Rex gerade getroffen wurde.
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
        // Ohne die TR3-Beutesuche ist Lara immer das Ziel (siehe Kopf).
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
            lara_item->hit_points -= item->current_anim_state == DINO_RUN ? 10 : 1;

        rex->flags = rex->mood != ESCAPE_MOOD && !info.ahead &&
                     info.enemy_facing > -0x4000 && info.enemy_facing < 0x4000;

        if (!rex->flags && info.distance > 0x225510 && info.distance < 0x1000000 && info.bite)
            rex->flags = 1;

        // Diese Klammer entscheidet, ob der T-Rex Ruhe gibt. TR3 verlangt:
        // keine Fackel, Lara steht oder duckt UND haelt den Zustand, kein
        // Treffer. Nur dann laeuft der Zaehler herunter; sobald Lara sich
        // bewegt, springt er auf 120 zurueck und der Dino bleibt wach.
        //
        // Beim ersten Versuch hatte ich den Stillstand weggelassen, weil TR5
        // andere Zustandsnummern hat - dadurch lief der Zaehler IMMER leer,
        // der Dino war dauerhaft BORED_MOOD und trottete nur herum, statt
        // Lara zu jagen (im Spiel gesehen). Statt der Zustandsnummern wird
        // jetzt Laras Geschwindigkeit geprueft: das ist dieselbe Aussage
        // ("sie bewegt sich nicht") und ueber Spielgrenzen hinweg stabil.
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

            // Der Biss. In TR3 folgt hier CreatureKill mit einer eigenen
            // Lara-Animation; die gibt es in TR5 nicht (siehe Kopf), also
            // toetet der Treffer schlicht.
            if (item->touch_bits & 0x3000)
            {
                CreatureEffect(item, &trex_hit, DoBloodSplat);
                lara_item->hit_points -= 10000;
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
