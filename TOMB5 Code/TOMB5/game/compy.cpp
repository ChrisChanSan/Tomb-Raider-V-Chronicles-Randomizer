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
/*
 * Compsognathus aus Tomb Raider 3, portiert 2026 nach TOMB5 (#99).
 *
 * Uebernommen aus tomb3 (game/compy.cpp, GPLv3) - dieselbe Lizenz wie TOMB5
 * und dieses Projekt. Der Code wird bewusst direkt uebernommen statt
 * umgeschrieben: Umschreiben wuerde die Herkunft verschleiern.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file at the root of this source tree.
 *
 * ----------------------------------------------------------------------------
 * Der Compy sitzt in TR5 auf Slot SAILOR (53). Der Slot haengt bisher an
 * InitialiseSas/SasControl und holt seine Anims von SWAT - benutzt hat ihn nie
 * jemand: in keinem der 16 Level steckt dort ein Modell (mit MissMod an allen
 * nachgemessen, Gegenprobe ueber den Hund). Vierter toter Slot nach SAS 31
 * (T-Rex), CROW 43 (Geier) und REAPER 75 (Raptor).
 *
 * VIER ABWEICHUNGEN gegenueber tomb3, alle hier begruendet:
 *
 * 1. DIE KADAVERSUCHE ENTFAELLT. tomb3 durchsucht den Raum nach einem Objekt
 *    vom Typ ANIMATING6 - in TR3 ist das der tote Raptor, um den sich das
 *    Rudel versammelt und an dem es nagt. In TR5 ist ANIMATING6 ein ganz
 *    normaler Kulissenslot, der in Leveln WIRKLICH belegt ist. Uebernommen
 *    wuerden die Compys also Deko umschwaermen.
 *
 *    Was daran haengt, nachgesehen statt weggelassen (Lehre aus dem T-Rex):
 *      - Im BORED_MOOD war der Kadaver ihr Sammelpunkt. Ohne ihn streifen sie
 *        umher, statt sich zu versammeln.
 *      - Im Zustand COMPY_STOP fuehrte er zu Sprung/Biss aus Langeweile. Der
 *        Zweig entfaellt; der Rueckfall auf COMPY_RUN traegt weiterhin.
 *      - In COMPY_JUMP/COMPY_ATTACK loeste er den Blutspritzer beim Nagen aus.
 *    Der Angriff auf Lara haengt an KEINER dieser Stellen und bleibt komplett.
 *
 * 2. non_lot BLEIBT AUS. tomb3 setzt dem Compy das Bit, damit er die
 *    Boxwegfindung umgeht und frei schwaermt. TR5 fuehrt das Bit zwar in
 *    OBJECT_INFO, liest es aber an KEINER Stelle - die Abfragen sind in
 *    TR4/TR5 aus box.cpp verschwunden. Sie zurueckzubauen waere ein Eingriff
 *    in den gemeinsamen Code ALLER Gegner; der Nutzer hat entschieden, dass
 *    sich die Compys wie normale Gegner bewegen duerfen. Damit bleibt
 *    box.cpp unangetastet.
 *
 * 3. compy_scared_timer wird beim Initialisieren MIT zurueckgesetzt. tomb3
 *    setzt nur compys_attack_lara zurueck. Beides sind Globale, die einen
 *    Levelwechsel ueberleben - in dieser Engine wird viel neu geladen (Level
 *    Select), und ein uebrig gebliebener Schreckzaehler liesse das erste
 *    Rudel im naechsten Level ohne Grund fliehen.
 *
 * 4. ZURUECKHALTUNG HALBIERT (Nutzerentscheidung 2026-08-17). Alle drei
 *    Wartezeiten der Feigheit auf die Haelfte:
 *      - Fluchtdauer        compy_scared_timer  280 -> 140  (~9,3 -> ~4,7 s)
 *      - Sperre vor Angriff item->timer > 180   -> 90       (~6   -> ~3   s)
 *      - dieselbe bei Beschuss  > 200           -> 100      (~6,7 -> ~3,3 s)
 *    Grund: TR3 gibt dem Rudel viel Zeit, weil dort ganze Schwaerme in
 *    weiten Dschungelabschnitten leben und Lara sie lange begleitet. Bei
 *    uns steht EIN Rudel aus vier Tieren irgendwo im Level - neun Sekunden
 *    Flucht heisst dort, dass Lara laengst weg ist, bevor etwas passiert.
 *    Die Feigheit als Verhalten bleibt, sie dauert nur halb so lang.
 *
 * Klaenge kommen ueber die Anim-Kommandos des Modells, hier ist kein Aufruf
 * noetig.
 */
#include "../tomb5/pch.h"
#include "balance.h"
#include "compy.h"
#include "box.h"
#include "objects.h"
#include "effects.h"
#include "control.h"
#include "lara.h"
#include "lara_states.h"
#include "items.h"
#include "../specific/function_stubs.h"
#include "../specific/3dmath.h"

static BITE_INFO compy_hit = { 0, 0, 0, 2 };

long compy_scared_timer;
long compys_attack_lara;

void InitialiseCompy(short item_number)
{
	InitialiseCreature(item_number);
	compys_attack_lara = 0;
	compy_scared_timer = 0;      // siehe Abweichung 3 im Dateikopf
}

void CompyControl(short item_number)
{
	ITEM_INFO* item;
	CREATURE_INFO* compy;
	FLOOR_INFO* floor;
	AI_INFO info;
	short angle, torso, head, bits, room_number;

	if (!CreatureActive(item_number))
		return;

	item = &items[item_number];
	compy = (CREATURE_INFO*)item->data;
	angle = 0;
	head = 0;
	torso = 0;

	if (item->hit_points <= 0)
	{
		if (item->current_anim_state != COMPY_DEATH)
		{
			item->anim_number = objects[item->object_number].anim_index + 6;
			item->current_anim_state = COMPY_DEATH;
			item->frame_number = anims[item->anim_number].frame_base;
		}
	}
	else
	{
		CreatureAIInfo(item, &info);

		// Hier stand die Umlenkung auf den Kadaver - siehe Abweichung 1.

		bits = 512 * (item_number & 7) - 1792;

		if (!compy_scared_timer && !compys_attack_lara && (info.enemy_facing < 0x3000 && info.enemy_facing > -0x3000 && lara_item->speed > 15 || lara_item->current_anim_state == AS_ROLL || item->hit_status))
		{
			item->item_flags[0] = (bits + 1792) >> 7;
			// ABWEICHUNG 4 (Nutzerentscheidung 2026-08-17): alle drei
			// Zurueckhaltungs-Zeiten halbiert. TR3 gab dem Rudel viel Zeit,
			// weil dort ganze Schwaerme in weiten Dschungelabschnitten
			// leben; bei uns steht EIN Rudel aus vier Tieren in einem
			// TR5-Level, und neun Sekunden Flucht sind dort schlicht
			// langweilig. Original: 280 (~9,3 s bei 30 Bildern/s).
			compy_scared_timer = 140;
		}
		else if (compy_scared_timer)
		{
			if (item->item_flags[0] > 0)
				item->item_flags[0]--;
			else
			{
				compy->mood = ESCAPE_MOOD;
				compy_scared_timer--;
			}

			// Halbiert, siehe Abweichung 4. Original: 180 (~6 s).
			if (GetRandomControl() < 0x1F && item->timer > 90)
				compys_attack_lara = 1;
		}
		else
			compy->mood = info.zone_number == info.enemy_zone ? ATTACK_MOOD : BORED_MOOD;

		switch (compy->mood)
		{
		case BORED_MOOD:
			// Ersatz fuer den Kadaver-Sammelpunkt (siehe Abweichung 1).
			//
			// Ohne Kadaver setzte dieser Zweig GAR KEIN Ziel, und compy->target
			// behielt den Wert, mit dem er ins Leben kam - die Compys liefen
			// auf eine Stelle zu, die niemand gesetzt hatte. Im Spiel gesehen:
			// nach dem Wecken erst einmal gegen die naechste Wand. In TR3
			// faellt das nie auf, weil ueberall dort ein Kadaver liegt, wo
			// Compys vorkommen.
			//
			// Lara ist der naheliegende Ersatz: sie streifen in ihrer Naehe
			// umher, ohne sie anzugreifen. Die Feigheit selbst haengt an
			// compys_attack_lara und ist von diesem Ersatz unberuehrt; ihre
			// DAUER ist inzwischen halbiert (Abweichung 4 oben).
			if (compy->enemy)
			{
				compy->target.x = compy->enemy->pos.x_pos;
				compy->target.z = compy->enemy->pos.z_pos;
			}

			break;

		case ATTACK_MOOD:
			compy->target.x = compy->enemy->pos.x_pos + (1024 * phd_sin(bits + item->pos.y_rot) >> W2V_SHIFT);
			compy->target.y = compy->enemy->pos.y_pos;
			compy->target.z = compy->enemy->pos.z_pos + (1024 * phd_cos(bits + item->pos.y_rot) >> W2V_SHIFT);
			break;

		case ESCAPE_MOOD:
		case STALK_MOOD:
			compy->target.x = compy->enemy->pos.x_pos + (1024 * phd_sin(bits + info.angle + 0x8000) >> W2V_SHIFT);
			compy->target.z = compy->enemy->pos.z_pos + (1024 * phd_cos(bits + info.angle + 0x8000) >> W2V_SHIFT);
			room_number = item->room_number;
			floor = GetFloor(compy->target.x, item->pos.y_pos, compy->target.z, &room_number);

			if (abs(boxes[floor->box].height - item->pos.y_pos) > 256)
			{
				compy->mood = BORED_MOOD;
				item->item_flags[0] = (short)compy_scared_timer;
			}

			break;
		}

		angle = CreatureTurn(item, compy->maximum_turn);

		if (info.ahead)
			torso = info.angle;

		head = -(info.angle / 4);
		item->timer++;

		// Halbiert, siehe Abweichung 4. Original: 200 (~6,7 s).
		if (item->hit_status && item->timer > 100 && GetRandomControl() < 0xC1C)
			compys_attack_lara = 1;

		switch (item->current_anim_state)
		{
		case COMPY_STOP:
			compy->flags &= ~1;
			compy->maximum_turn = 546;

			if (compy->mood == ATTACK_MOOD)
			{
				if (info.angle && info.distance < 0x718E4)
				{
					if (compys_attack_lara)
					{
						if (GetRandomControl() < 0x4000)
							item->goal_anim_state = COMPY_ATTACK;
						else
							item->goal_anim_state = COMPY_JUMP;
					}
					else
						item->goal_anim_state = COMPY_STOP;
				}
				else if (info.distance > 0xFF801 - 0x718E4 * compys_attack_lara)
					item->goal_anim_state = COMPY_RUN;
			}
			else if (compy->mood == BORED_MOOD)
			{
				// Der Kadaverzweig entfaellt, der Rueckfall auf RUN traegt.
				if (info.distance > 0x552AB)
					item->goal_anim_state = COMPY_RUN;
			}
			else if (GetRandomControl() < 0x1000)
				item->goal_anim_state = COMPY_JUMP;
			else
				item->goal_anim_state = COMPY_RUN;

			break;

		case COMPY_RUN:
			compy->flags &= ~1;
			compy->maximum_turn = 1820;

			if (info.angle < 0x3000 && info.angle > -0x3000 && info.distance < 0xFF801 - 0x718E4 * compys_attack_lara)
				item->goal_anim_state = COMPY_STOP;

			break;

		case COMPY_JUMP:
		case COMPY_ATTACK:
			compy->maximum_turn = 1820;

			// Der zweite Zweig (Nagen am Kadaver) entfaellt mit Abweichung 1.
			if (!(compy->flags & 1))
			{
				if (item->touch_bits & 4 && compys_attack_lara)
				{
					compy->flags |= 1;
					lara_item->hit_points -= EnemyDamage(balance.dmgCompyBite);
					lara_item->hit_status = 1;
					CreatureEffect(item, &compy_hit, DoBloodSplat);
				}
			}

			break;
		}
	}

	CreatureTilt(item, angle >> 1);
	CreatureJoint(item, 0, torso);
	CreatureJoint(item, 1, head);
	CreatureAnimation(item_number, angle, angle >> 1);
}
